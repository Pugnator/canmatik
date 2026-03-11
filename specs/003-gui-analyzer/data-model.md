# Data Model: GUI Analyzer

**Feature Branch**: `003-gui-analyzer`
**Created**: 2026-03-11
**Input**: spec.md, plan.md

---

## Entity Overview

```
                         canmatik_gui.json
                               │
                         GuiSettings (JSON)
                               │
           ┌───────────────────┼───────────────────┐
           ▼                   ▼                   ▼
    ConnectionConfig      BufferConfig        PidFilterSet
           │                   │
           ▼                   ▼
    CaptureController    FrameCollector
    ReplayController          │
           │          ┌───────┼────────┐
           ▼          ▼       ▼        ▼
    CaptureService  RingBuf  PerIdState  WatchdogSet
           │          │       │          │
           ▼          ▼       ▼          ▼
    SnapshotRow[]  SaveFile  DiffInfo   WatchdogRow[]
           │
    ┌──────┼──────┐
    ▼      ▼      ▼
  Table  StatusBar  ObdDashboard
```

---

## Entities

### GuiSettings

Persisted to `canmatik_gui.json` on exit, loaded on startup.

| Field | Type | Default | Description |
|-------|------|---------|-------------|
| `provider` | `string` | `""` | Selected J2534 provider name (empty = auto) |
| `bitrate` | `uint32_t` | `500000` | CAN bitrate in bps |
| `mock_enabled` | `bool` | `false` | Use MockProvider |
| `buffer_capacity` | `uint32_t` | `100000` | RAM ring buffer capacity (frames) |
| `change_filter_n` | `uint32_t` | `1` | Compare against last N transmissions |
| `show_changed_only` | `bool` | `false` | Change-only filter state |
| `obd_mode` | `ObdDisplayMode` | `OBD_AND_BROADCAST` | OBD/broadcast filter |
| `obd_pids` | `vector<uint8_t>` | `[0x0C,0x0D,0x05]` | PIDs to query |
| `obd_interval_ms` | `uint32_t` | `500` | OBD query interval |
| `window_width` | `int` | `1024` | Window width |
| `window_height` | `int` | `720` | Window height |
| `last_file_path` | `string` | `""` | Last opened log file (for replay) |

### ObdDisplayMode (enum)

```cpp
enum class ObdDisplayMode : uint8_t {
    OBD_AND_BROADCAST = 0,   // Show all frames (default)
    OBD_ONLY          = 1,   // Show only OBD request/response (0x7DF, 0x7E0-0x7EF)
    BROADCAST_ONLY    = 2,   // Show only non-OBD frames
};
```

### DataSource (enum)

```cpp
enum class DataSource : uint8_t {
    LIVE = 0,   // Real-time from J2534/mock
    FILE = 1,   // Replay from .asc/.jsonl
};
```

### PlaybackState (enum)

```cpp
enum class PlaybackState : uint8_t {
    STOPPED  = 0,
    PLAYING  = 1,
    PAUSED   = 2,
};
```

### FrameCollector

Central data structure owned by the GUI. Thread-safe (mutex-guarded). Receives frames from CaptureService (live) or ReplayController (file).

| Field | Type | Description |
|-------|------|-------------|
| `ring_buffer` | `vector<CanFrame>` | Fixed-capacity ring buffer (all frames, pre-filter) |
| `ring_head` | `size_t` | Next write position |
| `ring_count` | `size_t` | Number of valid frames in buffer |
| `per_id_state` | `unordered_map<uint32_t, PerIdState>` | Change-tracking state per arb ID |
| `watchdog_ids` | `unordered_set<uint32_t>` | Set of watched arb IDs |
| `mu` | `mutex` | Protects all fields |

### PerIdState

Tracks per-arb-ID state for change detection and display.

| Field | Type | Description |
|-------|------|-------------|
| `arb_id` | `uint32_t` | Arbitration ID |
| `dlc` | `uint8_t` | Last seen DLC |
| `data` | `array<uint8_t, 8>` | Last seen payload |
| `history` | `deque<array<uint8_t,8>>` | Last N payloads (for N-transmission comparison) |
| `history_dlc` | `deque<uint8_t>` | Last N DLCs |
| `changed_mask` | `array<bool, 8>` | Per-byte changed flag |
| `is_new` | `bool` | True if arb ID seen for first time |
| `dlc_changed` | `bool` | DLC differs from previous |
| `last_seen_ts` | `double` | Relative timestamp of last frame |
| `update_count` | `uint64_t` | Total frames received for this ID |
| `is_watched` | `bool` | True if this ID is in watchdog set |

### MessageRow

Snapshot row passed to ImGui for rendering. Produced by `FrameCollector::snapshot()`.

| Field | Type | Description |
|-------|------|-------------|
| `arb_id` | `uint32_t` | Arbitration ID |
| `dlc` | `uint8_t` | Data length code |
| `data` | `array<uint8_t, 8>` | Payload bytes |
| `changed` | `array<bool, 8>` | Per-byte diff flags |
| `is_new` | `bool` | First-seen flag |
| `dlc_changed` | `bool` | DLC change flag |
| `last_seen` | `double` | Relative timestamp |
| `update_count` | `uint64_t` | Frame count for this ID |
| `is_watched` | `bool` | Watchdog flag |
| `selected` | `bool` | User-selected flag |

### WatchdogRow

Subset of `MessageRow` for the watchdog panel. Same fields.

### ObdPidRow

Row for the OBD Data tab.

| Field | Type | Description |
|-------|------|-------------|
| `pid` | `uint8_t` | PID number |
| `name` | `string` | Human-readable name |
| `value` | `double` | Decoded value |
| `unit` | `string` | Unit string |
| `raw_bytes` | `string` | Hex string of raw response bytes |
| `value_changed` | `bool` | True if value differs from previous |
| `history` | `deque<pair<double,double>>` | (timestamp, value) pairs for graphing |

### ReplayState

Playback state for the replay controller.

| Field | Type | Description |
|-------|------|-------------|
| `frames` | `vector<CanFrame>` | All frames loaded from file |
| `current_index` | `size_t` | Current playback position |
| `state` | `PlaybackState` | Playing / paused / stopped |
| `speed_multiplier` | `float` | 1.0, 2.0, 4.0, 8.0 |
| `loop` | `bool` | Loop-at-end flag |
| `elapsed_us` | `uint64_t` | Logical playback time |

---

## Relationships

```
GuiSettings  1 ──── 1  GuiApp (owns settings, persists on exit)
GuiApp       1 ──── 1  FrameCollector (central data store)
GuiApp       1 ──── 1  CaptureController (live capture lifecycle)
GuiApp       1 ──── 1  ReplayController (file replay lifecycle)
GuiApp       1 ──── 1  ObdController (OBD query lifecycle)

FrameCollector 1 ──── *  PerIdState (one per unique arb ID)
FrameCollector 1 ──── *  CanFrame (ring buffer entries)
FrameCollector 1 ──── *  MessageRow (snapshot output)

CaptureController uses SessionService + CaptureService (from canmatik_services)
ReplayController  uses AscReader / JsonlReader (from canmatik_logging)
ObdController     uses OBDSession (from canmatik_obd)
```

---

## Serialization

### canmatik_gui.json

```json
{
  "provider": "",
  "bitrate": 500000,
  "mock_enabled": false,
  "buffer_capacity": 100000,
  "change_filter_n": 1,
  "show_changed_only": false,
  "obd_mode": "obd_and_broadcast",
  "obd_pids": ["0x0C", "0x0D", "0x05"],
  "obd_interval_ms": 500,
  "window_width": 1024,
  "window_height": 720,
  "last_file_path": ""
}
```
