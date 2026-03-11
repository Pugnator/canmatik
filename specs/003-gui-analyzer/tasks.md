# Tasks: GUI Analyzer — CAN Traffic & OBD-II Display

**Input**: Design documents from `/specs/003-gui-analyzer/`
**Prerequisites**: plan.md (required), spec.md (required), data-model.md (required)
**Depends on**: Specs 001 and 002 fully implemented (194 tests passing)

**Tests**: Each phase includes manual verification via the GUI. Unit tests for FrameCollector, ReplayController, and settings persistence.

**Organization**: Tasks are grouped by plan.md phases. `[P]` = parallelizable, `[US-GUI-N]` = user story reference.

---

## Phase 1: App Shell & Settings (Foundation)

**Purpose**: Establish the GUI application skeleton — tab bar, settings persistence, status bar, clean entry point.

**Existing code**: `src/gui/gui_main.cpp` (WinMain scaffold), `src/gui/panels/sniff_panel.h/.cpp` (SniffCollector, render_sniff_panel)

- [x] T001 [US-GUI-8] Create `src/gui/gui_state.h` defining enums: `DataSource` (LIVE, FILE), `PlaybackState` (STOPPED, PLAYING, PAUSED), `ObdDisplayMode` (OBD_AND_BROADCAST, OBD_ONLY, BROADCAST_ONLY), and the `GuiState` struct holding current runtime state (data source, playback state, connection status, selected tab, speed multiplier, loop flag)
- [x] T002 [P] [US-GUI-8] Create `src/gui/gui_settings.h` and `src/gui/gui_settings.cpp` implementing `GuiSettings` struct with all fields from data-model.md; implement `load(path)` and `save(path)` using nlohmann/json for `canmatik_gui.json`; include defaults
- [x] T003 [US-GUI-8] Create `src/gui/gui_app.h` and `src/gui/gui_app.cpp` implementing `GuiApp` class: owns `GuiSettings`, `GuiState`, controller pointers (initially null), and a `render()` method that draws the ImGui tab bar ("CAN Messages", "OBD Data", "Settings") and calls per-tab render functions
- [x] T004 [US-GUI-8] Create `src/gui/panels/settings_panel.h` and `src/gui/panels/settings_panel.cpp` implementing the Settings tab: Connection section (provider dropdown populated from `SessionService::scan()`, bitrate combo 125K/250K/500K/1M, mock checkbox, Connect/Disconnect button), PID Filters section (hex input + add/remove list), Capture Buffer section (capacity input with validation 1K–10M), and Interface section
- [x] T005 Create `src/gui/panels/status_bar.h` and `src/gui/panels/status_bar.cpp` rendering a bottom status bar with: connection state icon/text, frame counter, error counter, buffer usage (frames/capacity), elapsed time
- [x] T006 Refactor `src/gui/gui_main.cpp`: replace inline mock setup and ImGui render loop with `GuiApp` instantiation; `GuiApp::init()` loads settings, creates ImGui context; `GuiApp::render()` called each frame; `GuiApp::shutdown()` saves settings
- [x] T007 Update `CMakeLists.txt`: add all new `src/gui/*.cpp` and `src/gui/**/*.cpp` files to `canmatik_gui` target sources; ensure `nlohmann_json::nlohmann_json` is linked (for settings JSON)
- [x] T008 [P] Unit test: `tests/unit/test_gui_settings.cpp` — verify `GuiSettings::save()` + `load()` roundtrip, default values, and corrupt-file handling

**Checkpoint**: `canmatik_gui.exe` launches, shows 3 tabs, Settings tab renders all sections, settings persist across restarts, status bar visible.

---

## Phase 2: CAN Messages Tab — Live Capture (Core)

**Purpose**: The main CAN messages display with live data, change detection, byte-level highlighting, and playback controls.

- [x] T009 [US-GUI-7] Create `src/gui/frame_collector.h` and `src/gui/frame_collector.cpp` implementing `FrameCollector`: ring buffer (`vector<CanFrame>` with head/count), `PerIdState` map (with `deque<array<uint8_t,8>>` for last-N history), `ICaptureSync::onFrame()` that stores to ring buffer and updates per-ID state, thread-safe `snapshot(bool changed_only, uint32_t change_n, ObdDisplayMode mode)` returning `vector<MessageRow>`, `add_watchdog(uint32_t id)`, `remove_watchdog(uint32_t id)`, `clear_watchdogs()`, `buffer_count()`, `buffer_capacity()`, `resize_buffer(uint32_t new_cap)`
- [x] T010 [P] [US-GUI-7] Unit test: `tests/unit/test_frame_collector.cpp` — verify ring buffer overflow drops oldest, per-ID change detection (1 and N transmissions), watchdog add/remove, snapshot filtering (changed_only, OBD mode), resize preserving data
- [x] T011 [US-GUI-1] Create `src/gui/controllers/capture_controller.h` and `src/gui/controllers/capture_controller.cpp` implementing `CaptureController`: `connect(provider, bitrate, mock)` → SessionService, `start()` → CaptureService with FrameCollector as sink, `stop()`, `disconnect()`, `pause()` / `resume()` (sets flag that suppresses drain), `is_connected()`, `is_capturing()`
- [x] T012 [US-GUI-1] Create `src/gui/panels/can_messages_panel.h` and `src/gui/panels/can_messages_panel.cpp` rendering the CAN Messages tab: ImGui table with columns (ID, DLC, Data, Time, Count), per-byte diff coloring (red=changed `ImVec4{1,0.3,0.3,1}`, green=new `ImVec4{0.3,1,0.3,1}`, yellow=DLC `ImVec4{1,0.9,0.2,1}`, cyan=ID `ImVec4{0.4,0.8,1,1}`), row selection on click (highlight selected row), auto-scroll toggle
- [x] T013 [US-GUI-3] Add change-only filter controls to CAN Messages panel: "Show changed only" checkbox bound to `GuiSettings::show_changed_only`, "Last N" spinner (ImGui::InputInt clamped 1–100) bound to `GuiSettings::change_filter_n`; pass both to `FrameCollector::snapshot()`
- [x] T014 [US-GUI-1] [US-GUI-2] Create `src/gui/widgets/playback_toolbar.h` and `src/gui/widgets/playback_toolbar.cpp` implementing a reusable toolbar widget: Play button (enabled when stopped/paused), Pause (enabled when playing), Stop (enabled when playing/paused), Rewind (enabled only in FILE data source), Fast-Forward (enabled only in FILE, cycles 2×/4×/8×/1×), Loop checkbox (enabled only in FILE); returns `PlaybackAction` enum for the caller to act on
- [x] T015 Integrate `PlaybackToolbar` into `CanMessagesPanel`; wire Play/Pause/Stop to `CaptureController` for live mode; add data source selector (ImGui radio buttons: "Live" / "File")
- [x] T016 [US-GUI-7] Add "Save Buffer" button to CAN Messages panel toolbar: opens `ImGui::FileSave` dialog (or native OPENFILENAME on Win32), writes ring buffer contents via `AscWriter` or `JsonlWriter` based on file extension; show progress/completion in status bar
- [x] T017 Wire CAN Messages panel into `GuiApp::render()`: pass `FrameCollector`, `GuiState`, `GuiSettings` references; connect Settings panel "Connect" button to `CaptureController::connect()`, start capture on Play

**Checkpoint**: Live CAN frames displayed with color-coded diffs, change-only filter works, play/pause/stop functional, save-to-file works, data source selector visible.

---

## Phase 3: CAN Messages Tab — Replay Mode

**Purpose**: Load and replay pre-recorded log files with playback controls.

- [x] T018 [US-GUI-2] Create `src/gui/controllers/replay_controller.h` and `src/gui/controllers/replay_controller.cpp` implementing `ReplayController`: `load(path)` using `AscReader` or `JsonlReader` (detect by extension), stores `vector<CanFrame>` + `ReplayState`, `play()` starts timed dispatch using original inter-frame deltas, `pause()` / `resume()`, `stop()` resets to frame 0, `rewind()` jumps to frame 0, `fast_forward()` cycles speed multiplier, `set_loop(bool)`, `tick(delta_us)` advances playback and pushes frames to `FrameCollector`
- [x] T019 [P] [US-GUI-2] Unit test: `tests/unit/test_replay_controller.cpp` — verify load from ASC and JSONL, play dispatches frames at correct timing, pause/resume preserves position, rewind resets, fast-forward doubles speed, loop wraps around
- [x] T020 [US-GUI-2] Integrate `ReplayController` into `GuiApp`: when data source is FILE, enabling Play calls `ReplayController::play()`, Rewind/FF call `rewind()`/`fast_forward()`, Loop checkbox maps to `set_loop()`; `ReplayController::tick()` called each ImGui frame with real delta time
- [x] T021 [US-GUI-2] Add file open mechanism: "Open File" in `CanMessagesPanel` toolbar (or File menu) → native `OPENFILENAME` dialog filtering for `*.asc;*.jsonl` → passes path to `ReplayController::load()`

**Checkpoint**: Pre-recorded `.asc`/`.jsonl` files load and replay with correct timing, rewind/ff/loop work, all playback controls correctly enabled/disabled per data source.

---

## Phase 4: Watchdog & OBD Mode

**Purpose**: Per-ID watchdog panel and OBD display mode filtering.

- [x] T022 [US-GUI-4] Create `src/gui/panels/watchdog_panel.h` and `src/gui/panels/watchdog_panel.cpp`: renders as a child window below the main message table; shows only `MessageRow` entries where `is_watched == true`; same table format and diff coloring; includes "Clear All Watchdogs" button
- [x] T023 [US-GUI-4] Add right-click context menu to `CanMessagesPanel` message table rows: "Add Watchdog" (calls `FrameCollector::add_watchdog(arb_id)`) and "Remove Watchdog" (calls `remove_watchdog`); conditionally show only the relevant option based on current watchdog state
- [x] T024 [US-GUI-5] Add OBD mode dropdown to CAN Messages panel toolbar: combo box with entries "OBD + Broadcast", "OBD Only", "Broadcast Only"; bound to `GuiSettings::obd_mode`; passed to `FrameCollector::snapshot()` to filter by OBD address ranges (0x7DF, 0x7E0–0x7EF)
- [x] T025 [US-GUI-5] Create `src/gui/controllers/obd_controller.h` and `src/gui/controllers/obd_controller.cpp` implementing `ObdController`: wraps `canmatik_obd::OBDSession`; `start_streaming(pids, interval)` begins cyclic queries on the open channel; `stop_streaming()`; collects decoded results into `vector<ObdPidRow>` (mutex-guarded); `snapshot()` returns current decoded values
- [x] T026 [US-GUI-5] Wire OBD streaming start/stop to "OBD Only" mode selection: when user selects "OBD Only" and capture is active, `ObdController::start_streaming()` is called with PIDs from settings; when switching away, `stop_streaming()` is called

**Checkpoint**: Right-click watchdog works, watchdog panel shows watched IDs, OBD mode dropdown filters display, OBD streaming starts/stops with mode.

---

## Phase 5: OBD Data Tab

**Purpose**: Decoded PID dashboard with table, value highlighting, and history graph.

- [x] T027 [US-GUI-6] Create `src/gui/panels/obd_data_panel.h` and `src/gui/panels/obd_data_panel.cpp` rendering the OBD Data tab: ImGui table with columns (PID hex, Name, Value, Unit, Raw Bytes); rows populated from `ObdController::snapshot()`; values that changed since last render briefly highlighted (e.g., text color flash for 0.5s using `ImGui::GetTime()`)
- [x] T028 [US-GUI-6] Add PID history graph: when user clicks a PID row, display a line plot below the table using `ImGui::PlotLines()` fed from `ObdPidRow::history` deque; show last 60 seconds of data; X axis = time, Y axis = value
- [x] T029 [US-GUI-8] Add PID filter section to Settings panel: list of selected PIDs with hex values, "Add PID" input (hex, validated 0x00–0xFF), "Remove" button per entry; changes update `GuiSettings::obd_pids`; apply to `ObdController` on next stream start

**Checkpoint**: OBD Data tab shows decoded PIDs with values/units, changed values flash, clicking a PID shows its graph, Settings PID list configures the query set.

---

## Phase 6: Polish & Integration

**Purpose**: UX refinements, keyboard shortcuts, error handling, final testing.

- [x] T030 Add keyboard shortcuts: Space = toggle play/pause, Ctrl+S = save buffer, Escape = stop capture/replay, Ctrl+O = open file
- [x] T031 Persist window state: save window position/size in `GuiSettings` on `WM_SIZE`/`WM_MOVE`; restore on startup via `SetWindowPos()`
- [x] T032 Add error dialogs: connection failure → `ImGui::OpenPopup("Connection Error")` with error message and OK button; file load failure → similar popup; buffer save failure → popup
- [x] T033 [P] Create `src/gui/panels/menu_bar.h` and `src/gui/panels/menu_bar.cpp`: File menu (Open, Save Buffer, Exit), View menu (toggle watchdog panel, toggle status bar), Help menu (About)
- [x] T034 Integration test: manual test script documenting steps to verify each user story (US-GUI-1 through US-GUI-8) using mock backend; record pass/fail

**Checkpoint**: All user stories pass manual testing. Keyboard shortcuts work. Error cases handled gracefully. Settings fully persist.

---

## CMakeLists.txt Source List (cumulative)

After all phases, the `canmatik_gui` target should include:

```cmake
add_executable(canmatik_gui WIN32
    src/gui/gui_main.cpp
    src/gui/gui_app.cpp
    src/gui/gui_settings.cpp
    src/gui/controllers/capture_controller.cpp
    src/gui/controllers/replay_controller.cpp
    src/gui/controllers/obd_controller.cpp
    src/gui/panels/can_messages_panel.cpp
    src/gui/panels/obd_data_panel.cpp
    src/gui/panels/settings_panel.cpp
    src/gui/panels/watchdog_panel.cpp
    src/gui/panels/status_bar.cpp
    src/gui/panels/menu_bar.cpp
    src/gui/widgets/playback_toolbar.cpp
)
```

Note: The existing `sniff_panel.h/.cpp` will be superseded by `can_messages_panel` (which incorporates its SniffCollector/PerIdState logic into the richer `FrameCollector`). The sniff_panel files can be removed once `CanMessagesPanel` is complete.

---

## Phase 7: UX Enhancements

**Purpose**: Application icon, color scheme selection, and J2534 provider discovery dropdown.

- [x] T035 Add GUI executable icon: create `src/gui/canmatik_gui.rc` referencing `icon.ico`, add `.rc` to `canmatik_gui` CMake target, set `wc.hIcon` in `gui_main.cpp` WNDCLASSEXW registration so the window and taskbar use the project icon
- [x] T036 Add color scheme setting: add `ColorScheme` enum (DARK, LIGHT, RETRO) to `gui_state.h`, add `color_scheme` field to `GuiSettings` with JSON persistence ("dark"/"light"/"retro"), implement `GuiApp::apply_color_scheme()` mapping to `ImGui::StyleColorsDark()`/`StyleColorsLight()`/`StyleColorsClassic()`, add "Appearance" section with combo to Settings panel, apply immediately on change
- [x] T037 Add J2534 provider scan dropdown: add `scan_providers(bool mock)` to `CaptureController` returning device names via `SessionService::scan()`, replace text input with `ImGui::BeginCombo` dropdown in Settings panel populated from scan results, add "Scan" button to refresh the provider list, auto-scan on first render

**Checkpoint**: GUI exe shows project icon in taskbar/title bar, color scheme persists and applies immediately, provider dropdown lists discovered J2534 devices.
