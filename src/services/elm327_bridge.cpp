#include "services/elm327_bridge.h"

#include "services/session_service.h"
#include "platform/win32/serial_channel.h"
#include "platform/win32/j2534_provider.h"
#include "mock/mock_provider.h"
#include "mock/scriptable_mock_provider.h"
#include "obd/obd_session.h"
#include "core/log_macros.h"
#include "core/timestamp.h"
#include "services/capture_service.h"
#include "services/global_capture.h"

#include <memory>

#include <algorithm>
#include <cctype>
#include <chrono>
#include <iostream>
#include <sstream>
#include <iomanip>
#include <thread>

namespace canmatik {

static std::string trim(const std::string& s) {
    size_t a = 0, b = s.size();
    while (a < b && std::isspace((unsigned char)s[a])) ++a;
    while (b > a && std::isspace((unsigned char)s[b-1])) --b;
    return s.substr(a, b-a);
}

Elm327Bridge::Elm327Bridge(std::string serial_port, std::string provider_name, ICaptureSync* frame_sink, bool terminated, uint32_t baud, std::string mock_script)
    : serial_port_(std::move(serial_port)), provider_name_(std::move(provider_name)), session_(), frame_sink_(frame_sink), stop_flag_{false}, last_error_(), terminated_mode_(terminated), serial_baud_(baud), mock_script_path_(std::move(mock_script)) {}

Elm327Bridge::~Elm327Bridge() = default;

int Elm327Bridge::run() {
    LOG_INFO("Starting ELM327 bridge: serial='{}' provider='{}'", serial_port_, provider_name_);

    // Open serial channel
    SerialChannel serial(serial_port_);
    try {
        serial.open(serial_baud_);
    } catch (const std::exception& ex) {
        LOG_ERROR("Failed to open serial port: {}", ex.what());
        last_error_ = std::string("Failed to open serial port: ") + ex.what();
        return 2;
    }

    // Start session to J2534 provider
    session_ = std::make_unique<SessionService>();
    if (terminated_mode_) {
        if (!mock_script_path_.empty()) {
            auto sp = std::make_unique<ScriptableMockProvider>(mock_script_path_);
            if (!sp->last_error().empty())
                LOG_INFO("Mock script load warning: {}", sp->last_error());
            LOG_INFO("Using scriptable mock with {} rules from '{}'",
                     sp->rule_count(), mock_script_path_);
            session_->setProvider(std::move(sp));
        } else {
            auto mp = std::make_unique<MockProvider>();
            mp->set_frame_rate(0); // silence synthetic traffic in terminated mode
            session_->setProvider(std::move(mp));
        }
    } else {
        session_->setProvider(std::make_unique<J2534Provider>());
    }

    auto providers = session_->scan();
    DeviceInfo selected;
    bool found = false;
    for (auto& p : providers) {
        if (p.name == provider_name_) { selected = p; found = true; break; }
    }
    if (!found) {
        if (!providers.empty()) selected = providers.front();
        else {
            LOG_ERROR("No J2534 providers found");
            last_error_ = "No J2534 providers found";
            return 2;
        }
    }

    try {
        session_->connect(selected);
        session_->openChannel(500000);
    } catch (const std::exception& ex) {
        LOG_ERROR("Failed to open J2534 provider: {}", ex.what());
        last_error_ = std::string("Failed to open J2534 provider: ") + ex.what();
        return 2;
    }

    ObdSession obd(*session_->channel());

    // Use the global capture service so GUI and bridge can share sinks/readers.
    ICaptureSync* registered_sink = nullptr;
    // Internal debug sink if none provided
    class BridgeSinkLocal : public ICaptureSync {
    public:
        void onFrame(const CanFrame& frame) override {
            LOG_DEBUG("Sniffed frame id=0x{:X} dlc={}", frame.arbitration_id, frame.dlc);
        }
        void onError(const TransportError& err) override {
            LOG_ERROR("Capture error: {}", err.what());
        }
    } bridge_sink_local;

    if (frame_sink_) {
        AddGlobalSink(frame_sink_);
        registered_sink = frame_sink_;
    } else {
        AddGlobalSink(&bridge_sink_local);
        registered_sink = &bridge_sink_local;
    }

    // Ensure the sink is removed on any exit from this function to avoid leaving
    // the capture service running with a channel owned by this session.
    auto remove_sink_guard = std::unique_ptr<ICaptureSync, void(*)(ICaptureSync*)>(
        registered_sink, [](ICaptureSync* s){ if (s) RemoveGlobalSink(s); });

    // Start the global capture reader for this channel (no-op if already running).
    StartGlobalCapture(session_->channel(), session_->mutableStatus());

    // Helper: build a display frame for the GUI collector.
    // id=0x7F0 for serial RX (diag tool → bridge), id=0x7F1 for TX (bridge → diag tool).
    auto push_display_frame = [this](uint32_t id, const std::string& text) {
        if (!frame_sink_) return;
        size_t off = 0;
        while (off < text.size()) {
            CanFrame df{};
            df.arbitration_id = id;
            df.type = FrameType::Standard;
            size_t chunk = std::min<size_t>(8, text.size() - off);
            df.dlc = static_cast<uint8_t>(chunk);
            for (size_t i = 0; i < chunk; ++i) df.data[i] = static_cast<uint8_t>(text[off + i]);
            df.host_timestamp_us = host_timestamp_us();
            frame_sink_->onFrame(df);
            off += chunk;
        }
    };

    // Helper: write an ASCII string back to the serial port as CanFrame chunks,
    // and push a copy to the GUI collector.
    auto write_string = [&serial, &push_display_frame](const std::string& s){
        size_t off = 0;
        while (off < s.size()) {
            CanFrame wf{};
            wf.arbitration_id = 0x7FF; wf.type = FrameType::Standard;
            size_t chunk = std::min<size_t>(8, s.size() - off);
            wf.dlc = static_cast<uint8_t>(chunk);
            for (size_t i = 0; i < chunk; ++i) wf.data[i] = static_cast<uint8_t>(s[off + i]);
            serial.write(wf);
            off += chunk;
        }
        push_display_frame(0x7F1, s);  // TX: bridge → diag tool
    };

    // Simple line reader loop from serial. We'll read bytes and split on CR/LF.
    std::string buffer;
    while (!stop_flag_.load()) {
        auto frames = serial.read(1000);
        if (!frames.empty()) {
            // Append bytes as ASCII
            for (const auto& f : frames) {
                for (int i = 0; i < f.dlc; ++i) buffer.push_back(static_cast<char>(f.data[i]));
            }

            // Process lines (accept \r or \n as delimiter — ELM327 uses \r)
            size_t pos;
            while ((pos = buffer.find_first_of("\r\n")) != std::string::npos) {
                std::string line = buffer.substr(0, pos);
                // Skip all consecutive \r/\n characters
                size_t end = buffer.find_first_not_of("\r\n", pos);
                buffer.erase(0, end != std::string::npos ? end : buffer.size());
                line = trim(line);
                if (line.empty()) continue;

                LOG_DEBUG("ELM327 RX: {}", line);
                push_display_frame(0x7F0, line + "\r");  // RX: diag tool → bridge

              try {
                // AT commands
                if (line.size() >= 2 && (line[0]=='A' || line[0]=='a') && (line[1]=='T' || line[1]=='t')) {
                    write_string("OK\r\n");
                    continue;
                }

                // Parse hex tokens — each token must be exactly 2 hex digits (one byte)
                std::istringstream iss(line);
                std::vector<uint8_t> bytes;
                std::string tok;
                bool parse_ok = true;
                while (iss >> tok) {
                    // Remove non-hex characters
                    tok.erase(std::remove_if(tok.begin(), tok.end(), [](char c){ return !isxdigit((unsigned char)c); }), tok.end());
                    if (tok.empty()) continue;
                    // Process pairs of hex digits
                    for (size_t i = 0; i + 1 < tok.size(); i += 2) {
                        std::string byte_str = tok.substr(i, 2);
                        bytes.push_back(static_cast<uint8_t>(std::stoul(byte_str, nullptr, 16)));
                    }
                }

                if (bytes.empty()) {
                    write_string("?\r\n");
                    continue;
                }

                uint8_t mode = bytes[0];
                uint8_t pid = (bytes.size() > 1) ? bytes[1] : 0x00;

                // Handle DTC read (03) and clear (04) and generic queries
                if (mode == 0x03) {
                    auto dtcs = obd.read_dtcs();
                    std::string s;
                    if (dtcs && !dtcs->empty()) {
                        for (size_t i = 0; i < dtcs->size(); ++i) {
                            if (i) s.push_back(' ');
                            s += (*dtcs)[i].code;
                        }
                        s += "\r\n";
                    } else {
                        s = "NO DATA\r\n";
                    }
                    write_string(s);
                } else if (mode == 0x04) {
                    auto r = obd.clear_dtcs(true);
                    write_string(r ? "OK\r\n" : "ERROR\r\n");
                } else {
                    // Generic mode+pid
                    auto dec = obd.query_pid(mode, pid);
                    if (dec) {
                        std::ostringstream out;
                        for (size_t i = 0; i < dec->raw_bytes.size(); ++i)
                            out << std::hex << std::uppercase << std::setw(2) << std::setfill('0') << (int)dec->raw_bytes[i] << ' ';
                        out << "\r\n";
                        write_string(out.str());
                    } else {
                        write_string("NO DATA\r\n");
                    }
                }
              } catch (const std::exception& ex) {
                LOG_ERROR("ELM327 bridge error processing '{}': {}", line, ex.what());
                write_string("ERROR\r\n");
              }
            }
        }

        // Drain global capture queue and sleep a little to avoid busy loop
        DrainGlobalCapture();
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }

    // Ensure global capture is stopped before destroying session/channel
    StopGlobalCaptureForced();
    return 0;
}

std::string Elm327Bridge::last_error() const {
    return last_error_;
}

void Elm327Bridge::stop() {
    stop_flag_.store(true);
}

void Elm327Bridge::handle_line(const std::string& /*line*/) {
    // placeholder for future; logic in run() for now
}

} // namespace canmatik
