#pragma once

/// @file can_frame.h
/// Core value types: CanFrame, FrameType, and validation helpers.

#include <cstdint>
#include <array>
#include <string>

namespace canmatik {

/// Bus protocol selector (CAN vs J1850).
enum class BusProtocol : uint8_t {
    CAN        = 0,  ///< Controller Area Network (classic / FD)
    J1850_VPW  = 1,  ///< SAE J1850 Variable Pulse Width (10.4 kbps, single-wire)
    J1850_PWM  = 2,  ///< SAE J1850 Pulse Width Modulation (41.6 kbps, dual-wire)
};

/// CAN frame type discriminator.
enum class FrameType : uint8_t {
    Standard  = 0,  ///< 11-bit arbitration ID, classic CAN
    Extended  = 1,  ///< 29-bit arbitration ID, classic CAN
    FD        = 2,  ///< CAN FD frame (future)
    Error     = 3,  ///< Error frame reported by adapter
    Remote    = 4,  ///< Remote Transmission Request
    J1850     = 5,  ///< J1850 VPW/PWM frame (3-byte header → 24-bit ID)
};

/// Maximum payload sizes.
inline constexpr uint8_t kClassicCanMaxDlc = 8;
inline constexpr uint8_t kCanFdMaxDlc      = 64;

/// Maximum valid arbitration IDs.
inline constexpr uint32_t kMaxStandardId = 0x7FFu;
inline constexpr uint32_t kMaxExtendedId = 0x1FFFFFFFu;
inline constexpr uint32_t kMaxJ1850Id   = 0xFFFFFFu;  ///< 24-bit (priority | target | source)

/// A single CAN frame as observed on the bus. Immutable once created.
struct CanFrame {
    uint64_t adapter_timestamp_us = 0;  ///< Adapter HW timestamp (µs, rollover-extended)
    uint64_t host_timestamp_us    = 0;  ///< Host steady_clock timestamp (µs)
    uint32_t arbitration_id       = 0;  ///< 11-bit or 29-bit CAN ID
    FrameType type                = FrameType::Standard;
    uint8_t dlc                   = 0;  ///< Data Length Code as observed
    std::array<uint8_t, 64> data  = {}; ///< Raw payload (first dlc bytes valid)
    uint8_t channel_id            = 0;  ///< Logical channel identifier
};

/// Validate that a CanFrame has consistent field values.
/// Returns an empty string on success, or a description of the first violation.
[[nodiscard]] std::string validate_frame(const CanFrame& frame);

/// Check whether an arbitration ID is valid for the given frame type.
[[nodiscard]] bool is_valid_id(uint32_t id, FrameType type);

/// Return a human-readable label for a FrameType value.
[[nodiscard]] const char* frame_type_to_string(FrameType type);

} // namespace canmatik
