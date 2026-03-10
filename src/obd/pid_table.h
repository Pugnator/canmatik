#pragma once

/// @file pid_table.h
/// J1979 Mode $01 PID definitions and formula types.

#include <cstdint>
#include <string>

namespace canmatik {

/// Decoding formula for converting raw OBD response bytes to engineering values.
struct PidFormula {
    enum class Type : uint8_t {
        Linear,       // value = (A * scale_a + B * scale_b + offset) / divisor
        BitEncoded,   // value = raw bitmask interpretation (returns raw uint)
        Enumerated,   // value = table lookup by raw byte (returns raw uint)
    };

    Type type = Type::Linear;
    double scale_a = 1.0;
    double scale_b = 0.0;
    double offset  = 0.0;
    double divisor = 1.0;
};

/// Static metadata for a single OBD-II PID.
struct PidDefinition {
    uint8_t mode = 0x01;
    uint8_t pid = 0x00;
    const char* name = "";
    const char* unit = "";
    uint8_t data_bytes = 1;
    PidFormula formula;
    double min_value = 0.0;
    double max_value = 0.0;
};

/// Lookup a PID definition by mode and PID number.
/// Returns nullptr if the PID is not in the built-in table.
[[nodiscard]] const PidDefinition* pid_lookup(uint8_t mode, uint8_t pid);

/// Get the total number of registered PIDs (for testing).
[[nodiscard]] size_t pid_table_size();

} // namespace canmatik
