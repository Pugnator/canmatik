#pragma once

/// @file dtc_decoder.h
/// Decode J1979 Mode $03/$07 DTC responses.

#include <cstdint>
#include <string>
#include <vector>

#include "core/result.h"

namespace canmatik {

enum class DtcCategory : uint8_t {
    Powertrain = 0, // P
    Chassis    = 1, // C
    Body       = 2, // B
    Network    = 3, // U
};

struct DtcCode {
    uint32_t ecu_id = 0;
    std::string code;       // e.g. "P0300"
    uint16_t raw = 0;
    DtcCategory category = DtcCategory::Powertrain;
    bool pending = false;   // true if from Mode $07
};

/// Decode a single DTC from 2 raw bytes.
[[nodiscard]] DtcCode decode_dtc(uint8_t byte0, uint8_t byte1, uint32_t ecu_id, bool pending);

/// Decode all DTCs from a Mode $03/$07 response data payload.
/// @param data Pointer to DTC data bytes (after mode byte in response).
/// @param length Number of data bytes (must be even).
/// @param ecu_id Source ECU arbitration ID.
/// @param pending True if Mode $07 (pending DTCs).
/// @return Vector of decoded DTCs, or error if byte count is odd.
[[nodiscard]] Result<std::vector<DtcCode>>
decode_dtcs(const uint8_t* data, size_t length, uint32_t ecu_id, bool pending);

} // namespace canmatik
