# Data Model: J2534 CAN Bus Scanner & Logger

**Feature Branch**: `001-can-bus-scanner`
**Created**: 2026-03-08
**Input**: plan.md, spec.md, research.md

---

## Entity Overview

```
DeviceInfo ──────────┐
                     │ discovered by
                     ▼
              IDeviceProvider
                     │
                     │ connect()
                     ▼
              IChannel ──── open(bitrate) ────▶ CanFrame stream
                     │                              │
                     │                              │ filtered by
                     │                              ▼
                     │                        FilterEngine
                     │                              │
                     │              ┌───────────────┼───────────────┐
                     │              ▼               ▼               ▼
                     │        CLI display     RecordService    SessionStatus
                     │                              │
                     │                              ▼
                     │                     ILogWriter (ASC/JSONL)
                     │                              │
                     │                              ▼
                     │                         Log File
                     │                              │
                     │                     ILogReader (ASC/JSONL)
                     │                              │
                     │                              ▼
                     │                       ReplayService
                     │
                     └──── close()
```

---

## Entities

### CanFrame

The fundamental unit of captured data. Immutable once created.

| Field | Type | Constraints | Description |
|-------|------|-------------|-------------|
| `adapter_timestamp_us` | `uint64_t` | ≥ 0; rollover-extended from 32-bit source | Adapter hardware timestamp in microseconds since adapter epoch |
| `host_timestamp_us` | `uint64_t` | ≥ 0; monotonic within session | Host-side timestamp from `steady_clock` at receive time |
| `arbitration_id` | `uint32_t` | 0x000–0x7FF (standard) or 0x00000000–0x1FFFFFFF (extended) | CAN arbitration ID |
| `type` | `FrameType` | One of: `Standard`, `Extended`, `FD`, `Error`, `Remote` | Frame type discriminator |
| `dlc` | `uint8_t` | 0–8 (classic CAN), 0–64 (CAN FD) | Data Length Code as observed on bus |
| `data` | `uint8_t[64]` | First `dlc` bytes valid; remainder zero | Raw payload bytes, unmodified |
| `channel_id` | `uint8_t` | 0–255 | Logical channel identifier |

**Validation rules**:
- If `type == Standard`, then `arbitration_id ≤ 0x7FF`
- If `type == Extended`, then `arbitration_id ≤ 0x1FFFFFFF`
- If `type != FD`, then `dlc ≤ 8`
- `data` bytes beyond index `dlc - 1` MUST be zero

**State transitions**: None — `CanFrame` is a value type, created once and never modified.

---

### FrameType

Enumeration of CAN frame types.

| Value | Numeric | Description |
|-------|:---:|-------------|
| `Standard` | 0 | 11-bit arbitration ID, classic CAN |
| `Extended` | 1 | 29-bit arbitration ID, classic CAN |
| `FD` | 2 | CAN FD frame (future) |
| `Error` | 3 | Error frame reported by adapter |
| `Remote` | 4 | Remote Transmission Request |

---

### DeviceInfo

Describes a discovered J2534 provider/adapter (USB-connected only). Read-only after discovery.

| Field | Type | Constraints | Description |
|-------|------|-------------|-------------|
| `name` | `string` | Non-empty | Human-readable provider name (e.g., "Tactrix OpenPort 2.0") |
| `vendor` | `string` | Non-empty | Vendor name |
| `dll_path` | `string` | Valid filesystem path | Full path to J2534 DLL on host |
| `supports_can` | `bool` | | Provider advertises CAN protocol support |
| `supports_iso15765` | `bool` | | Provider advertises ISO 15765 support |

**Source**: Windows Registry under `HKLM\SOFTWARE\PassThruSupport.04.04\*`

---

### FilterRule

A single filter criterion. Multiple rules combine into a filter set.

| Field | Type | Constraints | Description |
|-------|------|-------------|-------------|
| `action` | `FilterAction` | `Pass` or `Block` | Whether matching frames are shown or hidden |
| `id_value` | `uint32_t` | Valid CAN ID range | Arbitration ID to match (also serves as range start when `is_range == true`) |
| `id_mask` | `uint32_t` | Default `0xFFFFFFFF` | Bitmask: `(frame.id & mask) == (id_value & mask)`. Ignored when `is_range == true` |
| `id_range_end` | `uint32_t` | Valid CAN ID, ≥ `id_value` | Upper bound (inclusive) for range filters. Only meaningful when `is_range == true` |
| `is_range` | `bool` | Default `false` | When true, matches `id_value <= frame.id <= id_range_end` instead of mask logic |

**Validation rules**:
- `id_mask` of `0xFFFFFFFF` means exact ID match
- `id_mask` of `0x00000000` means "match all" (no-op filter)
- When `is_range == true`: `id_range_end` MUST be ≥ `id_value`; `id_mask` is ignored

**Combination logic**: Filters are evaluated in order. Default action (no matching rule) is `Pass` (show all). If any `Block` rule matches, the frame is hidden. If `Pass` rules are present, only frames matching at least one `Pass` rule are shown.

---

### FilterAction

| Value | Description |
|-------|-------------|
| `Pass` | Frame is included in output |
| `Block` | Frame is excluded from output |

---

### OperatingMode

Determines what operations are permitted on the channel.

| Value | Description | TX Allowed | MVP |
|-------|-------------|:---:|:---:|
| `Passive` | Receive only | No | Yes (default) |
| `ActiveQuery` | Diagnostic request/response | Yes (gated) | No |
| `ActiveInject` | Arbitrary frame transmission | Yes (gated) | No |

**State transitions**:
```
Passive ──(explicit user action + confirmation)──▶ ActiveQuery
Passive ──(explicit user action + confirmation)──▶ ActiveInject
ActiveQuery ──(user action)──▶ Passive
ActiveInject ──(user action)──▶ Passive
```
Transition from `Passive` to any active mode requires:
1. Explicit CLI flag (`--active`) or GUI toggle
2. User confirmation (CLI prompt or GUI dialog)
3. Entry logged to capture stream with `[MODE CHANGE]` marker

---

### SessionStatus

Aggregate runtime state. Updated atomically by `CaptureService`.

| Field | Type | Constraints | Description |
|-------|------|-------------|-------------|
| `mode` | `OperatingMode` | Default `Passive` | Current operating mode |
| `provider_name` | `string` | | Connected provider name, or empty |
| `adapter_name` | `string` | | Adapter hardware name, or empty |
| `bitrate` | `uint32_t` | 0 if no channel | Configured CAN bitrate in bps |
| `channel_open` | `bool` | | Whether a CAN channel is currently open |
| `recording` | `bool` | | Whether frame recording is active |
| `recording_file` | `string` | | Current recording file path, or empty |
| `frames_received` | `uint64_t` | Monotonically increasing | Total frames received in session |
| `frames_transmitted` | `uint64_t` | Monotonically increasing | Total frames transmitted (0 in passive) |
| `errors` | `uint64_t` | Monotonically increasing | Total errors observed |
| `dropped` | `uint64_t` | Monotonically increasing | Frames reported as dropped by adapter |
| `session_start` | `time_point` | Set on channel open | When the current session started |
| `active_filters` | `vector<FilterRule>` | | Currently applied filter rules |

**State transitions**:
```
Disconnected ──(connect)──▶ Connected
Connected ──(openChannel)──▶ ChannelOpen
ChannelOpen ──(startCapture)──▶ Capturing
Capturing ──(startRecord)──▶ Recording
Recording ──(stopRecord)──▶ Capturing
Capturing ──(stopCapture)──▶ ChannelOpen
ChannelOpen ──(closeChannel)──▶ Connected
Connected ──(disconnect)──▶ Disconnected
```

---

### RecordingSession

Metadata for a completed or active recording.

| Field | Type | Constraints | Description |
|-------|------|-------------|-------------|
| `file_path` | `string` | Valid path | Output file location |
| `format` | `LogFormat` | `ASC`, `JSONL`, or `CSV` | Selected output format |
| `start_time_utc` | `string` | ISO 8601 | When recording started |
| `start_time_unix_us` | `uint64_t` | | Unix microseconds at start |
| `end_time_utc` | `string` | ISO 8601, empty if active | When recording ended |
| `frame_count` | `uint64_t` | | Frames written to file |
| `adapter_name` | `string` | | Adapter in use during recording |
| `bitrate` | `uint32_t` | | CAN bitrate during recording |
| `filters_at_start` | `vector<FilterRule>` | | Filters active when recording began |

---

### LogFormat

| Value | Description | Extension |
|-------|-------------|-----------|
| `ASC` | Vector ASC text format | `.asc` |
| `JSONL` | JSON Lines (one object per line) | `.jsonl` |
| `CSV` | Comma-separated values (future) | `.csv` |

---

### Log File Version Markers

Each log file MUST start with a version marker so readers can detect format mismatches (spec.md edge case: "future-version log file opened").

**ASC format**: First line MUST be a comment containing the version string:
```
; CANmatik ASC v1.0
```
Readers MUST reject files where the major version exceeds their supported range.

**JSONL format**: First line MUST be a metadata object (not a frame):
```json
{"_meta":true,"format":"canmatik-jsonl","version":"1.0","created_utc":"2026-03-08T14:30:00Z"}
```
Readers MUST check `version` before parsing frame lines. Unknown major versions trigger a clear version-mismatch error.

---

### Label

A user-assigned human-readable name for a CAN arbitration ID, supporting incremental reverse engineering per Constitution Principle VI and FR-021.

| Field | Type | Constraints | Description |
|-------|------|-------------|-------------|
| `arbitration_id` | `uint32_t` | Valid CAN ID range; unique within store | The arbitration ID to label |
| `label` | `string` | Non-empty, max 64 characters | Human-readable name (e.g., "Engine RPM") |
| `created_utc` | `string` | ISO 8601 | When this label was first created |

**Persistence**: Stored as a JSON array in `labels.json` (working directory or `--config`-relative). Example:

```json
[
  {"arbitration_id": "0x7E8", "label": "Engine RPM", "created_utc": "2026-03-08T14:30:00Z"},
  {"arbitration_id": "0x3B0", "label": "ABS Wheel Speed", "created_utc": "2026-03-08T14:32:15Z"}
]
```

**State transitions**: Labels are created or updated via `canmatik label set <id> <name>` and deleted via `canmatik label remove <id>`. The store is loaded on startup and saved after each mutation.

---

### TransportError

Structured error from the transport layer.

| Field | Type | Description |
|-------|------|-------------|
| `code` | `int32_t` | J2534 status code (or mock error code) |
| `message` | `string` | Human-readable description |
| `source` | `string` | Originating function (e.g., "PassThruConnect") |
| `recoverable` | `bool` | Whether the session can continue after this error |

---

### Config

Application configuration. Loaded from file, overridden by CLI flags.

| Field | Type | Default | Description |
|-------|------|---------|-------------|
| `provider` | `string` | `""` (auto-select first) | J2534 provider name |
| `bitrate` | `uint32_t` | `500000` | CAN bitrate in bps |
| `mode` | `OperatingMode` | `Passive` | Startup operating mode |
| `filters` | `vector<FilterRule>` | `[]` | Startup filters |
| `output_format` | `LogFormat` | `ASC` | Default recording format |
| `output_directory` | `string` | `"./captures"` | Default recording directory |
| `gui_launch` | `bool` | `false` | Whether to launch GUI by default |
| `gui_font_size` | `uint32_t` | `14` | ImGui font size |
| `mock_enabled` | `bool` | `false` | Use mock backend |
| `mock_frame_rate` | `uint32_t` | `100` | Frames/sec in mock mode |
| `mock_trace_file` | `string` | `""` | Replay this file as mock input |
| `verbose` | `bool` | `false` | Enable verbose diagnostic output (TinyLog `TraceSeverity::verbose`) |
| `debug` | `bool` | `false` | Enable debug-level file logging (TinyLog `TraceType::file`) |
| `log_file` | `string` | `"canmatik.log"` | Diagnostic log file path (TinyLog FileTracer) |
| `log_max_file_size` | `uint32_t` | `10485760` | Max log file size in bytes before rotation (0 = no limit) |
| `log_max_backups` | `uint32_t` | `5` | Max rotated backup files to keep (0 = unlimited) |
| `log_compress` | `bool` | `true` | Compress rotated log files with zstd |

**JSON config path → struct field mapping**:

| JSON Path | Config Field | Notes |
|-----------|-------------|-------|
| `provider` | `provider` | Direct |
| `bitrate` | `bitrate` | Direct |
| `mode` | `mode` | String → enum |
| `filters[]` | `filters` | Array of FilterRule objects |
| `output.format` | `output_format` | Nested → flat |
| `output.directory` | `output_directory` | Nested → flat |
| `gui.launch` | `gui_launch` | Nested → flat |
| `gui.font_size` | `gui_font_size` | Nested → flat |
| `mock.enabled` | `mock_enabled` | Nested → flat |
| `mock.frame_rate` | `mock_frame_rate` | Nested → flat |
| `mock.trace_file` | `mock_trace_file` | Nested → flat |
| `logging.file` | `log_file` | Nested → flat |
| `logging.max_file_size` | `log_max_file_size` | Nested → flat |
| `logging.max_backups` | `log_max_backups` | Nested → flat |
| `logging.compress` | `log_compress` | Nested → flat |

CLI flags `--verbose` and `--debug` are not persisted in the JSON file; they set `verbose` and `debug` fields directly.

---

## Relationships

```
DeviceInfo          1 ──── discovered by ──── 1  IDeviceProvider
IDeviceProvider     1 ──── produces ────────── *  DeviceInfo
IDeviceProvider     1 ──── creates ─────────── 1  IChannel (per connect)
IChannel            1 ──── produces ────────── *  CanFrame (continuous stream)
CanFrame            * ──── evaluated by ────── 1  FilterEngine (contains FilterRules)
FilterRule          * ──── grouped in ──────── 1  FilterEngine
CanFrame            * ──── written by ──────── 1  ILogWriter (during recording)
ILogWriter          1 ──── produces ────────── 1  Log File
Log File            1 ──── read by ─────────── 1  ILogReader
ILogReader          1 ──── produces ────────── *  CanFrame (replay stream)
SessionStatus       1 ──── aggregates ──────── 1  Session lifecycle
RecordingSession    1 ──── tracks ──────────── 1  Log File output
Config              1 ──── configures ──────── 1  Application startup
```
