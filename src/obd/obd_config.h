#pragma once

/// @file obd_config.h
/// OBD-II YAML configuration loader and default config generator.

#include <cstdint>
#include <string>
#include <vector>

#include "core/result.h"
#include "obd/interval_spec.h"

namespace canmatik {

enum class AddressingMode : uint8_t {
    Functional, // Broadcast to 0x7DF
    Physical,   // Target single ECU 0x7E0–0x7E7
};

struct QueryGroup {
    std::string name;
    IntervalSpec interval;           // Group-specific interval (0 = use default)
    bool has_interval = false;       // Whether group specifies its own interval
    std::vector<uint8_t> pids;
};

struct ObdConfig {
    IntervalSpec default_interval{1000};
    AddressingMode addressing_mode = AddressingMode::Functional;
    uint32_t tx_id = 0x7DF;
    uint32_t rx_base = 0x7E8;
    std::vector<QueryGroup> groups;
    std::vector<uint8_t> standalone_pids;

    /// Load config from a YAML file.
    [[nodiscard]] static Result<ObdConfig> load(const std::string& path);

    /// Generate a default config file at the given path.
    [[nodiscard]] static Result<void> generate_default(const std::string& path);
};

} // namespace canmatik
