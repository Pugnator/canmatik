#pragma once

/// @file obd_response.h
/// Parse OBD-II responses from CAN frames.

#include <cstdint>
#include <string>
#include <vector>

#include "core/can_frame.h"
#include "core/result.h"

namespace canmatik {

/// Parsed single-frame OBD-II response.
struct ObdResponse {
    uint32_t rx_id = 0;
    uint8_t mode = 0;           // Response mode (request mode + 0x40)
    uint8_t pid = 0;
    uint8_t data[4]{};          // Data bytes A, B, C, D
    uint8_t data_length = 0;
    uint64_t timestamp_us = 0;
    CanFrame raw_frame;
    bool is_negative = false;
    uint8_t negative_code = 0;  // NRC if is_negative
};

/// Parse a single-frame OBD response from a CAN frame.
/// Validates that the frame is a valid OBD response for the given request mode/pid.
[[nodiscard]] Result<ObdResponse>
parse_obd_response(const CanFrame& frame, uint8_t expected_mode, uint8_t expected_pid);

/// Parse a single-frame OBD response without request validation (for broadcast).
[[nodiscard]] Result<ObdResponse>
parse_obd_response(const CanFrame& frame);

/// Assemble a multi-frame (ISO-TP) response from a sequence of CAN frames.
/// The first frame in the vector must be the First Frame; subsequent are Consecutive Frames.
/// Returns the reassembled payload (without PCI bytes).
[[nodiscard]] Result<std::vector<uint8_t>>
assemble_multiframe(const std::vector<CanFrame>& frames);

} // namespace canmatik
