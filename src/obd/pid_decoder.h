#pragma once

/// @file pid_decoder.h
/// Decode raw OBD response bytes into engineering values using PidFormula.

#include <cstdint>
#include <string>
#include <vector>

#include "obd/pid_table.h"

namespace canmatik {

/// A fully decoded PID value.
struct DecodedPid {
    uint32_t ecu_id = 0;
    uint8_t mode = 0;
    uint8_t pid = 0;
    std::string name;
    double value = 0.0;
    std::string unit;
    std::vector<uint8_t> raw_bytes;
    uint64_t timestamp_us = 0;
};

/// Decode raw data bytes using a PID formula.
/// @param data Pointer to raw bytes (A, B, C, D).
/// @param length Number of data bytes (1–4).
/// @param formula The decoding formula to apply.
/// @return Decoded value as double.
[[nodiscard]] double decode_pid_value(const uint8_t* data, uint8_t length,
                                      const PidFormula& formula);

/// Build a full DecodedPid from response data and a PidDefinition.
[[nodiscard]] DecodedPid decode_pid(uint32_t ecu_id, const uint8_t* data, uint8_t length,
                                     const PidDefinition& def, uint64_t timestamp_us);

} // namespace canmatik
