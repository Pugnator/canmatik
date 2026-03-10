/// @file pid_table.cpp
/// Built-in J1979 Mode $01 PID table.

#include "obd/pid_table.h"

#include <array>

namespace canmatik {

namespace {

// Helper to build linear formulas concisely
constexpr PidFormula linear(double sa, double sb = 0.0, double off = 0.0, double div = 1.0) {
    return {PidFormula::Type::Linear, sa, sb, off, div};
}

constexpr PidFormula bit_encoded() {
    return {PidFormula::Type::BitEncoded, 1.0, 0.0, 0.0, 1.0};
}

constexpr PidFormula enumerated() {
    return {PidFormula::Type::Enumerated, 1.0, 0.0, 0.0, 1.0};
}

// Built-in Mode $01 PID definitions (J1979)
constexpr std::array kPidTable = {
    // PID $00: Supported PIDs [01-20] — 4-byte bitmask
    PidDefinition{0x01, 0x00, "Supported PIDs [01-20]", "", 4, bit_encoded(), 0, 0},

    // PID $04: Calculated engine load
    PidDefinition{0x01, 0x04, "Calculated Engine Load", "%", 1,
                  linear(100.0, 0.0, 0.0, 255.0), 0, 100},

    // PID $05: Engine coolant temperature
    PidDefinition{0x01, 0x05, "Coolant Temperature", "\xC2\xB0""C", 1,
                  linear(1.0, 0.0, -40.0), -40, 215},

    // PID $06: Short term fuel trim — Bank 1
    // Formula: (A / 1.28) - 100 = (100*A - 12800) / 128
    PidDefinition{0x01, 0x06, "Short Term Fuel Trim (Bank 1)", "%", 1,
                  linear(100.0, 0.0, -12800.0, 128.0), -100, 99.2},

    // PID $07: Long term fuel trim — Bank 1
    PidDefinition{0x01, 0x07, "Long Term Fuel Trim (Bank 1)", "%", 1,
                  linear(100.0, 0.0, -12800.0, 128.0), -100, 99.2},

    // PID $0C: Engine RPM — (256*A + B) / 4
    PidDefinition{0x01, 0x0C, "Engine RPM", "rpm", 2,
                  linear(256.0, 1.0, 0.0, 4.0), 0, 16383.75},

    // PID $0D: Vehicle speed — A
    PidDefinition{0x01, 0x0D, "Vehicle Speed", "km/h", 1,
                  linear(1.0), 0, 255},

    // PID $0F: Intake air temperature
    PidDefinition{0x01, 0x0F, "Intake Air Temperature", "\xC2\xB0""C", 1,
                  linear(1.0, 0.0, -40.0), -40, 215},

    // PID $10: MAF air flow rate — (256*A + B) / 100
    PidDefinition{0x01, 0x10, "MAF Air Flow Rate", "g/s", 2,
                  linear(256.0, 1.0, 0.0, 100.0), 0, 655.35},

    // PID $11: Throttle position — A * 100/255
    PidDefinition{0x01, 0x11, "Throttle Position", "%", 1,
                  linear(100.0, 0.0, 0.0, 255.0), 0, 100},

    // PID $1C: OBD standards this vehicle conforms to
    PidDefinition{0x01, 0x1C, "OBD Standard", "", 1,
                  enumerated(), 0, 255},

    // PID $1F: Run time since engine start — 256*A + B
    PidDefinition{0x01, 0x1F, "Runtime Since Engine Start", "s", 2,
                  linear(256.0, 1.0), 0, 65535},

    // PID $20: Supported PIDs [21-40]
    PidDefinition{0x01, 0x20, "Supported PIDs [21-40]", "", 4, bit_encoded(), 0, 0},

    // PID $2F: Fuel tank level input — A * 100/255
    PidDefinition{0x01, 0x2F, "Fuel Tank Level", "%", 1,
                  linear(100.0, 0.0, 0.0, 255.0), 0, 100},

    // PID $33: Absolute barometric pressure
    PidDefinition{0x01, 0x33, "Barometric Pressure", "kPa", 1,
                  linear(1.0), 0, 255},

    // PID $40: Supported PIDs [41-60]
    PidDefinition{0x01, 0x40, "Supported PIDs [41-60]", "", 4, bit_encoded(), 0, 0},

    // PID $42: Control module voltage — (256*A + B) / 1000
    PidDefinition{0x01, 0x42, "Control Module Voltage", "V", 2,
                  linear(256.0, 1.0, 0.0, 1000.0), 0, 65.535},

    // PID $46: Ambient air temperature
    PidDefinition{0x01, 0x46, "Ambient Air Temperature", "\xC2\xB0""C", 1,
                  linear(1.0, 0.0, -40.0), -40, 215},

    // PID $51: Fuel type
    PidDefinition{0x01, 0x51, "Fuel Type", "", 1,
                  enumerated(), 0, 23},
};

} // anonymous namespace

const PidDefinition* pid_lookup(uint8_t mode, uint8_t pid) {
    for (const auto& def : kPidTable) {
        if (def.mode == mode && def.pid == pid) {
            return &def;
        }
    }
    return nullptr;
}

size_t pid_table_size() {
    return kPidTable.size();
}

} // namespace canmatik
