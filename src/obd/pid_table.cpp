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

    // PID $01: Monitor status since DTCs cleared — 4-byte bitmask
    PidDefinition{0x01, 0x01, "Monitor Status Since DTCs Cleared", "", 4, bit_encoded(), 0, 0},

    // PID $03: Fuel system status — 2-byte bitmask
    PidDefinition{0x01, 0x03, "Fuel System Status", "", 2, bit_encoded(), 0, 0},

    // PID $04: Calculated engine load — A * 100 / 255
    PidDefinition{0x01, 0x04, "Calculated Engine Load", "%", 1,
                  linear(100.0, 0.0, 0.0, 255.0), 0, 100},

    // PID $05: Engine coolant temperature — A - 40
    PidDefinition{0x01, 0x05, "Coolant Temperature", "\xC2\xB0""C", 1,
                  linear(1.0, 0.0, -40.0), -40, 215},

    // PID $06: Short term fuel trim — Bank 1
    PidDefinition{0x01, 0x06, "Short Term Fuel Trim (Bank 1)", "%", 1,
                  linear(100.0, 0.0, -12800.0, 128.0), -100, 99.2},

    // PID $07: Long term fuel trim — Bank 1
    PidDefinition{0x01, 0x07, "Long Term Fuel Trim (Bank 1)", "%", 1,
                  linear(100.0, 0.0, -12800.0, 128.0), -100, 99.2},

    // PID $08: Short term fuel trim — Bank 2
    PidDefinition{0x01, 0x08, "Short Term Fuel Trim (Bank 2)", "%", 1,
                  linear(100.0, 0.0, -12800.0, 128.0), -100, 99.2},

    // PID $09: Long term fuel trim — Bank 2
    PidDefinition{0x01, 0x09, "Long Term Fuel Trim (Bank 2)", "%", 1,
                  linear(100.0, 0.0, -12800.0, 128.0), -100, 99.2},

    // PID $0A: Fuel pressure — A * 3
    PidDefinition{0x01, 0x0A, "Fuel Pressure", "kPa", 1,
                  linear(3.0), 0, 765},

    // PID $0B: Intake manifold absolute pressure — A
    PidDefinition{0x01, 0x0B, "Intake Manifold Pressure", "kPa", 1,
                  linear(1.0), 0, 255},

    // PID $0C: Engine RPM — (256*A + B) / 4
    PidDefinition{0x01, 0x0C, "Engine RPM", "rpm", 2,
                  linear(256.0, 1.0, 0.0, 4.0), 0, 16383.75},

    // PID $0D: Vehicle speed — A
    PidDefinition{0x01, 0x0D, "Vehicle Speed", "km/h", 1,
                  linear(1.0), 0, 255},

    // PID $0E: Timing advance — A / 2 - 64
    PidDefinition{0x01, 0x0E, "Timing Advance", "\xC2\xB0", 1,
                  linear(1.0, 0.0, -128.0, 2.0), -64, 63.5},

    // PID $0F: Intake air temperature — A - 40
    PidDefinition{0x01, 0x0F, "Intake Air Temperature", "\xC2\xB0""C", 1,
                  linear(1.0, 0.0, -40.0), -40, 215},

    // PID $10: MAF air flow rate — (256*A + B) / 100
    PidDefinition{0x01, 0x10, "MAF Air Flow Rate", "g/s", 2,
                  linear(256.0, 1.0, 0.0, 100.0), 0, 655.35},

    // PID $11: Throttle position — A * 100/255
    PidDefinition{0x01, 0x11, "Throttle Position", "%", 1,
                  linear(100.0, 0.0, 0.0, 255.0), 0, 100},

    // PID $12: Commanded secondary air status — enumerated
    PidDefinition{0x01, 0x12, "Commanded Secondary Air Status", "", 1,
                  enumerated(), 0, 255},

    // PID $13: O2 sensors present (in 2 banks) — bitmask
    PidDefinition{0x01, 0x13, "O2 Sensors Present (2 Banks)", "", 1, bit_encoded(), 0, 0},

    // PID $14: O2 Sensor Bank 1 Sensor 1 — voltage = A / 200
    PidDefinition{0x01, 0x14, "O2 Voltage (Bank 1, Sensor 1)", "V", 2,
                  linear(1.0, 0.0, 0.0, 200.0), 0, 1.275},

    // PID $15: O2 Sensor Bank 1 Sensor 2
    PidDefinition{0x01, 0x15, "O2 Voltage (Bank 1, Sensor 2)", "V", 2,
                  linear(1.0, 0.0, 0.0, 200.0), 0, 1.275},

    // PID $16: O2 Sensor Bank 1 Sensor 3
    PidDefinition{0x01, 0x16, "O2 Voltage (Bank 1, Sensor 3)", "V", 2,
                  linear(1.0, 0.0, 0.0, 200.0), 0, 1.275},

    // PID $17: O2 Sensor Bank 1 Sensor 4
    PidDefinition{0x01, 0x17, "O2 Voltage (Bank 1, Sensor 4)", "V", 2,
                  linear(1.0, 0.0, 0.0, 200.0), 0, 1.275},

    // PID $18: O2 Sensor Bank 2 Sensor 1
    PidDefinition{0x01, 0x18, "O2 Voltage (Bank 2, Sensor 1)", "V", 2,
                  linear(1.0, 0.0, 0.0, 200.0), 0, 1.275},

    // PID $19: O2 Sensor Bank 2 Sensor 2
    PidDefinition{0x01, 0x19, "O2 Voltage (Bank 2, Sensor 2)", "V", 2,
                  linear(1.0, 0.0, 0.0, 200.0), 0, 1.275},

    // PID $1A: O2 Sensor Bank 2 Sensor 3
    PidDefinition{0x01, 0x1A, "O2 Voltage (Bank 2, Sensor 3)", "V", 2,
                  linear(1.0, 0.0, 0.0, 200.0), 0, 1.275},

    // PID $1B: O2 Sensor Bank 2 Sensor 4
    PidDefinition{0x01, 0x1B, "O2 Voltage (Bank 2, Sensor 4)", "V", 2,
                  linear(1.0, 0.0, 0.0, 200.0), 0, 1.275},

    // PID $1C: OBD standards this vehicle conforms to
    PidDefinition{0x01, 0x1C, "OBD Standard", "", 1,
                  enumerated(), 0, 255},

    // PID $1F: Run time since engine start — 256*A + B
    PidDefinition{0x01, 0x1F, "Runtime Since Engine Start", "s", 2,
                  linear(256.0, 1.0), 0, 65535},

    // PID $20: Supported PIDs [21-40]
    PidDefinition{0x01, 0x20, "Supported PIDs [21-40]", "", 4, bit_encoded(), 0, 0},

    // PID $21: Distance traveled with MIL on — 256*A + B
    PidDefinition{0x01, 0x21, "Distance Traveled With MIL On", "km", 2,
                  linear(256.0, 1.0), 0, 65535},

    // PID $2E: Evap system vapor pressure — (256*A + B) / 4 (signed)
    PidDefinition{0x01, 0x2E, "Evap System Vapor Pressure", "Pa", 2,
                  linear(256.0, 1.0, 0.0, 4.0), -8192, 8191.75},

    // PID $2F: Fuel tank level input — A * 100/255
    PidDefinition{0x01, 0x2F, "Fuel Tank Level", "%", 1,
                  linear(100.0, 0.0, 0.0, 255.0), 0, 100},

    // PID $30: Warm-ups since codes cleared — A
    PidDefinition{0x01, 0x30, "Warm-ups Since Codes Cleared", "", 1,
                  linear(1.0), 0, 255},

    // PID $31: Distance traveled since codes cleared — 256*A + B
    PidDefinition{0x01, 0x31, "Distance Since Codes Cleared", "km", 2,
                  linear(256.0, 1.0), 0, 65535},

    // PID $33: Absolute barometric pressure — A
    PidDefinition{0x01, 0x33, "Barometric Pressure", "kPa", 1,
                  linear(1.0), 0, 255},

    // PID $40: Supported PIDs [41-60]
    PidDefinition{0x01, 0x40, "Supported PIDs [41-60]", "", 4, bit_encoded(), 0, 0},

    // PID $42: Control module voltage — (256*A + B) / 1000
    PidDefinition{0x01, 0x42, "Control Module Voltage", "V", 2,
                  linear(256.0, 1.0, 0.0, 1000.0), 0, 65.535},

    // PID $43: Absolute load value — (256*A + B) * 100 / 255
    PidDefinition{0x01, 0x43, "Absolute Load Value", "%", 2,
                  linear(25600.0, 100.0, 0.0, 255.0), 0, 25700.4},

    // PID $44: Commanded air-fuel equivalence ratio — (256*A + B) / 32768
    PidDefinition{0x01, 0x44, "Commanded Air-Fuel Ratio", "", 2,
                  linear(256.0, 1.0, 0.0, 32768.0), 0, 2.0},

    // PID $45: Relative throttle position — A * 100/255
    PidDefinition{0x01, 0x45, "Relative Throttle Position", "%", 1,
                  linear(100.0, 0.0, 0.0, 255.0), 0, 100},

    // PID $46: Ambient air temperature — A - 40
    PidDefinition{0x01, 0x46, "Ambient Air Temperature", "\xC2\xB0""C", 1,
                  linear(1.0, 0.0, -40.0), -40, 215},

    // PID $47: Absolute throttle position B — A * 100/255
    PidDefinition{0x01, 0x47, "Absolute Throttle Position B", "%", 1,
                  linear(100.0, 0.0, 0.0, 255.0), 0, 100},

    // PID $49: Accelerator pedal position D — A * 100/255
    PidDefinition{0x01, 0x49, "Accelerator Pedal Position D", "%", 1,
                  linear(100.0, 0.0, 0.0, 255.0), 0, 100},

    // PID $4A: Accelerator pedal position E — A * 100/255
    PidDefinition{0x01, 0x4A, "Accelerator Pedal Position E", "%", 1,
                  linear(100.0, 0.0, 0.0, 255.0), 0, 100},

    // PID $4C: Commanded throttle actuator — A * 100/255
    PidDefinition{0x01, 0x4C, "Commanded Throttle Actuator", "%", 1,
                  linear(100.0, 0.0, 0.0, 255.0), 0, 100},

    // PID $51: Fuel type — enumerated
    PidDefinition{0x01, 0x51, "Fuel Type", "", 1,
                  enumerated(), 0, 23},

    // PID $5C: Engine oil temperature — A - 40
    PidDefinition{0x01, 0x5C, "Engine Oil Temperature", "\xC2\xB0""C", 1,
                  linear(1.0, 0.0, -40.0), -40, 215},

    // PID $60: Supported PIDs [61-80]
    PidDefinition{0x01, 0x60, "Supported PIDs [61-80]", "", 4, bit_encoded(), 0, 0},
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
