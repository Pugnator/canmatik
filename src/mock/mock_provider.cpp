/// @file mock_provider.cpp
/// MockProvider implementation.

#include "mock/mock_provider.h"
#include "mock/mock_channel.h"
#include "transport/transport_error.h"

namespace canmatik {

MockProvider::MockProvider() {
    devices_.push_back(DeviceInfo{
        .name = "Mock Adapter",
        .vendor = "CANmatik",
        .dll_path = "(mock)",
        .supports_can = true,
        .supports_iso15765 = true,
    });
}

MockProvider::MockProvider(std::vector<DeviceInfo> devices)
    : devices_(std::move(devices))
{}

std::vector<DeviceInfo> MockProvider::enumerate() {
    return devices_;
}

std::unique_ptr<IChannel> MockProvider::connect(const DeviceInfo& /*dev*/) {
    if (fail_connect_) {
        throw TransportError(0, "Mock: simulated connection failure",
                             "MockProvider::connect", false);
    }
    return std::make_unique<MockChannel>(frame_rate_);
}

} // namespace canmatik
