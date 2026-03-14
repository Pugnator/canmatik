#pragma once

/// @file scriptable_mock_provider.h
/// ScriptableMockProvider : IDeviceProvider — loads a YAML ECU script and
/// creates ScriptableMockChannels that respond according to the rule set.

#include "transport/device_provider.h"
#include "transport/device_info.h"
#include "mock/scriptable_mock_channel.h"

#include <string>
#include <vector>
#include <memory>

namespace canmatik {

class ScriptableMockProvider : public IDeviceProvider {
public:
    /// Load rules from a YAML file. Check last_error() if enumerate() is empty.
    explicit ScriptableMockProvider(const std::string& yaml_path);

    std::vector<DeviceInfo> enumerate() override;
    std::unique_ptr<IChannel> connect(const DeviceInfo& dev) override;

    [[nodiscard]] const std::string& last_error() const { return last_error_; }
    [[nodiscard]] size_t rule_count() const { return rules_.size(); }

private:
    std::string yaml_path_;
    std::string script_name_;
    std::vector<MockRule> rules_;
    std::string last_error_;
};

} // namespace canmatik
