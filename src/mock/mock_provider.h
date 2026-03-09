#pragma once

/// @file mock_provider.h
/// MockProvider : IDeviceProvider — simulated backend for testing and demo mode.

#include "transport/device_provider.h"
#include "transport/device_info.h"
#include "transport/channel.h"

#include <vector>
#include <memory>
#include <cstdint>

namespace canmatik {

/// Mock device provider for testing and demo mode.
/// Returns a configurable list of fake DeviceInfo and creates MockChannels.
class MockProvider : public IDeviceProvider {
public:
    /// Construct with default mock device info.
    MockProvider();

    /// Construct with a custom list of mock devices.
    explicit MockProvider(std::vector<DeviceInfo> devices);

    /// Return the configured mock device list.
    std::vector<DeviceInfo> enumerate() override;

    /// Create a MockChannel connected to the given device.
    std::unique_ptr<IChannel> connect(const DeviceInfo& dev) override;

    /// Configure whether connect() should throw (simulates connection failure).
    void set_fail_connect(bool fail) { fail_connect_ = fail; }

private:
    std::vector<DeviceInfo> devices_;
    bool fail_connect_ = false;
    uint32_t frame_rate_ = 100;  ///< Default mock frame rate
};

} // namespace canmatik
