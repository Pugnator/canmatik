#pragma once

/// @file interval_spec.h
/// Parse human-readable interval strings ("500ms", "1s", "2hz") to milliseconds.

#include <cstdint>
#include <string>

#include "core/result.h"

namespace canmatik {

/// Parsed interval value in milliseconds.
struct IntervalSpec {
    uint32_t milliseconds = 1000;

    static constexpr uint32_t kMinMs = 10;
    static constexpr uint32_t kMaxMs = 60000;
};

/// Parse an interval string. Returns IntervalSpec on success, error message on failure.
/// Supported formats: "500ms", "1s", "2.5s", "2hz", "0.5hz"
/// Case-insensitive. Range: 10ms–60000ms.
[[nodiscard]] Result<IntervalSpec> parse_interval(const std::string& input);

} // namespace canmatik
