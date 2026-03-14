#include "mock/scriptable_mock_provider.h"

#include "core/log_macros.h"
#include <yaml-cpp/yaml.h>

namespace canmatik {

ScriptableMockProvider::ScriptableMockProvider(const std::string& yaml_path)
    : yaml_path_(yaml_path)
{
    rules_ = load_mock_rules(yaml_path_, &last_error_);

    // Read optional metadata
    try {
        auto root = YAML::LoadFile(yaml_path_);
        if (root["name"]) script_name_ = root["name"].as<std::string>();
    } catch (...) {}

    if (script_name_.empty())
        script_name_ = "Scriptable ECU";
}

std::vector<DeviceInfo> ScriptableMockProvider::enumerate() {
    DeviceInfo info;
    info.name = script_name_;
    info.vendor = "YAML Mock";
    info.dll_path = yaml_path_;
    info.supports_can = true;
    return { info };
}

std::unique_ptr<IChannel> ScriptableMockProvider::connect(const DeviceInfo& /*dev*/) {
    LOG_INFO("ScriptableMockProvider: creating channel with {} rules", rules_.size());
    return std::make_unique<ScriptableMockChannel>(rules_);
}

} // namespace canmatik
