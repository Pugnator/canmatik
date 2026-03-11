# Implementation Plan: GUI Analyzer

**Branch**: `003-gui-analyzer` | **Date**: 2026-03-11 | **Spec**: [spec.md](spec.md)
**Depends on**: `001-can-bus-scanner`, `002-obd-diagnostics`

---

## Summary

Build `canmatik_gui.exe`, a standalone ImGui-based Windows application for
visual CAN bus analysis and OBD-II diagnostics. Three tabs (CAN Messages, OBD
Data, Settings) provide live/replay viewing, change-only filtering with
byte-level diff coloring, per-ID watchdogs, OBD mode selection, a configurable
RAM capture buffer with save-to-file, and a decoded OBD dashboard.

The GUI shares the same static libraries as the CLI — no protocol logic is
duplicated. The architecture follows an **App → Controller → Service → Panel**
layering: the `GuiApp` owns the ImGui lifecycle and window, controllers
(`CaptureController`, `ReplayController`, `ObdController`) orchestrate backends,
and panels render ImGui widgets from thread-safe data snapshots.

---

## Technical Context

**Existing stack**: C++20, MinGW-w64 i686, CMake+Ninja, static linking
**GUI framework**: ImGui 1.91.8 (FetchContent, already wired in CMakeLists.txt)
**Backend**: Win32 + OpenGL3 (WGL, no GLEW needed for legacy GL)
**Binary**: `canmatik_gui.exe` (WIN32 subsystem — no console)
**Existing scaffold**: `src/gui/gui_main.cpp` (WinMain, ImGui init), `src/gui/panels/sniff_panel.h/.cpp` (SniffCollector + render)
**Threading**: CaptureService reader thread → SPSC queue → GUI thread drains each frame; all panel data access through mutex-guarded snapshots

---

## Architecture

### Layer Diagram

```
┌─────────────────────────────────────────────────────────┐
│                    canmatik_gui.exe                      │
├──────────┬──────────────────┬───────────────────────────┤
│  ImGui   │    Controllers   │        Panels             │
│  Loop    │                  │                           │
│          │  CaptureCtrl     │  CAN Messages Panel       │
│  WinMain │  ReplayCtrl      │    ├─ Message Table       │
│  OpenGL  │  ObdCtrl         │    ├─ Watchdog Panel      │
│  WndProc │  SettingsCtrl    │    └─ Playback Controls   │
│          │                  │  OBD Data Panel           │
│          │                  │  Settings Panel           │
│          │                  │  Status Bar               │
├──────────┴──────────────────┴───────────────────────────┤
│                   Shared Libraries                       │
│  canmatik_core  canmatik_platform  canmatik_mock         │
│  canmatik_services  canmatik_logging  canmatik_obd       │
└─────────────────────────────────────────────────────────┘
```

### Source Layout

```
src/gui/
├── gui_main.cpp              # WinMain, WGL setup, ImGui init, main loop (EXISTS)
├── gui_app.h                 # GuiApp class: owns controllers, tabs, runs loop
├── gui_app.cpp
├── gui_state.h               # Shared GUI state: connection, data source, playback
├── gui_settings.h            # GuiSettings struct + JSON persistence
├── gui_settings.cpp
├── controllers/
│   ├── capture_controller.h  # Live capture lifecycle (SessionService + CaptureService)
│   ├── capture_controller.cpp
│   ├── replay_controller.h   # Log file replay with playback controls
│   ├── replay_controller.cpp
│   ├── obd_controller.h      # OBD query session orchestration
│   └── obd_controller.cpp
├── panels/
│   ├── can_messages_panel.h   # Tab1: message table + toolbar + watchdog sub-panel
│   ├── can_messages_panel.cpp
│   ├── obd_data_panel.h      # Tab2: decoded PID table + history graph
│   ├── obd_data_panel.cpp
│   ├── settings_panel.h      # Tab3: connection, PID filters, buffer config
│   ├── settings_panel.cpp
│   ├── watchdog_panel.h      # Sub-panel: watched ID messages
│   ├── watchdog_panel.cpp
│   ├── status_bar.h          # Bottom status bar
│   ├── status_bar.cpp
│   ├── sniff_panel.h         # (EXISTS — will be refactored into can_messages_panel)
│   └── sniff_panel.cpp       # (EXISTS — collector logic reused)
└── widgets/
    ├── playback_toolbar.h     # Reusable play/pause/stop/rewind/ff/loop widget
    └── playback_toolbar.cpp
```

### Data Flow

```
                    ┌─────────────┐
    Live:           │  IChannel   │──▶ CaptureService reader thread
                    └─────────────┘          │
                                        SPSC Queue
                                             │
    Replay:    ┌─────────────┐          GUI thread drain()
               │  LogReader   │──────────────┤
               └─────────────┘               │
                                    ┌────────▼────────┐
                                    │  FrameCollector  │  (mutex-guarded)
                                    │  ├─ all_frames[] │  ←── RAM buffer (ring)
                                    │  ├─ per_id_state │  ←── change detection
                                    │  └─ watchdog_set │  ←── watched IDs
                                    └────────┬────────┘
                                             │ snapshot()
                                    ┌────────▼────────┐
                                    │  Panels (ImGui)  │
                                    └─────────────────┘
```

### RAM Capture Buffer

A ring buffer (`std::vector<CanFrame>`) owned by the `FrameCollector`. Capacity
set in Settings (default 100,000 frames, max 10,000,000). When full, oldest
frame is overwritten. The buffer stores **all** pre-filter frames — display
filtering only affects what the panel renders.

Save-to-file iterates the ring buffer in chronological order and writes via
`AscWriter` or `JsonlWriter` (existing logging library).

---

## Phase Plan

### Phase 1 — App Shell & Settings (Foundation)

- `GuiApp` class with tab bar, main loop orchestration
- `GuiSettings` with JSON persistence (`canmatik_gui.json`)
- Settings panel: provider dropdown, bitrate, mock toggle, buffer size
- Status bar: connection state, frame count
- Refactor existing `gui_main.cpp` to instantiate `GuiApp`

### Phase 2 — CAN Messages Tab (Core Feature)

- `FrameCollector`: thread-safe frame storage with per-ID state tracking, change detection, RAM ring buffer
- `CaptureController`: live capture lifecycle (connect, start, stop)
- `ReplayController`: log file loading and timed playback
- CAN Messages panel: message table with columns (ID, DLC, Data, Time, Count)
- Playback toolbar: play, pause, stop, rewind, ff, loop
- Data source selector: Live vs File
- Change-only filter checkbox + N spinner
- Byte-level diff coloring (red=changed, green=new, yellow=DLC)
- Message row selection
- Save buffer to file

### Phase 3 — Watchdog & OBD Mode

- Watchdog panel: right-click → Add/Remove Watchdog, dedicated sub-panel
- OBD mode dropdown: OBD Only, Broadcast Only, OBD + Broadcast
- `ObdController`: PID query orchestration (reuses `OBDSession`)
- OBD filter integration (0x7DF, 0x7E0–0x7EF ranges)

### Phase 4 — OBD Data Tab

- OBD Data panel: decoded PID table with value/unit/raw
- Value change highlighting (brief flash)
- PID history graph (ImGui line plot using value history ring buffer)
- PID filter config in Settings tab

### Phase 5 — Polish & Testing

- Keyboard shortcuts (Space=pause, Ctrl+S=save, Esc=stop)
- Window state persistence (size, position, column widths)
- Error dialogs for connection failures
- Integration testing with mock backend

---

## Constitution Check

| # | Principle | Status | Notes |
|---|-----------|:---:|-------|
| I | Read First, Transmit Later | PASS | Default mode is passive display. OBD queries require explicit user action via mode dropdown. |
| II | Open and Inspectable | PASS | All GUI code is open-source. Same libraries as CLI. |
| III | Vehicle-Agnostic Core | PASS | No brand-specific logic in GUI. |
| IV | Deterministic Logging | PASS | RAM buffer stores all frames faithfully. Save-to-file uses existing writers. |
| V | Safe by Design | PASS | GUI inherits CLI safety posture. No ECU write/flash. |
| VI | Incremental Reverse Engineering | PASS | Change-only mode and watchdogs support iterative analysis. |
| VII | CLI-First Foundation | PASS | GUI reuses CLI's service layer. No CLI-absent features. |
| VIII | Portable Architecture | PASS | GUI is a thin layer over shared libraries. |

---

## Risks & Mitigations

| Risk | Impact | Mitigation |
|---|---|---|
| ImGui rendering throughput at high frame rates | Dropped GUI frames | Throttle table to visible rows only; snapshot limits |
| RAM buffer memory pressure at large sizes | OOM on 32-bit process | Cap at ~10M frames (~640 MB); warn user in settings |
| OpenGL driver compatibility on old machines | Black screen / crash | Fallback to OpenGL 2.x; document minimum GPU requirements |
| Thread safety between capture reader and GUI | Data races | All shared state behind mutex; snapshot() copies before render |
