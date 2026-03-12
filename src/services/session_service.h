#pragma once

/// @file session_service.h
/// SessionService — connect, disconnect, channel management, provider selection (T030 — US1).

#include "transport/device_provider.h"
#include "transport/channel.h"
#include "core/session_status.h"

#include <memory>
#include <string>

namespace canmatik {

/// Manages device connection lifecycle: provider selection, device connection,
/// channel open/close, and session status tracking.
class SessionService {
public:
    SessionService() = default;
    ~SessionService();

    // Non-copyable
    SessionService(const SessionService&) = delete;
    SessionService& operator=(const SessionService&) = delete;

    /// Set the device provider backend (J2534Provider, MockProvider, etc.).
    void setProvider(std::unique_ptr<IDeviceProvider> provider);

    /// Enumerate available devices via the current provider.
    /// @return List of discovered devices (empty if no provider set).
    [[nodiscard]] std::vector<DeviceInfo> scan();

    /// Connect to the specified device.
    /// @throws TransportError on failure.
    void connect(const DeviceInfo& dev);

    /// Disconnect from the current device and close any open channel.
    void disconnect();

    /// Open a CAN channel at the specified bitrate and bus protocol.
    /// @throws TransportError if no device connected or channel open fails.
    void openChannel(uint32_t bitrate, BusProtocol protocol = BusProtocol::CAN);

    /// Close the currently open CAN channel.
    void closeChannel();

    /// Get a raw pointer to the open channel (nullptr if none).
    [[nodiscard]] IChannel* channel();

    /// Get current session status (read-only).
    [[nodiscard]] const SessionStatus& status() const;

    /// Get mutable session status (for CaptureService to update counters).
    [[nodiscard]] SessionStatus& mutableStatus();

    /// Check if connected to a device.
    [[nodiscard]] bool isConnected() const { return channel_ != nullptr; }

    /// Check if a CAN channel is open.
    [[nodiscard]] bool isChannelOpen() const { return status_.channel_open; }

private:
    std::unique_ptr<IDeviceProvider> provider_;
    std::unique_ptr<IChannel> channel_;
    SessionStatus status_;
};

} // namespace canmatik
