# Feature Specification: OBD-II Diagnostics — J1979 Modes & PID Decoding

**Feature Branch**: `002-obd-diagnostics`  
**Created**: 2026-03-10  
**Status**: Draft  
**Depends on**: `001-can-bus-scanner` (CAN transport layer, session service, capture/record infrastructure)  
**Input**: J1979 OBD-II standard, ISO 15765-4 transport, user request for diagnostic message streaming/recording with YAML-configurable PID selection

---

## Feature Overview

Extend CANmatik with active OBD-II diagnostic capabilities: query ECUs using SAE J1979 service modes over ISO 15765-4 (CAN-based diagnostics), decode PID responses into human-readable values, stream decoded results in real time, and record them alongside raw CAN traffic. PID sets and query schedules are defined in YAML configuration files, making it easy to create vehicle-specific or use-case-specific profiles without recompiling.

The feature is **modular by design**: all OBD logic lives in a standalone `canmatik_obd` static library that the CLI (and future GUI) links against. The library exposes a clean C++ API for PID definition, query scheduling, response decoding, and result streaming.

**Key constraint**: This is a **read-only diagnostic tool**. It sends standard OBD-II requests (Mode $01–$09) which are safe, legislated operations that every OBD-II-compliant vehicle must support. It does **not** send manufacturer-specific UDS commands, reflash ECUs, or modify calibrations.

---

## Scope

### In Scope

- J1979 Mode $01 (current data), $02 (freeze frame), $03 (DTCs), $04 (clear DTCs — gated by confirmation), $05 (O2 sensor monitoring), $06 (on-board monitoring test results), $07 (pending DTCs), $09 (vehicle info — VIN, calibration IDs)
- ISO 15765-4 addressing: functional broadcast (0x7DF) and physical addressing (0x7E0–0x7E7 → 0x7E8–0x7EF)
- PID decoding with scaling formulas (A, B, C, D byte references per J1979 tables)
- YAML configuration for PID selection, query groups, and scheduling
- CLI `obd` subcommand with sub-modes: `query`, `stream`, `dtc`, `info`
- Simultaneous monitor + record + OBD query operation
- Query interval selection via CLI `--interval` or YAML `interval:` field (time or frequency)
- Default YAML config auto-generated on first run if none specified
- Modular `canmatik_obd` static library (reusable outside CLI)

### Out of Scope

- UDS (ISO 14229) extended diagnostics
- Manufacturer-specific enhanced PIDs (Mode $22)
- CAN FD ISO-TP (future)
- ECU reprogramming / calibration flashing
- Bluetooth / Wi-Fi OBD adapters
- J1850 / K-Line (PWM/VPW) transport

---

## User Personas

### Persona 1 — Vehicle Diagnostics Hobbyist

Wants to read engine data (RPM, coolant temp, speed) from their car or motorcycle, view it in real time, and log it during test drives for later analysis. Comfortable with command-line tools and editing config files.

### Persona 2 — Automotive Reverse Engineer

Uses OBD queries as a baseline to map ECU behavior while simultaneously sniffing raw CAN traffic. Needs both OBD decoded output and raw frame logging in a single session. Wants to control exactly which PIDs are queried and at what rate.

### Persona 3 — Embedded Developer

Building a custom ECU or CAN device that must respond correctly to OBD-II requests. Uses the fake J2534 backend + future ECU simulator to test their device's compliance without a real vehicle.

---

## User Scenarios & Testing

### US-OBD-1 — Query Supported PIDs (Priority: P1)

A user connects to a vehicle and discovers which J1979 PIDs the ECU supports.

**Acceptance Scenarios**:

1. **Given** an active CAN channel at the correct bitrate, **When** the user runs `canmatik obd query --supported`, **Then** the tool sends Mode $01 PID $00/$20/$40/... requests via functional broadcast (0x7DF), collects responses from all responding ECUs, and displays a table of supported PIDs per ECU address.
2. **Given** multiple ECUs respond (e.g., engine, transmission), **When** results are displayed, **Then** each ECU's response address (0x7E8, 0x7E9, etc.) is shown with its supported PID bitmap.
3. **Given** `--json` is specified, **When** the query completes, **Then** output is a JSON object with ECU addresses as keys and PID arrays as values.

### US-OBD-2 — Stream Live PID Data (Priority: P1)

A user selects a set of PIDs and streams their decoded values in real time.

**Acceptance Scenarios**:

1. **Given** an OBD YAML config with `pids: [0x0C, 0x0D, 0x05]` (RPM, speed, coolant temp), **When** the user runs `canmatik obd stream`, **Then** the tool cyclically queries each PID at the configured interval and displays decoded values with units.
2. **Given** `--interval 500ms` or `--interval 2hz`, **When** streaming, **Then** queries are sent at the specified rate (once per 500ms, or twice per second).
3. **Given** `--record -o session.jsonl` is also specified, **When** streaming, **Then** both raw CAN frames and decoded OBD values are recorded to the file.
4. **Given** the user presses Ctrl+C, **When** streaming is active, **Then** the session stops gracefully and reports statistics (queries sent, responses received, decode errors).

### US-OBD-3 — Read and Clear DTCs (Priority: P2)

A user reads diagnostic trouble codes from the vehicle and optionally clears them.

**Acceptance Scenarios**:

1. **Given** an active connection, **When** the user runs `canmatik obd dtc`, **Then** the tool sends Mode $03 (stored DTCs), $07 (pending DTCs), and displays decoded DTC codes (e.g., P0300, C0035) with count.
2. **Given** `canmatik obd dtc --clear`, **When** the user confirms the action, **Then** Mode $04 is sent. The tool re-queries DTCs to verify they were cleared.
3. **Given** `--force` is NOT specified with `--clear`, **When** the command runs, **Then** the user is prompted with "WARNING: This will clear DTCs and turn off MIL. Continue? [y/N]" before sending Mode $04.

### US-OBD-4 — Vehicle Information (Priority: P2)

A user retrieves VIN, calibration IDs, and ECU name via Mode $09.

**Acceptance Scenarios**:

1. **Given** an active connection, **When** the user runs `canmatik obd info`, **Then** the tool queries Mode $09 PIDs $02 (VIN), $04 (calibration ID), $0A (ECU name) and displays results.
2. **Given** an ECU supports only VIN, **When** the query runs, **Then** unsupported PIDs are reported as "N/A" without error.

### US-OBD-5 — YAML Configuration Management (Priority: P1)

PID sets and query parameters are managed via YAML files.

**Acceptance Scenarios**:

1. **Given** no config file is specified and no `obd.yaml` exists in the working directory, **When** the user runs any `obd` subcommand, **Then** a default `obd.yaml` is created with a common PID set (RPM, speed, coolant temp, throttle, MAF, intake temp, fuel trims) and 1Hz query interval.
2. **Given** `--obd-config my_profile.yaml`, **When** the command runs, **Then** the specified YAML file is loaded instead of the default.
3. **Given** a YAML file with `groups:` defining multiple PID groups with different intervals, **When** streaming, **Then** each group is queried at its own rate.
4. **Given** a YAML syntax error, **When** loading, **Then** a clear error message with line number is displayed and the tool exits with code 1.

### US-OBD-6 — Concurrent Monitor + Record + OBD (Priority: P1)

A user runs passive monitoring, recording, and OBD queries simultaneously.

**Acceptance Scenarios**:

1. **Given** `canmatik obd stream --monitor --record -o capture.asc`, **When** the session is active, **Then** all three activities run concurrently: raw CAN frames are displayed, OBD decoded values are displayed, and all frames are recorded.
2. **Given** concurrent mode, **When** OBD query frames (0x7DF→0x7E8) appear on the bus, **Then** the monitor stream shows them as regular CAN frames AND the OBD decoder shows the decoded values — both are visible.
3. **Given** concurrent mode, **When** the recorded file is inspected, **Then** it contains all CAN frames including OBD request/response pairs.

---

## Safety Constraints

| ID | Constraint | Rationale |
|----|-----------|-----------|
| SC-OBD-1 | Mode $04 (clear DTCs) requires interactive confirmation unless `--force` is given | Prevents accidental MIL reset during emissions-related troubleshooting |
| SC-OBD-2 | No Mode $08 (control of on-board system) | Direct actuator control is out of scope — safety risk |
| SC-OBD-3 | Query interval floor: 10ms minimum | Prevents bus flooding that could cause ECU communication errors |
| SC-OBD-4 | ISO 15765-4 timing: P2CAN timeout = 50ms (standard), max 5000ms for Mode $09 multi-frame | Per J1979 spec to avoid false timeouts |
| SC-OBD-5 | All OBD frames TX'd are logged before sending | Auditability — user can always see exactly what was sent |

---

## YAML Configuration Schema

```yaml
# obd.yaml — OBD-II query configuration for CANmatik

# Default query interval (applied to groups without explicit interval)
# Formats: "500ms", "1s", "2hz", "0.5hz", "100ms"
interval: "1s"

# Target ECU addressing (optional, default: functional broadcast)
addressing:
  mode: functional    # functional | physical
  tx_id: 0x7DF       # request ID (functional default)
  rx_base: 0x7E8     # first response ID

# PID groups — each group is queried independently at its own rate
groups:
  - name: engine
    interval: "500ms"  # override default
    pids:
      - id: 0x0C       # Engine RPM
      - id: 0x0D       # Vehicle Speed
      - id: 0x05       # Coolant Temperature
      - id: 0x04       # Calculated Engine Load

  - name: fuel
    interval: "1s"
    pids:
      - id: 0x06       # Short Term Fuel Trim Bank 1
      - id: 0x07       # Long Term Fuel Trim Bank 1
      - id: 0x10       # MAF Air Flow Rate
      - id: 0x0F       # Intake Air Temperature

  - name: o2_sensors
    interval: "2s"
    pids:
      - id: 0x14       # O2 Sensor 1 (Bank 1, Sensor 1)
      - id: 0x15       # O2 Sensor 2 (Bank 1, Sensor 2)

# Standalone PID list (uses default interval, no group name)
pids:
  - id: 0x0C
  - id: 0x0D
  - id: 0x05
  - id: 0x11           # Throttle Position
```

---

## Query Interval Specification

The interval can be specified as:

| Format | Example | Meaning |
|--------|---------|---------|
| Milliseconds | `100ms`, `500ms` | Query every N milliseconds |
| Seconds | `1s`, `0.5s`, `2.5s` | Query every N seconds |
| Hertz | `10hz`, `2hz`, `0.5hz` | Query N times per second |

Parsing rules:
- Case-insensitive suffix matching
- Minimum: 10ms (SC-OBD-3)
- Maximum: 60s (sanity cap)
- Default: 1s (1Hz)
- CLI `--interval` overrides YAML `interval:` for the entire session

---

## Non-Functional Requirements

| ID | Requirement | Target |
|----|------------|--------|
| NF-OBD-1 | PID decode latency | < 1ms per PID (table lookup + formula) |
| NF-OBD-2 | Memory for PID database | < 50 KB for full Mode $01 table |
| NF-OBD-3 | YAML parse time | < 100ms for typical config |
| NF-OBD-4 | Library binary size | < 200 KB for `canmatik_obd.a` |
| NF-OBD-5 | Zero external runtime dependencies | yaml-cpp linked statically |
