/// @file pid_decoder.cpp
/// PID value decoding implementation.

#include "obd/pid_decoder.h"

namespace canmatik {

double decode_pid_value(const uint8_t* data, uint8_t length, const PidFormula& formula) {
    double a = (length >= 1) ? static_cast<double>(data[0]) : 0.0;
    double b = (length >= 2) ? static_cast<double>(data[1]) : 0.0;

    switch (formula.type) {
        case PidFormula::Type::Linear:
            return (a * formula.scale_a + b * formula.scale_b + formula.offset) / formula.divisor;

        case PidFormula::Type::BitEncoded:
        case PidFormula::Type::Enumerated: {
            // Return raw integer value (combine up to 4 bytes big-endian)
            uint32_t raw = 0;
            for (uint8_t i = 0; i < length && i < 4; ++i) {
                raw = (raw << 8) | data[i];
            }
            return static_cast<double>(raw);
        }
    }

    return 0.0; // unreachable
}

DecodedPid decode_pid(uint32_t ecu_id, const uint8_t* data, uint8_t length,
                       const PidDefinition& def, uint64_t timestamp_us) {
    DecodedPid result;
    result.ecu_id = ecu_id;
    result.mode = def.mode;
    result.pid = def.pid;
    result.name = def.name;
    result.unit = def.unit;
    result.value = decode_pid_value(data, length, def.formula);
    result.raw_bytes.assign(data, data + length);
    result.timestamp_us = timestamp_us;
    return result;
}

} // namespace canmatik
