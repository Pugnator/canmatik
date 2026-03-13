#pragma once

#include <string>
#include <vector>

#include "transport/device_provider.h"

namespace canmatik {

class SerialProvider : public IDeviceProvider {
public:
    SerialProvider() = default;
    ~SerialProvider() override = default;

    std::vector<DeviceInfo> enumerate() override;
    std::unique_ptr<IChannel> connect(const DeviceInfo& dev) override;
};

} // namespace canmatik
