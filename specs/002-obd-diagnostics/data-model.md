# Data Model: OBD-II Diagnostics

**Feature Branch**: `002-obd-diagnostics`  
**Created**: 2026-03-10  
**Input**: spec.md, J1979 standard, ISO 15765-4

---

## Entity Overview

```
                          YAML Config (obd.yaml)
                                │
                          ObdConfig (yaml-cpp)
                                │
              ┌─────────────────┼─────────────────┐
              ▼                 ▼                 ▼
        PidDefinition     QueryGroup        IntervalSpec
              │                │
              │    ┌───────────┘
              ▼    ▼
         QueryScheduler ──────▶ ObdRequest
              │                     │
              │               ISO 15765-4
              │                     │
              ▼                     ▼
         IChannel::write()    IChannel::read()
                                    │
                                    ▼
                              ObdResponse
                                    │
                        ┌───────────┼───────────┐
                        ▼           ▼           ▼
                  PidDecoder   DtcDecoder   VinDecoder
                        │           │           │
                        ▼           ▼           ▼
                  DecodedPid     DtcCode    VehicleInfo
                        │           │           │
                        └───────────┼───────────┘
                                    ▼
                              ObdResult
                                    │
                     ┌──────────────┼──────────────┐
                     ▼              ▼              ▼
               CLI display    RecordService   ICaptureSync
```

---

## Entities

### PidDefinition

Static metadata for a single OBD-II PID. Loaded from embedded table (compile-time) or YAML override.

| Field | Type | Constraints | Description |
|-------|------|-------------|-------------|
| `mode` | `uint8_t` | 0x01–0x09 | J1979 service/mode number |
| `pid` | `uint8_t` | 0x00–0xFF | Parameter ID within the mode |
| `name` | `std::string` | Non-empty | Human-readable name (e.g., "Engine RPM") |
| `unit` | `std::string` | May be empty | Unit of measure (e.g., "rpm", "°C", "km/h") |
| `data_bytes` | `uint8_t` | 1–4 | Number of data bytes in response (A, B, C, D) |
| `formula` | `PidFormula` | Valid variant | Decoding formula variant |
| `min_value` | `double` | | Expected minimum after decoding |
| `max_value` | `double` | | Expected maximum after decoding |

**Invariants**:
- `data_bytes` must match the number of bytes the formula references
- `min_value < max_value`

---

### PidFormula

Decoding formula for converting raw bytes to engineering values. Covers the common J1979 patterns.

```cpp
/// Discriminated union of PID decode formulas.
/// A, B, C, D are raw response bytes (indices 0–3 after mode+pid header).
struct PidFormula {
    enum class Type {
        Linear,       // value = (A * scale_a + B * scale_b) + offset
        BitEncoded,   // value = raw bitmask interpretation
        Enumerated,   // value = table lookup by raw byte
        String,       // value = ASCII string (VIN, Cal ID)
        Custom,       // value = custom lambda (rare edge cases)
    };

    Type type = Type::Linear;
    double scale_a = 1.0;    // multiplier for byte A
    double scale_b = 0.0;    // multiplier for byte B (256*A + B patterns)
    double offset  = 0.0;    // additive offset
    double divisor = 1.0;    // final divisor
};
```

**Common patterns**:
- RPM (0x0C): `(256*A + B) / 4` → `scale_a=256, scale_b=1, divisor=4`
- Coolant (0x05): `A - 40` → `scale_a=1, offset=-40`
- Speed (0x0D): `A` → `scale_a=1`
- Throttle (0x11): `A * 100/255` → `scale_a=100, divisor=255`

---

### ObdRequest

A single OBD-II request to be sent on the CAN bus.

| Field | Type | Constraints | Description |
|-------|------|-------------|-------------|
| `tx_id` | `uint32_t` | 0x7DF (functional) or 0x7E0–0x7E7 (physical) | CAN arbitration ID for request |
| `mode` | `uint8_t` | 0x01–0x09 | J1979 service mode |
| `pid` | `uint8_t` | 0x00–0xFF | PID within the mode |
| `timestamp_us` | `uint64_t` | Monotonic | Host timestamp when request was sent |

**CAN frame encoding** (ISO 15765-4 single frame, 11-bit CAN):
```
Data[0] = 0x02          (ISO-TP single frame, 2 payload bytes)
Data[1] = mode          (e.g., 0x01 for current data)
Data[2] = pid           (e.g., 0x0C for RPM)
Data[3..7] = 0x55       (padding per ISO 15765-4 §6.3.1)
```

---

### ObdResponse

A parsed OBD-II response from an ECU.

| Field | Type | Constraints | Description |
|-------|------|-------------|-------------|
| `rx_id` | `uint32_t` | 0x7E8–0x7EF | Responding ECU's CAN arbitration ID |
| `mode` | `uint8_t` | = request mode + 0x40 | Positive response mode |
| `pid` | `uint8_t` | = request PID | Echoed PID |
| `data` | `uint8_t[4]` | Up to 4 bytes (A, B, C, D) | Raw response data bytes |
| `data_length` | `uint8_t` | 1–4 | Number of valid data bytes |
| `timestamp_us` | `uint64_t` | Monotonic | Host timestamp when response was received |
| `raw_frame` | `CanFrame` | | Original CAN frame (preserved for logging) |

**Validation**:
- Response mode = request mode + 0x40
- Response PID = request PID
- `rx_id` is in the expected range for the addressing mode

---

### DecodedPid

A fully decoded PID value ready for display or recording.

| Field | Type | Constraints | Description |
|-------|------|-------------|-------------|
| `ecu_id` | `uint32_t` | 0x7E8–0x7EF | Source ECU address |
| `mode` | `uint8_t` | | J1979 mode |
| `pid` | `uint8_t` | | PID number |
| `name` | `std::string` | | Human-readable PID name |
| `value` | `double` | | Decoded engineering value |
| `unit` | `std::string` | | Unit string |
| `raw_bytes` | `std::vector<uint8_t>` | 1–4 elements | Raw A/B/C/D bytes |
| `timestamp_us` | `uint64_t` | | Response timestamp |

---

### DtcCode

A decoded diagnostic trouble code.

| Field | Type | Constraints | Description |
|-------|------|-------------|-------------|
| `ecu_id` | `uint32_t` | 0x7E8–0x7EF | Source ECU |
| `code` | `std::string` | "P0000"–"U3FFF" | Standard DTC string |
| `raw` | `uint16_t` | 2 bytes from response | Raw DTC value |
| `category` | `DtcCategory` | Powertrain/Chassis/Body/Network | DTC category from leading bits |
| `pending` | `bool` | | True if from Mode $07 (pending) |

**DTC encoding** (2 bytes → 5-char code):
```
Byte 0: [7:6] = category (P/C/B/U)
         [5:4] = second digit
         [3:0] = third digit
Byte 1: [7:4] = fourth digit
         [3:0] = fifth digit
```

---

### DtcCategory

| Value | Prefix | Meaning |
|-------|--------|---------|
| 0b00 | P | Powertrain |
| 0b01 | C | Chassis |
| 0b10 | B | Body |
| 0b11 | U | Network |

---

### VehicleInfo

Mode $09 vehicle information response.

| Field | Type | Description |
|-------|------|-------------|
| `vin` | `std::string` | Vehicle Identification Number (17 chars) |
| `calibration_id` | `std::string` | ECU calibration ID |
| `cvn` | `std::string` | Calibration Verification Number |
| `ecu_name` | `std::string` | ECU name string |

---

### IntervalSpec

Parsed query interval from CLI or YAML.

| Field | Type | Constraints | Description |
|-------|------|-------------|-------------|
| `milliseconds` | `uint32_t` | 10–60000 (SC-OBD-3) | Resolved interval in ms |

**Parsing** (see spec.md for format table):
```
"500ms"  → 500
"1s"     → 1000
"2hz"    → 500
"0.5hz"  → 2000
"100ms"  → 100
```

---

### QueryGroup

A named set of PIDs queried at a common interval.

| Field | Type | Constraints | Description |
|-------|------|-------------|-------------|
| `name` | `std::string` | Non-empty | Group identifier (e.g., "engine", "fuel") |
| `interval` | `IntervalSpec` | | Query rate for this group |
| `pids` | `std::vector<uint8_t>` | Non-empty | PIDs to query in round-robin order |

---

### ObdConfig

Top-level YAML configuration model.

| Field | Type | Constraints | Description |
|-------|------|-------------|-------------|
| `default_interval` | `IntervalSpec` | | Fallback interval for groups without explicit interval |
| `addressing_mode` | `AddressingMode` | functional or physical | ISO 15765-4 addressing |
| `tx_id` | `uint32_t` | Default 0x7DF | Request CAN ID |
| `rx_base` | `uint32_t` | Default 0x7E8 | Base response CAN ID |
| `groups` | `std::vector<QueryGroup>` | | Named PID groups with intervals |
| `standalone_pids` | `std::vector<uint8_t>` | | PIDs not in any group (use default interval) |

---

### AddressingMode

| Value | Description |
|-------|-------------|
| `Functional` | Broadcast to 0x7DF, all ECUs respond |
| `Physical` | Target single ECU via 0x7E0–0x7E7 |

---

### ObdResult

Union type for decoded OBD output (PID value, DTC list, vehicle info, or supported PID bitmap).

```cpp
using ObdResult = std::variant<DecodedPid, DtcCode, VehicleInfo, SupportedPids>;
```

---

### SupportedPids

Result of a Mode $01 PID $00/$20/$40 query.

| Field | Type | Description |
|-------|------|-------------|
| `ecu_id` | `uint32_t` | Responding ECU |
| `supported` | `std::vector<uint8_t>` | List of supported PID numbers |

---

## Entity Relationships

```
ObdConfig 1──* QueryGroup 1──* PidDefinition
                                    │
                              PidFormula (embedded)
                                    │
ObdRequest ────────────────────── ObdResponse
                                    │
                    ┌───────────────┼───────────────┐
                    ▼               ▼               ▼
              DecodedPid       DtcCode        VehicleInfo
                    │               │               │
                    └───────────────┼───────────────┘
                                    ▼
                               ObdResult
```

---

## ISO 15765-4 CAN Addressing Summary

| Parameter | Functional | Physical |
|-----------|-----------|----------|
| Request ID (11-bit) | 0x7DF | 0x7E0 + ECU index |
| Response ID (11-bit) | 0x7E8 + ECU index | 0x7E8 + ECU index |
| Request ID (29-bit) | 0x18DB33F1 | 0x18DA00F1 + ECU |
| Max ECUs | 8 (0x7E8–0x7EF) | 1 |

**Note**: MVP supports 11-bit addressing only. 29-bit (extended) addressing is future scope.
