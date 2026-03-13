# Implementation Plan: J2534 CAN Bus Scanner & Logger

**Branch**: `001-can-bus-scanner` | **Date**: 2026-03-08 | **Spec**: [spec.md](spec.md)
**Input**: Feature specification from `/specs/001-can-bus-scanner/spec.md`

## Summary

Build a C++20 desktop application (CLI + future GUI) that connects to J2534
Pass-Thru interfaces on Windows, captures raw CAN frames in real time, applies
ID-based filters, logs traffic in Vector ASC and JSONL formats, and replays
previously captured sessions offline. The architecture isolates
Windows/J2534-specific code behind narrow abstractions so the core domain
(frames, filters, logging, replay) is testable without hardware via a mock
backend.

## Technical Context

**Language/Version**: C++20 (ISO 14882:2020) — required by TinyLog's use of `std::format`
**Compiler/Toolchain**: MinGW-w64 (GCC ≥ 13, POSIX/winpthreads threading model)
**Build System**: CMake ≥ 3.24
**Primary Dependencies**: CLI11 2.4, nlohmann/json 3.11, Catch2 v3, [TinyLog](https://github.com/Pugnator/TinyLog) (singleton logger, git submodule; transitively depends on zstd, vendored within TinyLog repo)
**Phase 3+ Dependencies**: ImGui 1.91 (Win32+OpenGL3 backend)
**Storage**: Files — Vector ASC (text), JSONL (structured), CSV (future)
**Testing**: Catch2 v3 with CTest integration (`catch_discover_tests()`)
**Target Platform**: Windows 10+ (32-bit build for universal J2534 DLL compatibility)
**Project Type**: Desktop application — CLI primary, ImGui GUI secondary
**Performance Goals**: Zero frame loss at 100% CAN bus utilization (adapter-limited); <100 ms frame-to-display latency
**Constraints**: <100 MB resident memory for typical sessions; static linking for distribution (`-static`)
**Scale/Scope**: Single-user, single-device, single-channel MVP; ~15 k LOC estimated

## Constitution Check

*GATE: Must pass before Phase 0 research. Re-check after Phase 1 design.*

| # | Principle | Pre-Research | Post-Design | Notes |
|---|-----------|:---:|:---:|-------|
| I | Read First, Transmit Later | PASS | PASS | Default mode is passive. `TransmitService` is isolated and requires explicit opt-in. GUI shows mode badge. |
| II | Open and Inspectable | PASS | PASS | All logic is open-source C++. J2534 DLLs are the only closed dependency; every DLL call is logged at debug level. |
| III | Vehicle-Agnostic Core | PASS | PASS | Zero brand-specific code in `core/` or `platform/`. Future decoder plugins load at runtime from `plugins/`. |
| IV | Deterministic Logging | PASS | PASS | `CanFrame` struct carries all required fields (timestamp µs, channel, arb ID 11/29-bit, frame type, DLC, raw payload). ASC and JSONL writers preserve all fields. |
| V | Safe by Design | PASS | PASS | Three modes defined as enum (`Passive`, `ActiveQuery`, `ActiveInject`). MVP ships `Passive` only. Mode transitions gated by explicit flag + confirmation. |
| VI | Incremental Reverse Engineering | PASS | PASS | Unknown IDs displayed and logged without error. No decode DB required. Future `DecoderPlugin` interface reserved. |
| VII | CLI-First Foundation | PASS | PASS | CLI is fully functional standalone. GUI consumes the same `SessionService` / `CaptureService` / `ReplayService`. GUI introduces no absent-from-CLI features. |
| VIII | Portable Architecture | PASS | PASS | Five layers: J2534 transport → CAN session → logging/storage → decoding (future) → UI/CLI. Each independently testable. Windows code behind `IDeviceProvider` abstraction. |

**Justified expansion**: The user request includes ImGui GUI in the MVP alongside
the CLI. Constitution Principle VII says "CLI before GUI" and "GUI MUST NOT
introduce functionality absent from CLI." This is satisfied: the CLI is the
first-class interface, fully functional alone. The GUI is a thin presentation
layer over shared services, phased after CLI (Phase 3). No violation.

## Project Structure

### Documentation (this feature)

```text
specs/001-can-bus-scanner/
├── plan.md              # This file
├── research.md          # Phase 0 output
├── data-model.md        # Phase 1 output
├── quickstart.md        # Phase 1 output
├── contracts/           # Phase 1 output
│   └── cli-commands.md
└── tasks.md             # Phase 2 output (/speckit.tasks)
```

### Source Code (repository root)

```text
CMakeLists.txt                   # Top-level build orchestration
README.md
CONTRIBUTING.md
SAFETY.md

src/
├── core/                        # Platform-agnostic domain logic
│   ├── can_frame.h              # CanFrame, FrameType, CanId types
│   ├── can_frame.cpp
│   ├── filter.h                 # FilterRule, FilterEngine
│   ├── filter.cpp
│   ├── session_status.h         # SessionStatus, counters, mode enum
│   ├── session_status.cpp
│   ├── capture_sink.h           # ICaptureSync interface (observer)
│   ├── timestamp.h              # Timestamp helpers, rollover handling
│   ├── label_store.h            # LabelStore: user-assigned arb ID labels (FR-021)
│   └── label_store.cpp          # LabelStore persistence (labels.json)
│
├── transport/                   # Hardware abstraction
│   ├── device_provider.h        # IDeviceProvider interface
│   ├── device_info.h            # DeviceInfo struct
│   ├── channel.h                # IChannel interface
│   └── transport_error.h        # TransportError types
│
├── platform/                    # Windows-specific implementations
│   └── win32/
│       ├── j2534_provider.h     # J2534Provider : IDeviceProvider
│       ├── j2534_provider.cpp
│       ├── j2534_channel.h      # J2534Channel : IChannel
│       ├── j2534_channel.cpp
│       ├── j2534_defs.h         # J2534 API types, constants (__stdcall)
│       ├── j2534_dll_loader.h   # RAII DLL wrapper (LoadLibrary)
│       ├── j2534_dll_loader.cpp
│       └── registry_scanner.h   # Windows Registry provider discovery
│       └── registry_scanner.cpp
│
├── mock/                        # Mock backend for testing & demo
│   ├── mock_provider.h          # MockProvider : IDeviceProvider
│   ├── mock_provider.cpp
│   ├── mock_channel.h           # MockChannel : IChannel
│   └── mock_channel.cpp
│
├── services/                    # Application services (shared by CLI & GUI)
│   ├── session_service.h        # Connect, open channel, start/stop session
│   ├── session_service.cpp
│   ├── capture_service.h        # Reader thread, frame dispatch, sinks
│   ├── capture_service.cpp
│   ├── record_service.h         # Start/stop recording, file management
│   ├── record_service.cpp
│   ├── replay_service.h         # Load log, iterate frames, search
│   ├── replay_service.cpp
│   └── transmit_service.h       # Future: controlled frame transmission
│   └── transmit_service.cpp
│
├── logging/                     # Log format writers and readers
│   ├── log_writer.h             # ILogWriter interface
│   ├── asc_writer.h             # AscWriter : ILogWriter
│   ├── asc_writer.cpp
│   ├── jsonl_writer.h           # JsonlWriter : ILogWriter
│   ├── jsonl_writer.cpp
│   ├── log_reader.h             # ILogReader interface
│   ├── asc_reader.h             # AscReader : ILogReader
│   ├── asc_reader.cpp
│   ├── jsonl_reader.h           # JsonlReader : ILogReader
│   └── jsonl_reader.cpp
│
├── cli/                         # CLI frontend
│   ├── main.cpp                 # CLI entry point
│   ├── cli_app.h                # CLI11 setup, subcommand dispatch
│   ├── cli_app.cpp
│   ├── cmd_scan.cpp             # `canmatik scan`
│   ├── cmd_monitor.cpp          # `canmatik monitor`
│   ├── cmd_record.cpp           # `canmatik record`
│   ├── cmd_replay.cpp           # `canmatik replay`
│   ├── cmd_status.cpp           # `canmatik status`
│   ├── cmd_demo.cpp             # `canmatik demo`
│   ├── cmd_label.cpp            # `canmatik label set/list` (FR-021)
│   └── formatters.h             # Text / JSON output formatters
│
├── gui/                         # ImGui frontend (Phase 3+)
│   ├── main.cpp                 # GUI entry point (Win32+OpenGL3)
│   ├── gui_app.h                # ImGui setup, render loop
│   ├── gui_app.cpp
│   ├── panels/
│   │   ├── device_panel.h       # Provider selection, connect/disconnect
│   │   ├── session_panel.h      # Bitrate, start/stop, mode badge
│   │   ├── traffic_panel.h      # Live frame table
│   │   ├── filter_panel.h       # Filter controls
│   │   ├── record_panel.h       # Recording controls
│   │   ├── replay_panel.h       # Offline log viewer
│   │   └── status_panel.h       # Diagnostics, counters, errors
│   └── imgui_helpers.h          # Utility widgets
│
└── config/                      # Configuration loading
    ├── config.h                 # Config struct, defaults
    └── config.cpp               # JSON/CLI config merge logic

third_party/
├── imgui/                       # Git submodule: ocornut/imgui (tagged release)
├── tinylog/                     # Git submodule: Pugnator/TinyLog (singleton logger)
└── CMakeLists.txt               # Compiles tinylog/log.cc and imgui sources as STATIC libraries (bypasses TinyLog's hardcoded SHARED default; links zstd statically)

tests/
├── unit/
│   ├── test_can_frame.cpp
│   ├── test_filter.cpp
│   ├── test_timestamp.cpp
│   ├── test_asc_writer.cpp
│   ├── test_asc_reader.cpp
│   ├── test_jsonl_writer.cpp
│   ├── test_jsonl_reader.cpp
│   ├── test_session_status.cpp
│   ├── test_config.cpp
│   └── test_label_store.cpp     # LabelStore unit tests (FR-021)
├── integration/
│   ├── test_mock_capture.cpp    # Full capture pipeline with mock backend
│   ├── test_record_replay.cpp   # Record → save → load → verify roundtrip
│   ├── test_cli_commands.cpp    # CLI subcommand end-to-end
│   ├── test_replay_performance.cpp  # SC-005: 1M-frame search benchmark
│   └── test_capture_performance.cpp # Latency + memory smoke test
└── CMakeLists.txt

samples/
├── configs/
│   └── default.json             # Example configuration
├── captures/
│   ├── idle_500kbps.asc         # Sample ASC log
│   └── idle_500kbps.jsonl       # Sample JSONL log
└── README.md

docs/
├── safety-notice.md
├── adapter-compatibility.md
├── log-format-spec.md
└── j2534-notes.md
```

**Structure Decision**: Single-project layout with `src/` organized by architectural
layer. Transport abstraction (`IDeviceProvider`, `IChannel`) separates
platform-specific code (`src/platform/win32/`) from the core domain
(`src/core/`) and services (`src/services/`). CLI and GUI are thin frontends
with separate entry points but shared services. Tests mirror the `src/` layout.

## Data Flow

### 1. Live Capture

```
┌─────────────┐    ┌──────────────────┐    ┌────────────────┐
│  User CLI/   │    │ SessionService    │    │ J2534Provider   │
│  GUI action  │───▶│ connect(provider) │───▶│ (LoadLibrary)   │
│  "monitor"   │    │ openChannel(bps)  │    │ PassThruOpen    │
└─────────────┘    │ startCapture()    │    │ PassThruConnect │
                   └───────┬──────────┘    │ StartMsgFilter  │
                           │               └────────┬───────┘
                           ▼                        │
                   ┌───────────────┐               │
                   │ CaptureService │◀──────────────┘
                   │ (reader thread)│  PassThruReadMsgs (poll loop)
                   │               │
                   │  normalize →  │
                   │  timestamp →  │
                   │  enqueue →    │
                   └───────┬───────┘
                           │ SPSC queue
                           ▼
                   ┌───────────────┐
                   │ Consumer thread│
```

**SPSC Queue Specification**: The reader-to-consumer queue is a bounded lock-free single-producer single-consumer ring buffer (capacity: 65 536 frames, ~4 MB). On overflow, the oldest unread frame is dropped and `SessionStatus::dropped` is incremented. This ensures the reader thread never blocks on a slow consumer while providing back-pressure visibility via the dropped counter.

```
                   │               │
                   │ FilterEngine  │──▶ display (CLI formatter / GUI panel)
                   │   .apply()    │
                   │               │──▶ RecordService (ASC/JSONL writer)
                   │ SessionStatus │
                   │   .update()   │──▶ status (counters, errors)
                   └───────────────┘
```

### 2. Offline Replay

```
┌───────────┐    ┌──────────────┐    ┌────────────┐
│ User opens │───▶│ ReplayService │───▶│ AscReader / │
│ log file   │    │ load(path)   │    │ JsonlReader │
└───────────┘    │              │    └─────┬──────┘
                 │ iterate()    │◀─────────┘
                 │ search(id)   │    parsed CanFrame stream
                 │ summary()    │
                 └──────┬───────┘
                        │
                        ▼
                 FilterEngine → display (CLI / GUI)
```

### 3. Controlled Transmission (Future — not MVP)

```
                       explicit user action
                              │
                              ▼
                 ┌───────────────────────┐
                 │ TransmitService       │
                 │ mode must be Active   │
                 │ confirm() if ECU-write│
                 │ validate frame        │
                 │ PassThruWriteMsgs     │
                 │ audit log entry       │
                 └───────────────────────┘
                              │
                  visible in CLI output + GUI badge
                  logged with TX marker in capture
```

## Core Abstractions

> **Note**: The pseudo-code below is illustrative. Authoritative entity definitions, field types, constraints, and validation rules live in [data-model.md](data-model.md). Keep data-model.md as the single source of truth; update these sketches only for high-level orientation.

### Transport Layer (interfaces)

```cpp
// Abstract provider — implemented by J2534Provider (Windows) or MockProvider
struct DeviceInfo {
    std::string name;           // "Tactrix OpenPort 2.0 J2534 ISO/CAN/VPW/PWM"
    std::string vendor;         // "Tactrix"
    std::string dll_path;       // "C:\...\op20pt32.dll"
    bool supports_can;
    bool supports_iso15765;
};

class IDeviceProvider {
public:
    virtual ~IDeviceProvider() = default;
    virtual std::vector<DeviceInfo> enumerate() = 0;
    virtual std::unique_ptr<IChannel> connect(const DeviceInfo& dev) = 0;
};

// Abstract channel — wraps one J2534 channel or mock channel
class IChannel {
public:
    virtual ~IChannel() = default;
    virtual void open(uint32_t bitrate) = 0;
    virtual void close() = 0;
    virtual std::vector<CanFrame> read(uint32_t timeout_ms) = 0;
    virtual void write(const CanFrame& frame) = 0;    // gated
    virtual void setFilter(uint32_t mask, uint32_t pattern) = 0;
    virtual void clearFilters() = 0;
    virtual bool isOpen() const = 0;
};
```

### Core Domain

```cpp
enum class FrameType : uint8_t {
    Standard,   // 11-bit ID
    Extended,   // 29-bit ID
    FD,         // CAN FD (future)
    Error,      // Error frame
    Remote      // Remote frame
};

enum class OperatingMode : uint8_t {
    Passive,        // Receive only (default)
    ActiveQuery,    // Diagnostic requests (future)
    ActiveInject    // Arbitrary TX (future)
};

struct CanFrame {
    uint64_t adapter_timestamp_us;  // From J2534 adapter (rollover-extended)
    uint64_t host_timestamp_us;     // From steady_clock
    uint32_t arbitration_id;        // 11-bit or 29-bit
    FrameType type;
    uint8_t dlc;
    uint8_t data[64];               // 8 for classic CAN, 64 for FD (future)
    uint8_t channel_id;
};

struct FilterRule {
    enum class Action { Pass, Block };
    Action action;
    uint32_t id_value;
    uint32_t id_mask;               // 0xFFFFFFFF = exact match
};

// Capture sink — observer pattern for frame dispatch
class ICaptureSync {
public:
    virtual ~ICaptureSync() = default;
    virtual void onFrame(const CanFrame& frame) = 0;
    virtual void onError(const TransportError& error) = 0;
};

struct SessionStatus {
    OperatingMode mode = OperatingMode::Passive;
    std::string provider_name;
    std::string adapter_name;
    uint32_t bitrate = 0;
    bool channel_open = false;
    bool recording = false;
    std::string recording_file;
    uint64_t frames_received = 0;
    uint64_t frames_transmitted = 0;
    uint64_t errors = 0;
    uint64_t dropped = 0;
    std::chrono::steady_clock::time_point session_start;
    std::vector<FilterRule> active_filters;
};
```

### Logging

```cpp
class ILogWriter {
public:
    virtual ~ILogWriter() = default;
    virtual void writeHeader(const SessionStatus& status) = 0;
    virtual void writeFrame(const CanFrame& frame) = 0;
    virtual void writeFooter(const SessionStatus& status) = 0;
    virtual void flush() = 0;
};

class ILogReader {
public:
    virtual ~ILogReader() = default;
    virtual bool open(const std::string& path) = 0;
    virtual std::optional<CanFrame> nextFrame() = 0;
    virtual SessionStatus metadata() const = 0;
    virtual void reset() = 0;
};
```

## GUI and CLI Interaction Model

Both CLI and GUI are **thin presentation layers** over the same service objects:

| Concern | Location | CLI Access | GUI Access |
|---------|----------|------------|------------|
| Provider discovery | `SessionService` | `canmatik scan` | Device panel dropdown |
| Connect/disconnect | `SessionService` | `canmatik monitor --provider` | Device panel button |
| Channel open/close | `SessionService` | `canmatik monitor --bitrate` | Session panel controls |
| Frame stream | `CaptureService` | stdout formatter | Traffic panel (ImGui table) |
| Filter apply/clear | `FilterEngine` | `--filter` flag | Filter panel checkboxes |
| Start/stop record | `RecordService` | `canmatik record -o` | Record panel button |
| Replay/inspect | `ReplayService` | `canmatik replay` | Replay panel |
| Session diagnostics | `SessionStatus` | `canmatik status` / footer | Status panel |
| Operating mode | `SessionService` | `--mode passive` (default) | Mode badge (green/red) |

**Rule**: If a feature exists in the GUI, it must also be invocable from the CLI.
The GUI may add visual enhancements (color coding, scrolling tables, charts) but
never new domain capabilities.

## Safety Boundaries

1. **Default mode**: `OperatingMode::Passive`. Set at construction, never changed without explicit user action.
2. **Transmission isolation**: `TransmitService` is a separate class, not part of `CaptureService`. CLI requires `--active` flag. GUI requires mode switch + confirmation dialog.
3. **Audit trail**: Every transmitted frame is logged with a `TX` marker in both console output and recording files.
4. **No flashing/programming**: No J2534 `PassThruIoctl` programming commands are implemented in MVP. The code path does not exist.
5. **Mode visibility**: CLI prints `[PASSIVE]` or `[ACTIVE]` prefix on every status line. GUI shows a persistent color-coded badge.
6. **Guard against accidental active mode**: `IChannel::write()` throws if `OperatingMode` is `Passive`. Defense in depth — even if UI code has a bug, the channel rejects the write.

## Configuration Strategy

Configuration sources, in priority order (highest wins):

1. **CLI flags** (`--provider`, `--bitrate`, `--filter`, `--output`, `--format`)
2. **Config file** (`canmatik.json` in working directory or `--config path`)
3. **Built-in defaults** (bitrate = 500000, format = asc, mode = passive, no filters)

```json
{
  "provider": "",
  "bitrate": 500000,
  "mode": "passive",
  "filters": [],
  "output": {
    "format": "asc",
    "directory": "./captures"
  },
  "gui": {
    "launch": false,
    "font_size": 14
  },
  "mock": {
    "enabled": false,
    "frame_rate": 100,
    "trace_file": ""
  },
  "logging": {
    "file": "canmatik.log",
    "max_file_size": 10485760,
    "max_backups": 5,
    "compress": true
  }
}
```

GUI reads the same config struct and writes changes back to it. No separate GUI
config. CLI `--gui` flag launches the GUI entry point instead of CLI mode.

## Logging and Observability

### What is surfaced

| Metric | CLI | GUI | Persistent Log |
|--------|-----|-----|----------------|
| Provider/adapter name | Header line | Device panel | Log header |
| Bitrate | Header line | Session panel | Log header |
| Channel state | Status line | Status panel | N/A (implicit) |
| Operating mode | `[PASSIVE]` prefix | Color badge | Log header + frame markers |
| Frames received counter | Footer / `status` | Status panel | Log footer |
| Frames transmitted counter | Footer / `status` | Status panel | Log footer |
| Error counter | Footer / `status` | Status panel (red) | Log footer |
| Dropped frame indicator | Warning line | Status panel (yellow) | Log annotation |
| Active filters | `status` output | Filter panel | Log header |
| Recording state | `[REC]` indicator | Record panel (red dot) | N/A |
| Session duration | Footer / `status` | Status panel | Log footer |
| Timestamps | Every frame line | Traffic panel column | Every frame record |

### Output modes

- **Human-readable** (default): Columnar text, one frame per line, colored where terminal supports it.
- **JSON** (`--json`): One JSON object per line (JSONL) for piping to `jq` or other tools.

### Diagnostic logging (internal) — TinyLog

Application-level diagnostic logging is handled by [TinyLog](https://github.com/Pugnator/TinyLog),
a singleton C++ logger integrated as a git submodule. TinyLog provides:

- **Severity levels**: info, debug, warning, error, verbose, critical
- **Macros**: `LOG_INFO(...)`, `LOG_DEBUG(...)`, `LOG_CALL(...)`, `LOG_EXCEPTION(...)`
- **Console tracer**: Colored output on Windows (via `WriteConsoleA` + `SetConsoleTextAttribute`)
- **File tracer**: Appends to `canmatik.log` with optional rotation and zstd compression
- **Rotation**: Configurable max file size, backup count, and zstd compression of rotated files
- **Thread-safe**: Mutex-protected writes, shared_mutex for configure/log concurrency

Diagnostic log output (adapter DLL calls, errors, state transitions) is written
via TinyLog to the console (stderr-equivalent) and/or a `canmatik.log` file when
`--verbose` / `--debug` flags are set. This is separate from CAN frame capture logs.

**Initialization**: On startup, configure TinyLog based on CLI flags:
- Default: `TraceType::console` with `TraceSeverity::info` + `TraceSeverity::warning` + `TraceSeverity::error`
- `--verbose`: Add `TraceSeverity::verbose`
- `--debug`: Add `TraceType::file` with path `canmatik.log`, enable all severity levels, apply rotation config from `canmatik.json`. Console tracer remains active.

## Error-Handling Strategy

### Error categories

| Category | Examples | User Impact | Handling |
|----------|----------|-------------|----------|
| **Fatal startup** | No J2534 providers found, config file parse error | Cannot proceed | Print error + suggestion, exit code 1 |
| **Connection failure** | Adapter not responding, DLL load failed, wrong architecture | Cannot start session | Print specific error, suggest driver install, stay in interactive mode |
| **Session fault** | Channel open failed, unsupported bitrate, filter rejected | Session cannot start | Print error, allow retry with different params |
| **Runtime fault** | Read timeout, adapter disconnect, bus-off | Session interrupted | Log warning, attempt recovery, finalize recording if active |
| **User error** | Invalid filter syntax, bad file path, TX in passive mode | Command rejected | Print validation error with correct usage |
| **Data error** | Malformed log file, version mismatch, corrupt frame | Replay fails gracefully | Print parse error with line number, skip or abort per severity |

### Error propagation

- Transport layer throws typed `TransportError` (enum + message string).
- Services catch and translate to user-friendly messages.
- CLI prints to stderr with exit code.
- GUI shows error in a status bar or modal dialog.
- All errors include: what happened, what the user can try, and (at `--debug` level) the underlying J2534 error code.

### J2534-specific error handling

```cpp
// After every J2534 call, check return code
long ret = PassThruConnect(...);
if (ret != STATUS_NOERROR) {
    char errMsg[256];
    PassThruGetLastError(errMsg);
    throw TransportError(ret, errMsg);  // "Device not connected"
}
```

## Testing Strategy

### What can be tested without hardware

| Component | Test Type | Hardware Needed |
|-----------|-----------|:---:|
| `CanFrame` construction, serialization | Unit | No |
| `FilterEngine` logic | Unit | No |
| `AscWriter` / `AscReader` roundtrip | Unit | No |
| `JsonlWriter` / `JsonlReader` roundtrip | Unit | No |
| `SessionStatus` counters | Unit | No |
| `Config` parsing, defaults, merge | Unit | No |
| Timestamp rollover handling | Unit | No |
| Full capture pipeline with `MockProvider` | Integration | No |
| Record → save → load → verify | Integration | No |
| CLI subcommand parsing and dispatch | Integration | No |
| Filter + display pipeline | Integration | No |
| Replay with sample captures | Integration | No |

### What requires real hardware

| Component | Test Type | Notes |
|-----------|-----------|-------|
| `J2534Provider` discovery + connect | Integration | Requires adapter + driver installed |
| `J2534Channel` read at bitrate | Integration | Requires adapter + live CAN bus |
| Adapter error handling (unplug, bus-off) | Manual | Hardware fault injection |

### CI strategy

CI runs all unit and integration tests using the mock backend. No hardware
required. Hardware integration tests are documented as manual test procedures in
`docs/adapter-compatibility.md`.

## Mock / Simulation Strategy

`MockProvider` implements `IDeviceProvider`. `MockChannel` implements `IChannel`.

| Scenario | Mock Behavior |
|----------|---------------|
| Successful discovery | Returns configurable list of fake `DeviceInfo` |
| Failed discovery | Returns empty list |
| Successful connect | Returns `MockChannel` |
| Failed connect | Throws `TransportError` |
| Incoming frames | Generates frames from an internal sequence or a prerecorded trace file |
| Timing variation | Configurable inter-frame delay (uniform, jitter, burst patterns) |
| Adapter errors | Injects `TransportError` at configurable intervals |
| Dropped frames | Sets overflow flags on simulated frames |
| Bus silence | Generates zero frames, returns after timeout |
| Replay trace | Reads a `.asc` or `.jsonl` file and re-emits frames at original timing |

**Activation**: `canmatik demo` or `--mock` flag. Config file `mock.enabled = true`.

## Extensibility Plan

### Future extension points

| Extension | How the Architecture Supports It |
|-----------|----------------------------------|
| ISO-TP reassembly | New service (`IsotpService`) consuming `CanFrame` stream from `CaptureService`. Core untouched. |
| UDS helpers | Builds on ISO-TP layer. New service + CLI subcommands. |
| Decoder plugins | `IDecoderPlugin` interface. Plugins loaded from `plugins/` directory at runtime. Each plugin registers CAN IDs it claims. |
| Vehicle profiles | JSON/YAML files describing known IDs for a vehicle. Loaded by decoder system. Stored in `profiles/`. |
| DBC import | New `DbcReader` in `logging/`. Feeds `IDecoderPlugin` with signal definitions. |
| Scripting API | Embed a lightweight scripting engine (Lua, Python) that calls the same services. CLI subcommand `canmatik script run`. |
| Alternative HW backends | New `IDeviceProvider` implementation (e.g., SocketCAN on Linux, Peak PCAN). Registered at startup. |
| GUI growth | New panels in `gui/panels/`. Share services with CLI. |
| Live charts | ImGui plotting extension (ImPlot). Consumes `CanFrame` stream. |
| CSV format | New `CsvWriter : ILogWriter`. Plugs into `RecordService`. |
| Binary capture | New `BinaryWriter : ILogWriter`. Plugs into `RecordService`. |

### Key rule

All extensions consume or produce `CanFrame` objects and interact through
service interfaces. No extension modifies `core/` or `transport/`. Brand-specific
knowledge never enters the core pipeline.

## Phased Delivery Plan

> **Terminology**: "Technical MVP" = Phases 1 + 2 (CLI-only, no GUI). "Full product" = Phases 1–5. The spec.md MVP scope aligns with Technical MVP. ImGui (Phase 3) is post-MVP expansion justified under Constitution Principle VII.

### Phase 1 — Windows MVP Capture Foundation

**Goal**: A user can discover a J2534 provider, connect, open a CAN channel, and
see frames streaming to the console.

**Technical outcomes**:
- `IDeviceProvider` / `IChannel` interfaces defined
- `J2534Provider` + `J2534Channel` implemented (Windows, 32-bit)
- `MockProvider` + `MockChannel` implemented
- `CanFrame` + `FilterRule`/`FilterAction` data types + `SessionStatus` core types (FilterEngine evaluation logic is Phase 2)
- `CaptureService` with reader thread + SPSC queue
- CLI `scan`, `monitor` subcommands (`demo` delivered in Phase 2 alongside US7)
- Unit tests for core types; integration test with mock capture
- CMake build with MinGW, FetchContent for CLI11/Catch2/nlohmann_json
- Reproducible build documented in README

### Phase 2 — Logging, Filtering, Replay, Diagnostics

**Goal**: A user can record traffic, save to ASC/JSONL, replay offline, and
troubleshoot adapter problems.

**Technical outcomes**:
- `AscWriter` / `JsonlWriter` + `AscReader` / `JsonlReader`
- `RecordService` (start/stop recording, file naming)
- `ReplayService` (load, iterate, search, summary)
- CLI `record`, `replay`, `status`, `demo` subcommands
- `FilterEngine` evaluation logic + `--filter` flag with ID/mask syntax
- `--json` output mode for CLI
- Sample captures in `samples/captures/`
- Roundtrip tests (record → save → load → verify)
- Log format specification in `docs/log-format-spec.md`

### Phase 3 — ImGui Live Inspection

**Goal**: A user can launch a graphical interface to browse traffic, manage
filters, and control recordings interactively.

**Technical outcomes**:
- ImGui integration (Win32 + OpenGL3 backend)
- Device panel, session panel, traffic panel, filter panel
- Record panel, replay panel, status panel
- Mode badge (passive/active indicator)
- GUI entry point (`canmatik --gui` or `canmatik-gui` binary)
- GUI shares all services with CLI — no duplicated logic

### Phase 4 — Controlled Transmission and Richer Tooling

**Goal**: A user can send individual CAN frames (with safety gates) and use
advanced capture features.

**Technical outcomes**:
- `TransmitService` with mode enforcement
- CLI `send` subcommand with `--active` flag + confirmation
- GUI active mode toggle + confirmation dialog
- TX frames logged with marker in capture
- CSV writer (third log format)
- Enhanced mock: burst patterns, error injection profiles

### Phase 5 — Extension Hooks

**Goal**: The project is ready for community-contributed decoders, vehicle
profiles, and alternative backends.

**Technical outcomes**:
- `IDecoderPlugin` interface + runtime loading
- Profile file format (JSON/YAML) for known vehicle IDs
- `DBC` reader for signal-level decoding
- Plugin discovery from `plugins/` directory
- Documentation for writing a decoder plugin
- Linux SocketCAN backend stub (compilation only, not tested on hardware)

## Risks and Mitigations

| Risk | Impact | Likelihood | Mitigation |
|------|--------|:---:|-----------|
| **Inconsistent J2534 implementations** | Adapters behave differently for filter setup, timestamp format, error codes | High | Defensive coding: validate every return, log all J2534 calls at debug level, maintain adapter compatibility notes |
| **32-bit build requirement** | Most J2534 DLLs are 32-bit; 64-bit process cannot load them | High | Build as 32-bit (`-m32`). Document clearly. Test with known adapters. |
| **Adapter timestamp quality** | Cheap adapters may have low-resolution or broken timestamps | Medium | Dual timestamp strategy (adapter + host). Flag suspicious deltas. |
| **MinGW + J2534 DLL calling convention** | `__stdcall` mismatch → stack corruption | Medium | Explicit `__stdcall` in all function pointer typedefs. `static_assert` on struct sizes. |
| **MinGW + ImGui build friction** | DirectX headers missing, linker issues | Medium | Use Win32 + OpenGL3 backend (zero external deps). Phase GUI after CLI. |
| **Unreliable hardware filtering** | Some adapters ignore `PassThruStartMsgFilter` | Medium | Always apply software filter in `FilterEngine` regardless of hardware filter. Hardware filter is optional optimization. |
| **GUI complexity distracts from capture reliability** | Time spent on UI polish instead of core robustness | Medium | Phase 3 comes after Phases 1+2 are stable. GUI is optional — CLI is always complete. |
| **Unsafe feature creep** | Contributors add TX features without safety gates | Medium | `TransmitService` is isolated. `IChannel::write()` enforces mode check. Code review checklist includes safety gate verification. |
| **Testability gap from hardware dependence** | Can't run CI without real adapter | Low | Mock backend covers all non-hardware paths. Document manual hardware test procedures. |
| **Portability erosion** | Windows-specific code leaks into core | Low | `#ifdef _WIN32` only in `src/platform/win32/`. Core compiles with `-DCANMATIK_NO_PLATFORM`. CI lint check for platform includes in `core/`. |

## Definition of Done — Technical MVP

The technical MVP (Phases 1 + 2) is done when:

- [ ] The tool discovers J2534 providers from the Windows registry
- [ ] The tool connects to a J2534 device and opens a CAN channel at 250/500/1000 kbps
- [ ] Raw CAN frames are captured continuously via a dedicated reader thread
- [ ] Frames display in the CLI with: timestamp, arb ID, frame type, DLC, payload
- [ ] ID-based filters can be applied from the CLI (`--filter`)
- [ ] Traffic can be recorded to Vector ASC and JSONL files
- [ ] Recorded logs can be replayed and inspected offline (`canmatik replay`)
- [ ] Session status is viewable (`canmatik status`)
- [ ] All features work with the mock backend (`canmatik demo`)
- [ ] Passive mode is the default; `IChannel::write()` rejects calls in passive mode
- [ ] All core and logging code has unit tests passing in CI
- [ ] Integration tests cover mock capture → filter → record → replay roundtrip
- [ ] Windows-specific code is confined to `src/platform/win32/`
- [ ] Build is reproducible with documented CMake + MinGW steps
- [ ] README includes: build instructions, quick-start, safety notice, example commands
- [ ] Exit codes distinguish success (0), user error (1), and hardware failure (2)

## Complexity Tracking

No constitution violations requiring justification. GUI inclusion in the broader
plan (Phase 3) is phased after CLI and shares services — no principle violated.
