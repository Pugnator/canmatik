/// @file obd_config.cpp
/// ObdConfig YAML loader and default config generator.

#include "obd/obd_config.h"

#include <fstream>
#include <sstream>

#include <yaml-cpp/yaml.h>

namespace canmatik {

Result<ObdConfig> ObdConfig::load(const std::string& path) {
    YAML::Node root;
    try {
        root = YAML::LoadFile(path);
    } catch (const YAML::Exception& e) {
        return Result<ObdConfig>::error("failed to load OBD config '" + path + "': " + e.what());
    }

    ObdConfig cfg;

    // Default interval
    if (root["interval"]) {
        auto iv = parse_interval(root["interval"].as<std::string>());
        if (!iv) return Result<ObdConfig>::error("config 'interval': " + iv.error());
        cfg.default_interval = *iv;
    }

    // Addressing
    if (auto addr = root["addressing"]) {
        if (addr["mode"]) {
            std::string mode = addr["mode"].as<std::string>();
            if (mode == "functional") {
                cfg.addressing_mode = AddressingMode::Functional;
            } else if (mode == "physical") {
                cfg.addressing_mode = AddressingMode::Physical;
            } else {
                return Result<ObdConfig>::error("config 'addressing.mode': unknown mode '" + mode
                                       + "' (expected 'functional' or 'physical')");
            }
        }
        if (addr["tx_id"]) {
            cfg.tx_id = addr["tx_id"].as<uint32_t>();
        }
        if (addr["rx_base"]) {
            cfg.rx_base = addr["rx_base"].as<uint32_t>();
        }
    }

    // Groups
    if (root["groups"]) {
        for (const auto& g : root["groups"]) {
            QueryGroup group;
            if (g["name"]) {
                group.name = g["name"].as<std::string>();
            } else {
                return Result<ObdConfig>::error("config 'groups': each group must have a 'name'");
            }

            if (g["interval"]) {
                auto iv = parse_interval(g["interval"].as<std::string>());
                if (!iv) {
                    return Result<ObdConfig>::error("config group '" + group.name
                                           + "' interval: " + iv.error());
                }
                group.interval = *iv;
                group.has_interval = true;
            }

            if (g["pids"]) {
                for (const auto& p : g["pids"]) {
                    if (p["id"]) {
                        group.pids.push_back(static_cast<uint8_t>(p["id"].as<unsigned>()));
                    } else {
                        // Allow plain integer list too
                        group.pids.push_back(static_cast<uint8_t>(p.as<unsigned>()));
                    }
                }
            }

            if (group.pids.empty()) {
                return Result<ObdConfig>::error("config group '" + group.name + "' has no PIDs");
            }

            cfg.groups.push_back(std::move(group));
        }
    }

    // Standalone PIDs
    if (root["standalone_pids"]) {
        for (const auto& p : root["standalone_pids"]) {
            cfg.standalone_pids.push_back(static_cast<uint8_t>(p.as<unsigned>()));
        }
    }

    return cfg;
}

Result<void> ObdConfig::generate_default(const std::string& path) {
    static constexpr const char* kDefaultYaml = R"(# CANmatik OBD-II default configuration
# Generated automatically - edit to customize PID selection

interval: "1s"

addressing:
  mode: functional
  tx_id: 0x7DF
  rx_base: 0x7E8

groups:
  - name: engine_basics
    pids:
      - id: 0x0C   # Engine RPM
      - id: 0x0D   # Vehicle Speed
      - id: 0x05   # Coolant Temperature
      - id: 0x04   # Calculated Engine Load
      - id: 0x11   # Throttle Position

  - name: fuel_system
    interval: "2s"
    pids:
      - id: 0x06   # Short Term Fuel Trim (Bank 1)
      - id: 0x07   # Long Term Fuel Trim (Bank 1)
      - id: 0x10   # MAF Air Flow Rate
      - id: 0x0F   # Intake Air Temperature
)";

    std::ofstream out(path);
    if (!out) {
        return Result<void>::error("cannot create default config file: " + path);
    }
    out << kDefaultYaml;
    if (!out) {
        return Result<void>::error("error writing default config file: " + path);
    }
    return {};
}

} // namespace canmatik
