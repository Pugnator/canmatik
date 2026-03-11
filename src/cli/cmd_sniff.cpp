/// @file cmd_sniff.cpp
/// `canmatik sniff` subcommand — display only changed CAN frames with diff highlighting.

#include "cli/cli_app.h"
#include "cli/formatters.h"
#include "cli/provider_select.h"
#include "core/capture_sink.h"
#include "core/filter.h"
#include "core/log_macros.h"
#include "core/timestamp.h"
#include "mock/mock_provider.h"
#include "platform/win32/j2534_provider.h"
#include "services/capture_service.h"
#include "services/session_service.h"
#include "transport/transport_error.h"

#include <nlohmann/json.hpp>

#include <array>
#include <atomic>
#include <csignal>
#include <format>
#include <iostream>
#include <string>
#include <thread>
#include <unordered_map>
#include <vector>

namespace canmatik {

namespace {

std::atomic<bool> g_sniff_stop{false};

void sniff_signal_handler(int /*sig*/) {
    g_sniff_stop.store(true);
}

// ANSI escape codes for Windows terminal coloring
constexpr const char* kAnsiReset   = "\033[0m";
constexpr const char* kAnsiRed     = "\033[1;31m";  // Changed bytes
constexpr const char* kAnsiGreen   = "\033[1;32m";  // New ID (first seen)
constexpr const char* kAnsiYellow  = "\033[1;33m";  // DLC changed
constexpr const char* kAnsiCyan    = "\033[36m";     // Arb ID

/// State tracked per arbitration ID.
struct FrameState {
    uint8_t dlc = 0;
    std::array<uint8_t, 8> data = {};
    bool seen = false;
};

/// Format a frame highlighting changed bytes in color.
/// Returns the formatted string with ANSI escape codes.
std::string format_sniff_text(const CanFrame& frame, uint64_t session_start_us,
                              const FrameState* prev) {
    double rel = session_relative_seconds(frame.host_timestamp_us, session_start_us);

    std::string id_str;
    if (frame.type == FrameType::Extended) {
        id_str = std::format("{:08X}", frame.arbitration_id);
    } else {
        id_str = std::format("{:03X}", frame.arbitration_id);
    }

    std::string line;
    line += std::format("   +{:.6f}  {}{}{}", rel, kAnsiCyan, id_str, kAnsiReset);
    line += std::format("  {}  [{}]  ", frame_type_to_string(frame.type), frame.dlc);

    // If first time seeing this ID, show all bytes in green
    if (!prev || !prev->seen) {
        line += kAnsiGreen;
        for (uint8_t i = 0; i < frame.dlc; ++i) {
            if (i > 0) line += ' ';
            line += std::format("{:02X}", frame.data[i]);
        }
        line += kAnsiReset;
        line += "  [NEW]";
        return line;
    }

    // DLC changed?
    bool dlc_changed = (frame.dlc != prev->dlc);
    if (dlc_changed) {
        // Rewrite DLC portion with color
        // Already printed DLC above; add a marker at end
    }

    // Compare each byte and color the changed ones
    for (uint8_t i = 0; i < frame.dlc; ++i) {
        if (i > 0) line += ' ';
        bool changed = (i >= prev->dlc) || (frame.data[i] != prev->data[i]);
        if (changed) {
            line += kAnsiRed;
            line += std::format("{:02X}", frame.data[i]);
            line += kAnsiReset;
        } else {
            line += std::format("{:02X}", frame.data[i]);
        }
    }

    if (dlc_changed) {
        line += std::format("  {}[DLC {}->{}]{}", kAnsiYellow, prev->dlc, frame.dlc, kAnsiReset);
    }

    return line;
}

/// Format a sniff frame as JSON with change metadata.
std::string format_sniff_json(const CanFrame& frame, uint64_t session_start_us,
                              const FrameState* prev) {
    double rel = session_relative_seconds(frame.host_timestamp_us, session_start_us);
    bool ext = (frame.type == FrameType::Extended);

    std::string id_str = ext ? std::format("{:08X}", frame.arbitration_id)
                             : std::format("{:03X}", frame.arbitration_id);

    std::string payload;
    for (uint8_t i = 0; i < frame.dlc; ++i) {
        if (i > 0) payload += ' ';
        payload += std::format("{:02X}", frame.data[i]);
    }

    nlohmann::json j;
    j["ts"]   = rel;
    j["id"]   = id_str;
    j["dlc"]  = frame.dlc;
    j["data"] = payload;

    // Changed byte indices
    nlohmann::json changed = nlohmann::json::array();
    if (prev && prev->seen) {
        for (uint8_t i = 0; i < frame.dlc; ++i) {
            if (i >= prev->dlc || frame.data[i] != prev->data[i]) {
                changed.push_back(i);
            }
        }
        j["new"] = false;
    } else {
        j["new"] = true;
    }
    j["changed"] = changed;

    return j.dump();
}

/// Sink that tracks per-ID state and only emits frames with changed payload.
class SniffSink : public ICaptureSync {
public:
    SniffSink(bool json_mode, uint64_t session_start_us)
        : json_mode_(json_mode), session_start_us_(session_start_us) {}

    void onFrame(const CanFrame& frame) override {
        uint32_t id = frame.arbitration_id;
        auto it = state_.find(id);
        const FrameState* prev = (it != state_.end()) ? &it->second : nullptr;

        // Check if data actually changed (or first time seeing this ID)
        bool is_new = !prev || !prev->seen;
        bool changed = is_new;
        if (!is_new) {
            if (frame.dlc != prev->dlc) {
                changed = true;
            } else {
                for (uint8_t i = 0; i < frame.dlc; ++i) {
                    if (frame.data[i] != prev->data[i]) {
                        changed = true;
                        break;
                    }
                }
            }
        }

        if (!changed) return; // Skip unchanged frames

        // Output the frame
        if (json_mode_) {
            std::cout << format_sniff_json(frame, session_start_us_, prev) << '\n';
        } else {
            std::cout << format_sniff_text(frame, session_start_us_, prev) << '\n';
        }

        // Update stored state
        FrameState& s = state_[id];
        s.seen = true;
        s.dlc = frame.dlc;
        uint8_t copy_len = (frame.dlc <= 8) ? frame.dlc : 8;
        for (uint8_t i = 0; i < copy_len; ++i) s.data[i] = frame.data[i];
    }

    void onError(const TransportError& error) override {
        LOG_ERROR("Transport error: {}", error.what());
    }

    uint64_t unique_ids() const { return state_.size(); }

private:
    bool json_mode_;
    uint64_t session_start_us_;
    std::unordered_map<uint32_t, FrameState> state_;
};

} // anonymous namespace

int dispatch_sniff(CLI::App& sub, const GlobalOptions& globals, const Config& config) {
    LOG_DEBUG("dispatch_sniff");

    // Enable ANSI escape processing on Windows console
#ifdef _WIN32
    {
        HANDLE hOut = GetStdHandle(STD_OUTPUT_HANDLE);
        if (hOut != INVALID_HANDLE_VALUE) {
            DWORD mode = 0;
            if (GetConsoleMode(hOut, &mode)) {
                SetConsoleMode(hOut, mode | ENABLE_VIRTUAL_TERMINAL_PROCESSING);
            }
        }
    }
#endif

    // --- Parse options ---
    uint32_t bitrate = 500000;
    std::vector<std::string> filter_specs;

    if (auto* opt = sub.get_option_no_throw("--bitrate")) {
        if (opt->count() > 0) bitrate = opt->as<uint32_t>();
    }
    if (auto* opt = sub.get_option_no_throw("--filter")) {
        if (opt->count() > 0) filter_specs = opt->as<std::vector<std::string>>();
    }

    // --- Build filter engine ---
    FilterEngine filter;
    for (const auto& spec : filter_specs) {
        FilterRule rule;
        auto err = parse_filter(spec, rule);
        if (!err.empty()) {
            std::cerr << "Error: Invalid filter '" << spec << "': " << err << '\n';
            return 1;
        }
        filter.add_rule(rule);
    }

    // --- Select provider ---
    SessionService session;
    if (globals.mock) {
        LOG_INFO("Using MockProvider for sniff");
        session.setProvider(std::make_unique<MockProvider>());
    } else {
        session.setProvider(std::make_unique<J2534Provider>());
    }

    auto providers = session.scan();
    if (providers.empty()) {
        std::cerr << "Error: No J2534 providers found. Use --mock for simulated traffic.\n";
        return 2;
    }

    auto selected = select_provider(providers, config.provider);
    if (!selected) {
        std::cerr << "Error: No provider matching '" << config.provider << "'.\n";
        return 2;
    }

    try {
        session.connect(*selected);
        session.openChannel(bitrate);
    } catch (const TransportError& e) {
        std::cerr << "Error: " << e.what() << '\n';
        return 2;
    }

    // --- Set up capture service with sniff sink ---
    CaptureService capture;
    capture.setFilter(filter);

    uint64_t session_start_us = host_timestamp_us();
    SniffSink sniff_sink(globals.json, session_start_us);
    capture.addSink(&sniff_sink);

    auto& status = session.mutableStatus();
    if (globals.mock) status.provider_name = "MockProvider";

    if (!globals.json) {
        std::cout << "[Sniff] Monitoring for changes — only modified frames are shown\n";
    }
    print_session_header(status, globals.json);

    // --- Install Ctrl+C handler ---
    g_sniff_stop.store(false);
    auto prev_handler = std::signal(SIGINT, sniff_signal_handler);

    // --- Start capture ---
    capture.start(session.channel(), status);

    while (!g_sniff_stop.load() && capture.isRunning()) {
        capture.drain();
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }

    capture.stop();
    std::signal(SIGINT, prev_handler);

    if (!globals.json) {
        std::cout << std::format("[Sniff] {} unique IDs tracked\n", sniff_sink.unique_ids());
    }
    print_session_footer(status, globals.json);

    session.closeChannel();
    session.disconnect();

    return 0;
}

} // namespace canmatik
