#pragma once

/// @file timestamp.h
/// Timestamp helpers: rollover extension, steady_clock utilities.

#include <cstdint>
#include <chrono>

namespace canmatik {

/// Extend a 32-bit adapter timestamp to 64-bit, handling rollover.
///
/// J2534 PASSTHRU_MSG.Timestamp is `unsigned long` (32-bit), in microseconds.
/// It rolls over every ~4295 seconds (~71.6 minutes). This function tracks
/// rollovers and produces a monotonic 64-bit value.
///
/// @param raw_ts     Current 32-bit adapter timestamp (microseconds).
/// @param prev_ts    Previous 32-bit adapter timestamp.
/// @param rollover   Rollover counter (updated in-place on wrap detection).
/// @return           64-bit extended timestamp in microseconds.
inline uint64_t extend_timestamp(uint32_t raw_ts, uint32_t prev_ts,
                                 uint32_t& rollover) {
    if (raw_ts < prev_ts) {
        // Rollover detected
        ++rollover;
    }
    return static_cast<uint64_t>(rollover) * 0x1'0000'0000ULL +
           static_cast<uint64_t>(raw_ts);
}

/// Get the current host time as microseconds since steady_clock epoch.
inline uint64_t host_timestamp_us() {
    auto now = std::chrono::steady_clock::now();
    return static_cast<uint64_t>(
        std::chrono::duration_cast<std::chrono::microseconds>(
            now.time_since_epoch()
        ).count()
    );
}

/// Compute session-relative seconds from two absolute microsecond timestamps.
inline double session_relative_seconds(uint64_t frame_us, uint64_t session_start_us) {
    if (frame_us < session_start_us) return 0.0;
    return static_cast<double>(frame_us - session_start_us) / 1'000'000.0;
}

} // namespace canmatik
