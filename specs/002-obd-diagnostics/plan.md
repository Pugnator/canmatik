# Implementation Plan: OBD-II Diagnostics

**Branch**: `002-obd-diagnostics` | **Date**: 2026-03-10 | **Spec**: [spec.md](spec.md)  
**Depends on**: `001-can-bus-scanner` (canmatik_core, canmatik_platform, canmatik_services, canmatik_logging)

---

## Summary

Add a modular `canmatik_obd` static library that implements J1979 OBD-II
diagnostic queries over ISO 15765-4 CAN addressing. The library provides PID
definition tables, query scheduling, response parsing, value decoding, and DTC
interpretation. YAML configuration (via statically linked yaml-cpp) controls
which PIDs to query and at what rate. A new `obd` CLI subcommand exposes
`query`, `stream`, `dtc`, and `info` sub-modes, all composable with existing
`--monitor` and `--record` flags for simultaneous operation.

---

## Technical Context

**Existing stack**: C++20, MinGW-w64 i686, CMake+Ninja, CLI11, nlohmann/json, Catch2, TinyLog  
**New dependency**: [yaml-cpp](https://github.com/jbeder/yaml-cpp) 0.8+ — FetchContent, static link  
**Build**: `canmatik_obd` as `STATIC` library; CLI links `canmatik_obd` alongside existing libs  
**ISO 15765-4**: Single-frame for most Mode $01 PIDs; multi-frame for Mode $09 (VIN, Cal ID)  
**Threading**: OBD query loop runs in a dedicated thread; results delivered via `ICaptureSync` or callback  

---

## Architecture

### Library Decomposition

```
canmatik_obd (NEW — STATIC library)
├── obd/
│   ├── pid_table.h / .cpp           # Built-in J1979 Mode $01 PID definitions
│   ├── pid_decoder.h / .cpp         # Formula evaluation (raw bytes → value)
│   ├── dtc_decoder.h / .cpp         # Mode $03/$07 DTC parsing
│   ├── obd_request.h                # ObdRequest value type
│   ├── obd_response.h / .cpp        # ObdResponse parsing from CanFrame
│   ├── obd_session.h / .cpp         # Query orchestration (send request, wait response, decode)
│   ├── query_scheduler.h / .cpp     # Multi-group interval scheduling
│   ├── interval_spec.h / .cpp       # Parse "500ms" / "2hz" / "1s"
│   ├── obd_config.h / .cpp          # ObdConfig YAML loading + default generation
│   └── iso15765.h                   # ISO 15765-4 constants and frame helpers
```

### Dependency Graph

```
canmatik (CLI exe)
  ├── canmatik_obd    (NEW)
  │     ├── canmatik_core   (CanFrame, filter, timestamp)
  │     └── yaml-cpp        (YAML parsing)
  ├── canmatik_core
  ├── canmatik_platform
  ├── canmatik_mock
  ├── canmatik_services
  ├── canmatik_logging
  └── CLI11
```

### ISO 15765-4 Frame Encoding

**Single frame request** (Mode $01, PID $0C — RPM):
```
CAN ID: 0x7DF (functional broadcast)
DLC: 8
Data: 02 01 0C 55 55 55 55 55
      ── ── ── ── ── ── ── ──
      │  │  │  └──────────────── padding (0x55)
      │  │  └──────────────────── PID
      │  └─────────────────────── Mode
      └────────────────────────── ISO-TP PCI: single frame, 2 bytes
```

**Single frame response** (from ECU at 0x7E8):
```
CAN ID: 0x7E8
DLC: 8
Data: 04 41 0C 1A F8 55 55 55
      ── ── ── ── ── ── ── ──
      │  │  │  │  │  └───────── padding
      │  │  │  └──┘──────────── A=0x1A, B=0xF8 → (256*26+248)/4 = 1726 RPM
      │  │  └─────────────────── PID echo
      │  └────────────────────── Mode + 0x40 (positive response)
      └───────────────────────── ISO-TP PCI: single frame, 4 bytes
```

**Multi-frame response** (Mode $09 PID $02 — VIN, 17 bytes):
Uses ISO-TP First Frame + Consecutive Frames + Flow Control.

### Query Scheduler Design

```
QueryScheduler
  │
  ├── Group "engine" (interval=500ms)
  │     ├── PID 0x0C (RPM)      ← next query at T+0ms
  │     ├── PID 0x0D (Speed)    ← next query at T+0ms
  │     └── PID 0x05 (Coolant)  ← next query at T+0ms
  │
  └── Group "fuel" (interval=1000ms)
        ├── PID 0x10 (MAF)      ← next query at T+0ms
        └── PID 0x0F (IAT)      ← next query at T+0ms

Scheduler loop (dedicated thread):
  1. Find group with earliest next_query_time
  2. Send next PID request in that group (round-robin)
  3. Wait for response (P2CAN timeout = 50ms)
  4. Decode response → emit DecodedPid via callback
  5. If all PIDs in group queried this cycle, advance next_query_time += interval
  6. Sleep until next scheduled query
```

### Concurrent Operation Architecture

```
                    ┌─────────────┐
                    │  IChannel   │ (shared, thread-safe read/write)
                    └──────┬──────┘
                           │
              ┌────────────┼────────────┐
              │            │            │
        ┌─────┴─────┐ ┌───┴────┐ ┌────┴─────┐
        │ Capture    │ │  OBD   │ │  Record  │
        │ Service    │ │ Session│ │  Service │
        │ (monitor)  │ │ (query)│ │ (writer) │
        └─────┬─────┘ └───┬────┘ └────┬─────┘
              │            │            │
              ▼            ▼            ▼
          CLI stdout   CLI stdout   Log file
          (frames)     (decoded)    (all frames)
```

- **CaptureService** reads from channel in its own thread, distributes to sinks
- **ObdSession** writes requests and reads responses on the same channel
- **RecordService** receives all frames (including OBD req/resp) via ICaptureSync
- Channel read/write are thread-safe (J2534 DLLLoader implementations typically serialize internally; our code adds no extra lock)

---

## YAML-CPP Integration

**FetchContent** in top-level CMakeLists.txt:
```cmake
FetchContent_Declare(
    yaml-cpp
    GIT_REPOSITORY https://github.com/jbeder/yaml-cpp.git
    GIT_TAG        0.8.0
)
set(YAML_CPP_BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(YAML_CPP_BUILD_TOOLS OFF CACHE BOOL "" FORCE)
set(YAML_CPP_BUILD_CONTRIB OFF CACHE BOOL "" FORCE)
set(YAML_CPP_INSTALL OFF CACHE BOOL "" FORCE)
FetchContent_MakeAvailable(yaml-cpp)
```

Link as: `target_link_libraries(canmatik_obd PUBLIC canmatik_core yaml-cpp)`

---

## Default Config Generation

When no `obd.yaml` exists and no `--obd-config` is given, generate:

```yaml
# CANmatik OBD-II default configuration
# Generated automatically — edit to customize PID selection

interval: "1s"

addressing:
  mode: functional
  tx_id: 0x7DF
  rx_base: 0x7E8

groups:
  - name: engine_basics
    pids:
      - id: 0x0C   # Engine RPM
      - id: 0x0D   # Vehicle Speed
      - id: 0x05   # Coolant Temperature
      - id: 0x04   # Calculated Engine Load
      - id: 0x11   # Throttle Position

  - name: fuel_system
    interval: "2s"
    pids:
      - id: 0x06   # Short Term Fuel Trim (Bank 1)
      - id: 0x07   # Long Term Fuel Trim (Bank 1)
      - id: 0x10   # MAF Air Flow Rate
      - id: 0x0F   # Intake Air Temperature
```

---

## CLI Integration

### New subcommand: `obd`

```
canmatik obd query --supported [--mock] [--json]
canmatik obd stream [--obd-config FILE] [--interval SPEC] [--monitor] [--record -o FILE] [--mock]
canmatik obd dtc [--clear] [--force] [--mock] [--json]
canmatik obd info [--mock] [--json]
```

Global options `--provider`, `--mock`, `--json`, `--bitrate` apply as usual.

### Interval CLI override

`--interval` accepts the same format as YAML: `500ms`, `1s`, `2hz`, etc.
When specified, it overrides the YAML `interval:` for ALL groups in the session.

---

## Phase Plan

| Phase | Scope | Depends On |
|-------|-------|-----------|
| 1 — Foundation | YAML-CPP integration, IntervalSpec parser, ObdConfig loader, default config generation | 001-can-bus-scanner complete |
| 2 — PID Tables & Decoding | PidTable (Mode $01 full table), PidDecoder, formula evaluation, unit tests | Phase 1 |
| 3 — ISO 15765-4 Transport | ObdRequest/ObdResponse encoding/decoding, single-frame + multi-frame (VIN), DTC decoder | Phase 2 |
| 4 — Query Engine | ObdSession (send/receive/decode), QueryScheduler (multi-group timing), thread-safe channel integration | Phase 3 |
| 5 — CLI Integration | `obd` subcommand with `query`/`stream`/`dtc`/`info`, concurrent monitor+record flags | Phase 4 |
| 6 — Polish | Default config generation, error messages, edge cases, documentation | Phase 5 |

---

## Testing Strategy

| Layer | Approach |
|-------|---------|
| IntervalSpec | Unit tests: all format variants, edge cases, min/max bounds |
| ObdConfig | Unit tests: YAML loading, default generation, merge with CLI overrides |
| PidTable | Unit tests: lookup by mode+PID, all Mode $01 entries, unknown PID handling |
| PidDecoder | Unit tests: every formula type with known input/output pairs from J1979 tables |
| DtcDecoder | Unit tests: DTC byte decoding, category classification, all 4 prefixes |
| ObdRequest | Unit tests: CAN frame encoding correctness |
| ObdResponse | Unit tests: CAN frame → response parsing, mode+0x40 validation, multi-frame assembly |
| ObdSession | Integration tests with MockChannel: send query → receive canned response → verify decoded value |
| QueryScheduler | Unit tests: timing correctness, round-robin, multi-group scheduling |
| CLI `obd` | Integration tests with `--mock`: query supported, stream with interval, DTC read |

---

## Project Structure Additions

```
src/
├── obd/                              # canmatik_obd library (NEW)
│   ├── pid_table.h / .cpp
│   ├── pid_decoder.h / .cpp
│   ├── dtc_decoder.h / .cpp
│   ├── obd_request.h
│   ├── obd_response.h / .cpp
│   ├── obd_session.h / .cpp
│   ├── query_scheduler.h / .cpp
│   ├── interval_spec.h / .cpp
│   ├── obd_config.h / .cpp
│   └── iso15765.h
├── cli/
│   └── cmd_obd.cpp                   # OBD CLI subcommand (NEW)
samples/
└── configs/
    └── obd_default.yaml              # Reference default config
tests/
├── unit/
│   ├── test_interval_spec.cpp
│   ├── test_pid_table.cpp
│   ├── test_pid_decoder.cpp
│   ├── test_dtc_decoder.cpp
│   ├── test_obd_request.cpp
│   ├── test_obd_response.cpp
│   ├── test_obd_config.cpp
│   └── test_query_scheduler.cpp
└── integration/
    ├── test_obd_session.cpp
    └── test_obd_cli.cpp
```
