# Feature Specification: GUI Analyzer — CAN Traffic & OBD-II Display

**Feature Branch**: `003-gui-analyzer`
**Created**: 2026-03-11
**Status**: Draft
**Depends on**: `001-can-bus-scanner` (CAN transport, session/capture services), `002-obd-diagnostics` (PID decoding, OBD session)
**Input**: User description: "GUI for analyzing and displaying CAN and OBD-II data with live/replay, watchdog, change highlighting, and configurable capture buffer"

---

## Feature Overview

A standalone Windows GUI application (`canmatik_gui.exe`) built with ImGui (Win32 + OpenGL3) that provides a visual interface for CAN bus analysis and OBD-II diagnostics. The GUI reuses the same core libraries as the CLI (`canmatik_core`, `canmatik_platform`, `canmatik_mock`, `canmatik_services`, `canmatik_logging`, `canmatik_obd`) — no duplicate protocol logic.

The application is organized into three tabs:

1. **CAN Messages** — live or replayed CAN traffic with playback controls, change-only filtering, byte-level diff highlighting, watchdog alerts, and a configurable RAM capture buffer.
2. **OBD Data** — decoded OBD-II PID values in a dashboard layout (gauges, tables, graphs).
3. **Settings** — connection parameters, interface selection, PID filter configuration, capture buffer size, and general preferences.

**Key constraint**: The GUI is a **display and analysis** tool. It inherits the CLI's safety posture — passive by default, active OBD queries require explicit user action.

---

## Scope

### In Scope

- Separate `canmatik_gui.exe` binary (WIN32 subsystem, no console)
- ImGui (v1.91.8) with Win32 + OpenGL3 backend
- Three-tab layout: CAN Messages, OBD Data, Settings
- Live CAN capture from J2534 hardware or mock backend
- Replay of pre-recorded `.asc` / `.jsonl` log files
- Playback controls: play, pause, stop, rewind, fast-forward (rewind/FF for replay only)
- Loop checkbox for replay mode
- Mouse-selectable message rows
- Change-only display mode: show only messages changed since last transmission or last N transmissions (user-configurable N)
- OBD diagnostic request modes: OBD-only, broadcast-only, OBD + broadcast
- Per-ID watchdog: user sets/clears a watchdog on a specific CAN ID; watchdog-flagged messages appear in a dedicated panel
- Byte-level diff highlighting (changed bytes colored differently)
- RAM capture buffer with configurable size; save-to-file on demand
- Settings tab: J2534 provider selection, bitrate, mock toggle, PID filters, buffer size

### Out of Scope

- Multi-window / detachable panels (single window, tabbed layout)
- DBC/ARXML database loading and signal-level decoding (future spec)
- Network-attached remote streaming
- Theming / skinning beyond ImGui default dark theme
- Platform ports (Linux, macOS) — Windows-only for J2534 DLL compatibility
- Log file editing / annotation in the GUI

---

## User Personas

Inherited from specs 001 and 002. The GUI primarily serves:

### Persona 1 — Vehicle Hacker / Reverse Engineer
Prefers the GUI for long analysis sessions where visual change highlighting and watchdog panels accelerate pattern discovery compared to terminal scrolling.

### Persona 2 — Automotive Hobbyist
Finds the GUI more approachable than the CLI. Uses playback controls and the OBD dashboard to understand their vehicle without deep protocol expertise.

### Persona 3 — Embedded Developer
Uses the GUI to visually verify their device's CAN output in real time, setting watchdogs on their device's specific IDs.

---

## User Scenarios & Testing

### US-GUI-1 — View Live CAN Traffic (Priority: P1)

A user launches the GUI, selects a J2534 provider (or mock), connects, and sees CAN frames streaming in the CAN Messages tab.

**Acceptance Scenarios**:

1. **Given** the GUI is launched, **When** the user selects a provider and clicks Connect in the Settings tab, **Then** the CAN Messages tab begins displaying live frames with arb ID, DLC, payload bytes, and relative timestamp.
2. **Given** live capture is active, **When** the user clicks Pause, **Then** the display freezes but the RAM buffer continues recording. Clicking Play resumes display.
3. **Given** live capture is active, **When** the user clicks Stop, **Then** capture stops and the session summary (frame count, duration) is shown.
4. **Given** live capture is active, **When** Rewind or Fast-Forward buttons are visible, **Then** they are disabled/grayed out (only available in replay mode).

---

### US-GUI-2 — Replay Pre-Recorded Data (Priority: P1)

A user loads a `.asc` or `.jsonl` log file and replays it with playback controls.

**Acceptance Scenarios**:

1. **Given** no live connection, **When** the user opens a log file via File menu or Settings, **Then** the CAN Messages tab shows the first frame and Rewind/Fast-Forward/Loop controls become active.
2. **Given** replay is active, **When** the user clicks Pause, **Then** playback stops at the current position. Play resumes from the paused position.
3. **Given** replay is active, **When** the user clicks Rewind, **Then** playback jumps to the beginning.
4. **Given** replay is active, **When** the user clicks Fast-Forward, **Then** playback speed doubles (2×, 4×, 8× with repeated clicks, cycling back to 1×).
5. **Given** replay is active with Loop enabled, **When** the last frame is reached, **Then** playback restarts from the beginning automatically.
6. **Given** replay is active, **When** the user clicks Stop, **Then** replay ends and the session summary is shown.

---

### US-GUI-3 — Change-Only Filtering (Priority: P1)

A user enables change-only mode to suppress unchanged CAN messages and focus on dynamic data.

**Acceptance Scenarios**:

1. **Given** the CAN Messages tab is displaying frames, **When** the user enables "Show changed only", **Then** only messages whose payload changed since the last transmission of that arb ID are displayed.
2. **Given** "Show changed only" is active, **When** the user sets N = 5 in the "last N transmissions" spinner, **Then** a message is shown if its payload differs from any of the previous 5 transmissions of that arb ID.
3. **Given** "Show changed only" is active, **When** a message's payload has changed bytes, **Then** the changed bytes are highlighted in a distinct color (red) while unchanged bytes remain in the default color.
4. **Given** "Show changed only" is active, **When** a new arb ID appears for the first time, **Then** it is shown highlighted in green as a new ID.

---

### US-GUI-4 — Watchdog on CAN IDs (Priority: P2)

A user sets a watchdog on one or more CAN IDs to isolate and monitor specific messages.

**Acceptance Scenarios**:

1. **Given** the CAN Messages tab is showing traffic, **When** the user right-clicks a message row and selects "Add Watchdog", **Then** that arb ID is added to the watchdog list.
2. **Given** a watchdog is set on ID 0x7E8, **When** a frame with ID 0x7E8 arrives, **Then** it appears in both the main table and the Watchdog panel (a separate sub-panel within the CAN Messages tab).
3. **Given** a watchdog is active, **When** the user clears the watchdog (right-click → Remove Watchdog or via the Watchdog panel), **Then** the ID is removed from the Watchdog panel.
4. **Given** the Watchdog panel is visible, **When** a watched frame's payload changes, **Then** changed bytes are highlighted the same way as in the main table.

---

### US-GUI-5 — OBD Diagnostic Mode Selection (Priority: P2)

A user selects what type of CAN traffic to display alongside OBD data.

**Acceptance Scenarios**:

1. **Given** the CAN Messages tab, **When** the user selects "OBD Only" from the mode dropdown, **Then** only OBD diagnostic request/response frames (0x7DF, 0x7E0–0x7EF) are shown.
2. **Given** the user selects "Broadcast Only", **Then** only non-OBD broadcast CAN frames are shown.
3. **Given** the user selects "OBD + Broadcast", **Then** all frames are shown (default).
4. **Given** "OBD Only" is selected, **When** the user enables OBD streaming, **Then** the tool starts sending Mode $01 queries per the configured PID list and displays request/response pairs.

---

### US-GUI-6 — OBD Data Dashboard (Priority: P2)

A user views decoded OBD-II data in the OBD Data tab.

**Acceptance Scenarios**:

1. **Given** an active OBD streaming session, **When** the user switches to the OBD Data tab, **Then** decoded PID values are displayed in a table with columns: PID, Name, Value, Unit, Raw Bytes.
2. **Given** OBD data is streaming, **When** a PID value changes, **Then** the new value is highlighted briefly (flash or color).
3. **Given** the OBD Data tab is active, **When** the user selects a PID row, **Then** a detail panel shows the PID's history graph (value over time).

---

### US-GUI-7 — RAM Capture Buffer and Save (Priority: P1)

All intercepted data is stored in a configurable RAM buffer that the user can save to disk.

**Acceptance Scenarios**:

1. **Given** capture is active (live or replay), **When** frames arrive, **Then** all frames (pre-filter) are stored in the RAM buffer up to the configured capacity.
2. **Given** the buffer capacity is 100,000 frames and 100,001 frames have arrived, **Then** the oldest frame is discarded (ring buffer behavior).
3. **Given** the user clicks "Save Buffer" in the toolbar, **Then** a file save dialog opens, and all buffered frames are written to the selected file in the chosen format (ASC or JSONL).
4. **Given** the Settings tab, **When** the user changes the buffer size (e.g., from 100K to 500K frames), **Then** the buffer is resized (preserving existing data up to the new limit).

---

### US-GUI-8 — Settings Tab (Priority: P1)

The Settings tab provides all configuration for connection and behavior.

**Acceptance Scenarios**:

1. **Given** the Settings tab, **When** the user opens it, **Then** the following sections are visible: Connection, PID Filters, Capture Buffer, Interface.
2. **Given** the Connection section, **When** the user selects a J2534 provider from the dropdown and sets bitrate, **Then** the values are applied when "Connect" is clicked.
3. **Given** the Connection section, **When** the user enables "Mock mode", **Then** the MockProvider backend is used instead of real hardware.
4. **Given** the PID Filters section, **When** the user adds/removes PID hex values, **Then** only the selected PIDs are queried during OBD streaming.
5. **Given** the Capture Buffer section, **When** the user sets buffer size to 200,000, **Then** the RAM buffer capacity is updated.
6. **Given** the Interface section, **When** the user selects a CAN bitrate (125K, 250K, 500K, 1M), **Then** the value is used for the next channel open.

---

## Functional Requirements

### FR-GUI-001: Tab Layout
The main window shall contain a tab bar with three tabs: "CAN Messages", "OBD Data", "Settings".

### FR-GUI-002: Data Source Selection
The CAN Messages tab shall provide controls to select between "Live" data (from hardware/mock) and "File" data (from a log file).

### FR-GUI-003: Playback Controls
The CAN Messages tab shall provide Play, Pause, Stop buttons for both live and replay modes. Rewind and Fast-Forward buttons shall be enabled only in replay mode.

### FR-GUI-004: Loop Replay
A "Loop" checkbox shall be available in replay mode. When enabled, playback restarts automatically after the last frame.

### FR-GUI-005: Message Selection
The user shall be able to select a message row with a mouse click. Selected messages shall be visually highlighted.

### FR-GUI-006: Change-Only Filter
A "Show changed only" checkbox shall filter the display to show only messages whose payload differs from the last (or last N) transmissions of the same arb ID. N is configurable via a spinner (default 1, range 1–100).

### FR-GUI-007: OBD Mode Selector
A dropdown shall allow the user to select: "OBD + Broadcast" (default), "OBD Only", "Broadcast Only". This filters the message display and optionally starts OBD query streaming.

### FR-GUI-008: Watchdog
The user shall be able to set a watchdog on a CAN arb ID via right-click context menu. Watched messages appear in a separate Watchdog sub-panel. Watchdogs can be cleared individually or all at once.

### FR-GUI-009: Byte-Level Diff Highlighting
In the CAN Messages table, bytes that changed since the previous transmission of the same arb ID shall be highlighted in red. New IDs shall be highlighted in green. DLC changes shall be highlighted in yellow.

### FR-GUI-010: RAM Capture Buffer
All received frames shall be stored in a ring buffer in RAM. The buffer capacity is configurable in the Settings tab (default: 100,000 frames). A "Save Buffer" action writes the buffer contents to a file.

### FR-GUI-011: OBD Data Tab
The OBD Data tab shall display decoded PID values in a table. A value-over-time graph shall be available for selected PIDs.

### FR-GUI-012: Settings Persistence
Settings (provider, bitrate, mock mode, buffer size, PID filters) shall be saved to `canmatik_gui.json` on exit and restored on launch.

### FR-GUI-013: Status Bar
A status bar at the bottom of the window shall display: connection state, frame count, error count, buffer usage (frames / capacity), and elapsed time.

---

## Non-Functional Requirements

### NFR-GUI-001: Frame Rate
The GUI shall maintain ≥ 30 FPS during live capture at full CAN bus utilization (~8000 frames/sec on a 500 kbps bus).

### NFR-GUI-002: Latency
Frame-to-display latency shall be < 100 ms.

### NFR-GUI-003: Memory
Resident memory shall stay below 200 MB with a 100K-frame buffer.

### NFR-GUI-004: Startup Time
The application shall reach the main window in < 2 seconds on a typical machine.

### NFR-GUI-005: Static Linking
The GUI binary shall be statically linked (no DLL dependencies except system DLLs and J2534 provider DLLs).

---

## Dependencies

| Dependency | Version | Purpose |
|---|---|---|
| ImGui | 1.91.8 | Immediate-mode GUI framework |
| OpenGL | 2.x+ | Rendering backend |
| canmatik_core | in-tree | CAN frame types, filters, timestamps |
| canmatik_platform | in-tree | J2534 provider/channel |
| canmatik_mock | in-tree | Mock backend |
| canmatik_services | in-tree | SessionService, CaptureService |
| canmatik_logging | in-tree | ASC/JSONL readers and writers |
| canmatik_obd | in-tree | PID decoding, OBD session |

---

## Open Questions

1. Should the OBD Data tab include a "freeze frame" (Mode $02) display panel?
2. Should the watchdog panel support alerts (audio beep / window flash) in addition to visual display?
3. Should there be an "Export Selection" feature to save only filtered/selected messages?
