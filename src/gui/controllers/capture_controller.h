#pragma once

/// @file capture_controller.h
/// Live capture lifecycle: connect, start, pause, stop, disconnect.

#include "gui/frame_collector.h"
#include "services/capture_service.h"
#include "services/session_service.h"

#include <memory>
#include <string>
#include <vector>

#include "transport/device_info.h"

namespace canmatik {

class CaptureController {
public:
    CaptureController() = default;

    /// Connect to a provider and open a channel.
    /// @return Empty on success, error message on failure.
    std::string connect(const std::string& provider_name, uint32_t bitrate,
                        bool mock, FrameCollector& collector,
                        BusProtocol protocol = BusProtocol::CAN);

    /// Start capturing frames into the collector.
    std::string start(FrameCollector& collector);

    /// Pause display (capture continues into buffer).
    void pause();

    /// Resume display after pause.
    void resume();

    /// Stop capture and close channel.
    void stop();

    /// Disconnect from device.
    void disconnect();

    bool is_connected() const;
    bool is_capturing() const;
    bool is_paused() const { return paused_; }

    /// Drain queued frames (call each GUI frame).
    void drain();

    /// Get session status.
    const SessionStatus& status() const;

    /// Scan for available J2534 providers. Returns device names.
    std::vector<std::string> scan_providers(bool mock);

    /// Get the last scanned provider list.
    const std::vector<DeviceInfo>& scanned_devices() const { return scanned_; }

    /// Get the underlying CAN channel (nullptr if not connected).
    IChannel* channel();

private:
    SessionService session_;
    CaptureService capture_;
    bool paused_ = false;
    std::vector<DeviceInfo> scanned_;
};

} // namespace canmatik
