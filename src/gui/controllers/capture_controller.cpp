/// @file capture_controller.cpp
/// Live capture lifecycle implementation.

#include "gui/controllers/capture_controller.h"
#include "mock/mock_provider.h"
#include "platform/win32/j2534_provider.h"
#include "transport/transport_error.h"
#include "cli/provider_select.h"

#include <format>

namespace canmatik {

std::string CaptureController::connect(const std::string& provider_name,
                                        uint32_t bitrate, bool mock,
                                        FrameCollector& collector,
                                        BusProtocol protocol) {
    try {
        if (mock)
            session_.setProvider(std::make_unique<MockProvider>());
        else
            session_.setProvider(std::make_unique<J2534Provider>());

        auto providers = session_.scan();
        if (providers.empty())
            return "No J2534 interfaces found.";

        // Pick matching interface or first available
        const DeviceInfo* sel = nullptr;
        if (!provider_name.empty()) {
            for (auto& p : providers) {
                if (p.name.find(provider_name) != std::string::npos) {
                    sel = &p; break;
                }
            }
            if (!sel) return std::format("No interface matching '{}'.", provider_name);
        } else {
            sel = &providers[0];
        }

        session_.connect(*sel);
        session_.openChannel(bitrate, protocol);
        auto& st = session_.mutableStatus();
        if (mock) st.provider_name = "MockProvider";
        return {};
    } catch (const TransportError& e) {
        return e.what();
    }
}

std::string CaptureController::start(FrameCollector& collector) {
    if (!session_.isChannelOpen()) return "Channel not open.";
    FilterEngine filter; // pass-all
    // Register the GUI collector with the global capture service and start it.
    collector_ = &collector;
    // Ensure the global capture uses the same filter
    SetGlobalFilter(filter);
    AddGlobalSink(&collector);
    StartGlobalCapture(session_.channel(), session_.mutableStatus());
    paused_ = false;
    return {};
}

void CaptureController::pause() { paused_ = true; }
void CaptureController::resume() { paused_ = false; }

void CaptureController::stop() {
    if (collector_) {
        RemoveGlobalSink(collector_);
        collector_ = nullptr;
    }
    // GUI controls lifecycle; stop the global capture when GUI stops.
    StopGlobalCapture();
}

void CaptureController::disconnect() {
    stop();
    if (session_.isChannelOpen()) session_.closeChannel();
    if (session_.isConnected()) session_.disconnect();
}

bool CaptureController::is_connected() const {
    return session_.isConnected();
}

bool CaptureController::is_capturing() const {
    return IsGlobalCaptureRunning();
}

void CaptureController::drain() {
    if (!paused_ && IsGlobalCaptureRunning())
        DrainGlobalCapture();
}

const SessionStatus& CaptureController::status() const {
    return session_.status();
}

IChannel* CaptureController::channel() {
    return session_.isChannelOpen() ? session_.channel() : nullptr;
}

std::vector<std::string> CaptureController::scan_providers(bool mock) {
    try {
        if (mock)
            session_.setProvider(std::make_unique<MockProvider>());
        else
            session_.setProvider(std::make_unique<J2534Provider>());

        scanned_ = session_.scan();
        std::vector<std::string> names;
        names.reserve(scanned_.size());
        for (auto& d : scanned_)
            names.push_back(d.name);
        return names;
    } catch (...) {
        scanned_.clear();
        return {};
    }
}

} // namespace canmatik
