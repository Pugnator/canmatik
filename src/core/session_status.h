#pragma once

/// @file session_status.h
/// OperatingMode enum and SessionStatus aggregate.

#include <cstdint>
#include <string>
#include <vector>
#include <chrono>
#include <atomic>

#include "core/filter.h"

namespace canmatik {

/// Determines what operations are permitted on the channel.
enum class OperatingMode : uint8_t {
    Passive      = 0, ///< Receive only (default, MVP)
    ActiveQuery  = 1, ///< Diagnostic request/response (future)
    ActiveInject = 2, ///< Arbitrary frame transmission (future)
};

/// Human-readable label for an operating mode.
[[nodiscard]] const char* operating_mode_to_string(OperatingMode mode);

/// Aggregate runtime state. Updated by CaptureService.
struct SessionStatus {
    OperatingMode mode = OperatingMode::Passive;
    std::string provider_name;
    std::string adapter_name;
    uint32_t bitrate      = 0;
    bool channel_open     = false;
    bool recording        = false;
    std::string recording_file;
    uint64_t frames_received    = 0;
    uint64_t frames_transmitted = 0;
    uint64_t errors             = 0;
    uint64_t dropped            = 0;
    std::chrono::steady_clock::time_point session_start;
    std::vector<FilterRule> active_filters;

    /// Reset all counters and state for a new session.
    void reset();

    /// Compute elapsed session duration.
    [[nodiscard]] double elapsed_seconds() const;
};

} // namespace canmatik
