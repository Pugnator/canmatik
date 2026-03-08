# Tasks: J2534 CAN Bus Scanner & Logger

**Input**: Design documents from `/specs/001-can-bus-scanner/`
**Prerequisites**: plan.md (required), spec.md (required), research.md, data-model.md, contracts/cli-commands.md, quickstart.md

**Tests**: Included — plan.md Definition of Done requires unit tests for all core/logging code and integration tests for mock capture → filter → record → replay roundtrip.

**Organization**: Tasks are grouped by user story to enable independent implementation and testing of each story.

## Format: `[ID] [P?] [Story] Description`

- **[P]**: Can run in parallel (different files, no dependencies on incomplete tasks)
- **[Story]**: Which user story this task belongs to (e.g., US1, US2, US3)
- Include exact file paths in descriptions

---

## Phase 1: Setup (Shared Infrastructure)

**Purpose**: Project initialization — directory structure, build system, top-level documentation

- [ ] T001 Create project directory structure per plan.md (src/core/, src/transport/, src/platform/win32/, src/mock/, src/services/, src/logging/, src/cli/, src/config/, tests/unit/, tests/integration/, samples/configs/, samples/captures/, docs/, third_party/) and add TinyLog as git submodule at third_party/tinylog/ from https://github.com/Pugnator/TinyLog (use `git submodule update --init --recursive` to also fetch TinyLog's vendored zstd dependency)
- [ ] T002 Create top-level CMakeLists.txt with C++20, MinGW Makefiles, 32-bit target, FetchContent for CLI11 2.4, nlohmann/json 3.11, Catch2 v3, and add_subdirectory for TinyLog git submodule (third_party/tinylog/) with `set(BUILD_SHARED_LIBS OFF)` before `add_subdirectory` to override TinyLog's default SHARED build, per research.md in CMakeLists.txt
- [ ] T003 [P] Create README.md with project overview, build instructions, safety notice, and example commands in README.md
- [ ] T004 [P] Create SAFETY.md with user-facing passive-mode-first policy summary and safety behavior quick reference (distinct from docs/safety-notice.md which provides detailed technical safety analysis) in SAFETY.md
- [ ] T005 [P] Create default configuration example in samples/configs/default.json and samples/README.md describing sample file contents
- [ ] T065 [P] Create CONTRIBUTING.md with development environment setup, PR workflow, code style guide, and constitution-compliance note requirement in CONTRIBUTING.md

---

## Phase 2: Foundational (Blocking Prerequisites)

**Purpose**: Core value types, transport interfaces, mock backend, CLI scaffold, and test infrastructure that ALL user stories depend on

**⚠️ CRITICAL**: No user story work can begin until this phase is complete

### Core Value Types

- [ ] T006 Implement CanFrame struct, FrameType enum, and CanId validation in src/core/can_frame.h and src/core/can_frame.cpp
- [ ] T007 [P] Implement timestamp rollover helpers (32→64-bit extension, steady_clock utilities) in src/core/timestamp.h
- [ ] T008 [P] Implement TransportError struct (code, message, source, recoverable) in src/transport/transport_error.h
- [ ] T009 [P] Implement DeviceInfo struct (name, vendor, dll_path, protocol flags) in src/transport/device_info.h
- [ ] T010 [P] Define FilterRule struct and FilterAction enum (data types only, not engine logic) in src/core/filter.h
- [ ] T011 Implement OperatingMode enum and SessionStatus struct with counters in src/core/session_status.h and src/core/session_status.cpp

### Transport Interfaces

- [ ] T012 Define IDeviceProvider interface (enumerate, connect) in src/transport/device_provider.h
- [ ] T013 [P] Define IChannel interface (open, close, read, write, setFilter, clearFilters) in src/transport/channel.h
- [ ] T014 [P] Define ICaptureSync observer interface (onFrame, onError) in src/core/capture_sink.h

### Infrastructure

- [ ] T015 Implement Config struct with JSON loading, CLI-flag merge logic, and TinyLog rotation settings (log_file, log_max_file_size, log_max_backups, log_compress) per configuration strategy in src/config/config.h and src/config/config.cpp
- [ ] T016 [P] Implement MockProvider (IDeviceProvider) with configurable DeviceInfo list in src/mock/mock_provider.h and src/mock/mock_provider.cpp
- [ ] T017 [P] Implement MockChannel (IChannel) with configurable frame generation, timing, and error injection in src/mock/mock_channel.h and src/mock/mock_channel.cpp

### CLI Scaffold

- [ ] T018 Create CLI entry point and CLI11 app setup with global options (--config, --provider, --mock, --json, --verbose, --debug) per contracts/cli-commands.md, initialize TinyLog singleton (console tracer default, file tracer on --debug, rotation config from Config) in src/cli/main.cpp, src/cli/cli_app.h, and src/cli/cli_app.cpp
- [ ] T019 [P] Create text and JSON output formatters (frame line format, session header/footer) per output format contract in src/cli/formatters.h

### Test Infrastructure

- [ ] T020 Create tests/CMakeLists.txt with Catch2 FetchContent, catch_discover_tests(), and test targets in tests/CMakeLists.txt
- [ ] T021 [P] Unit test for CanFrame construction, validation rules, and FrameType in tests/unit/test_can_frame.cpp
- [ ] T022 [P] Unit test for timestamp rollover handling and conversion in tests/unit/test_timestamp.cpp
- [ ] T023 [P] Unit test for SessionStatus counters, state tracking, and OperatingMode in tests/unit/test_session_status.cpp
- [ ] T024 [P] Unit test for Config JSON loading, defaults, and CLI-flag override in tests/unit/test_config.cpp

**Checkpoint**: Foundation ready — all core types compile, mock backend works, CLI scaffold runs `--help`, all foundational unit tests pass. User story implementation can now begin.

---

## Phase 3: User Story 1 — Discover and Connect to a J2534 Interface (Priority: P1) 🎯 MVP

**Goal**: A user plugs in their USB J2534-compatible adapter, launches `canmatik scan`, sees available providers, and connects to a device.

**Independent Test**: Run `canmatik scan` with a real adapter (or mock backend) and verify provider list and connection success with adapter details.

### Implementation for User Story 1

- [ ] T025 [US1] Create J2534 API types, constants, function pointer typedefs with __stdcall calling convention, and PASSTHRU_MSG struct in src/platform/win32/j2534_defs.h
- [ ] T026 [P] [US1] Implement RAII DLL loader (LoadLibrary, GetProcAddress for all J2534 functions, FreeLibrary destructor) in src/platform/win32/j2534_dll_loader.h and src/platform/win32/j2534_dll_loader.cpp
- [ ] T027 [P] [US1] Implement Windows Registry scanner for J2534 provider discovery under HKLM\SOFTWARE\PassThruSupport.04.04 with KEY_WOW64_32KEY in src/platform/win32/registry_scanner.h and src/platform/win32/registry_scanner.cpp
- [ ] T028 [US1] Implement J2534Provider (IDeviceProvider) using DLL loader and registry scanner in src/platform/win32/j2534_provider.h and src/platform/win32/j2534_provider.cpp
- [ ] T029 [P] [US1] Implement J2534Channel (IChannel) with PassThruConnect, PassThruReadMsgs, PassThruStartMsgFilter, PassThruDisconnect in src/platform/win32/j2534_channel.h and src/platform/win32/j2534_channel.cpp
- [ ] T030 [US1] Implement SessionService (connect, disconnect, openChannel, closeChannel, provider selection) in src/services/session_service.h and src/services/session_service.cpp
- [ ] T031 [US1] Implement `canmatik scan` subcommand with text and JSON output per CLI contract in src/cli/cmd_scan.cpp

**Checkpoint**: `canmatik scan` lists providers. `canmatik scan --mock` lists the mock provider. Exit codes follow contract (0 = found, 2 = registry failure).

---

## Phase 4: User Story 2 — Start a CAN Monitoring Session (Priority: P1) 🎯 MVP

**Goal**: A connected user opens a CAN channel at a chosen bitrate and sees a continuous stream of frames with timestamps, IDs, DLC, and payload.

**Independent Test**: Run `canmatik monitor --mock` and verify frames appear with all required fields in correct format, session stops cleanly on Ctrl+C with summary.

### Implementation for User Story 2

- [ ] T032 [US2] Implement CaptureService with dedicated reader thread, SPSC queue, frame normalization, dual timestamp capture, and ICaptureSync dispatch in src/services/capture_service.h and src/services/capture_service.cpp
- [ ] T033 [US2] Implement `canmatik monitor` subcommand with --provider, --bitrate flags, Ctrl+C handling, session summary footer per CLI contract in src/cli/cmd_monitor.cpp
- [ ] T034 [US2] Integration test: full mock capture pipeline (MockProvider → connect → open channel → receive frames → verify format and ordering) in tests/integration/test_mock_capture.cpp

**Checkpoint**: `canmatik monitor --mock --bitrate 500000` streams frames to console. Ctrl+C prints session summary. All fields (timestamp, arb ID, frame type, DLC, payload) present.

---

## Phase 5: User Story 3 — Filter Traffic by Arbitration ID (Priority: P2)

**Goal**: While monitoring, a user sets up ID-based filters (exact, range, mask, pass/block) to focus on specific traffic. Unmatched frames are hidden from display.

**Independent Test**: Run `canmatik monitor --mock --filter 0x100` and verify only ID 0x100 frames appear. Clear filter and verify all frames resume.

### Implementation for User Story 3

- [ ] T035 [US3] Implement FilterEngine with pass/block evaluation, ID/mask matching, range support, and rule combination logic in src/core/filter.cpp
- [ ] T036 [P] [US3] Unit test for FilterEngine: exact match, mask match, range, pass/block combinations, multiple rules, default-pass behavior in tests/unit/test_filter.cpp
- [ ] T037 [US3] Integrate FilterEngine into CaptureService consumer path (filter before display dispatch) in src/services/capture_service.cpp
- [ ] T038 [US3] Add --filter flag parsing with filter syntax (0x7E8, 0x700-0x7FF, 0x700/0xFF0, !0x000, pass:, block:) to monitor subcommand in src/cli/cmd_monitor.cpp

**Checkpoint**: `canmatik monitor --mock --filter 0x100` shows only matching frames. `--filter 0x100-0x1FF` and `--filter !0x000` work. FilterEngine unit tests pass.

---

## Phase 6: User Story 4 — Record and Save a Capture Session (Priority: P2)

**Goal**: A user starts recording during monitoring. Frames are written to a Vector ASC or JSONL log file. Recording stops cleanly with frame count and file path reported.

**Independent Test**: Run `canmatik record --mock --output test.asc`, capture frames, stop, and verify the output file contains all expected frames with correct formatting.

### Implementation for User Story 4

- [ ] T039 [US4] Create ILogWriter interface (writeHeader, writeFrame, writeFooter, flush) in src/logging/log_writer.h
- [ ] T040 [US4] Implement AscWriter (Vector ASC format with timestamps, IDs, DLC, payload) in src/logging/asc_writer.h and src/logging/asc_writer.cpp
- [ ] T041 [P] [US4] Implement JsonlWriter (one JSON object per line via nlohmann/json) in src/logging/jsonl_writer.h and src/logging/jsonl_writer.cpp
- [ ] T042 [P] [US4] Unit test for AscWriter: header, frame lines, footer format validation in tests/unit/test_asc_writer.cpp
- [ ] T043 [P] [US4] Unit test for JsonlWriter: JSON structure, field presence, escaping in tests/unit/test_jsonl_writer.cpp
- [ ] T044 [US4] Implement RecordService (start/stop recording, file creation, writer selection, --filter-recording support) in src/services/record_service.h and src/services/record_service.cpp
- [ ] T045 [US4] Implement `canmatik record` subcommand with --output, --format, --filter, --filter-recording flags per CLI contract in src/cli/cmd_record.cpp
- [ ] T046 [P] [US4] Create sample capture files in samples/captures/idle_500kbps.asc and samples/captures/idle_500kbps.jsonl

**Checkpoint**: `canmatik record --mock --output captures/test.asc` produces a valid ASC file. `--format jsonl` produces valid JSONL. Frame counts match. Writer unit tests pass.

---

## Phase 7: User Story 5 — Open and Inspect a Previously Captured Log (Priority: P3)

**Goal**: A user opens a previously saved log file offline, scrolls through frames, searches by arbitration ID, and views session summary without a live device.

**Independent Test**: Open a known ASC/JSONL log with `canmatik replay`, verify all frames load correctly, `--search 0x7E8` finds expected frames, `--summary` shows correct metadata.

### Implementation for User Story 5

- [ ] T047 [US5] Create ILogReader interface (open, nextFrame, metadata, reset) in src/logging/log_reader.h
- [ ] T048 [US5] Implement AscReader (parse Vector ASC format back into CanFrame stream) in src/logging/asc_reader.h and src/logging/asc_reader.cpp
- [ ] T049 [P] [US5] Implement JsonlReader (parse JSONL back into CanFrame stream) in src/logging/jsonl_reader.h and src/logging/jsonl_reader.cpp
- [ ] T050 [P] [US5] Unit test for AscReader: valid file, corrupt data, version mismatch, empty file in tests/unit/test_asc_reader.cpp
- [ ] T051 [P] [US5] Unit test for JsonlReader: valid file, malformed JSON, missing fields in tests/unit/test_jsonl_reader.cpp
- [ ] T052 [US5] Implement ReplayService (load file, iterate frames, search by ID, session summary with unique ID count and distribution) in src/services/replay_service.h and src/services/replay_service.cpp
- [ ] T053 [US5] Implement `canmatik replay` subcommand with positional file arg, --filter, --summary, --search flags per CLI contract in src/cli/cmd_replay.cpp
- [ ] T054 [US5] Integration test: record → save → load → verify roundtrip (AscWriter→AscReader, JsonlWriter→JsonlReader, verify frame-level equality) in tests/integration/test_record_replay.cpp

**Checkpoint**: `canmatik replay samples/captures/idle_500kbps.asc` displays all frames. `--summary` shows session metadata. `--search 0x100` filters correctly. Reader unit tests and roundtrip integration test pass.

---

## Phase 8: User Story 6 — View Session Information and Health (Priority: P3)

**Goal**: During an active session, a user checks live status: connection state, bitrate, duration, frame/error/dropped counters, recording state, and active filters.

**Independent Test**: Run a mock session, call `canmatik status --mock`, verify all status fields display correctly including error counters when mock injects errors.

### Implementation for User Story 6

- [ ] T055 [US6] Implement `canmatik status` subcommand with text and JSON output (provider, firmware, reachability) per CLI contract in src/cli/cmd_status.cpp
- [ ] T056 [US6] Add session summary footer with frame count, error count, dropped count, and duration to monitor and record output in src/cli/formatters.h

**Checkpoint**: `canmatik status --mock` shows provider reachability. Monitor/record commands display session summary on exit with accurate counters.

---

## Phase 9: User Story 7 — Run Without Hardware Using a Test Backend (Priority: P3)

**Goal**: A developer or new user runs `canmatik demo` without a physical adapter. The mock backend generates simulated traffic so all features (monitoring, filtering, recording, replay) can be exercised.

**Independent Test**: Launch `canmatik demo`, verify frames appear with [MOCK] indicator, apply filters, record to file, replay — all without hardware.

### Implementation for User Story 7

- [ ] T057 [US7] Implement `canmatik demo` subcommand with --bitrate, --filter, --trace, --frame-rate flags using MockProvider per CLI contract in src/cli/cmd_demo.cpp
- [ ] T058 [US7] Integration test: CLI end-to-end with mock backend (scan, monitor, record, replay, demo subcommands verified) in tests/integration/test_cli_commands.cpp

**Checkpoint**: `canmatik demo` streams mock frames with [MOCK] badge. `canmatik demo --trace captures/idle_500kbps.asc` replays a trace as mock input. All features identical to real session. Integration test passes.

---

## Phase 10: Polish & Cross-Cutting Concerns

**Purpose**: Documentation, hardening, and validation across all user stories

- [ ] T059 [P] Create detailed safety notice documentation with technical safety analysis, active-mode risk assessment, and J2534 write-guard implementation notes (complements root SAFETY.md user-facing summary) in docs/safety-notice.md
- [ ] T060 [P] Create adapter compatibility guide with tested adapters and known issues in docs/adapter-compatibility.md
- [ ] T061 [P] Create log format specification for Vector ASC and JSONL formats in docs/log-format-spec.md
- [ ] T062 [P] Create J2534 implementation notes with DLL loading, registry discovery, and calling convention details in docs/j2534-notes.md
- [ ] T063 Error handling hardening: verify all 6 error categories (fatal startup, connection failure, session fault, runtime fault, user error, data error) produce actionable messages across all CLI commands. Specifically validate spec.md edge cases: (1) USB adapter unplugged during session — graceful stop + recording finalization, (2) silent bus — no-timeout, "no frames received" status, (3) high bus load — no frame drops up to adapter limit + overflow indication, (4) duplicate recording request — clear reject/queue, (5) disk space exhaustion — graceful stop + warning, (6) future-version log file — version mismatch error
- [ ] T064 Run quickstart.md validation end-to-end: build from scratch, scan, monitor, record, replay, demo — verify all documented commands work
- [ ] T066 [P] Implement LabelStore (load/save/lookup of user-assigned arbitration ID labels from labels.json) per FR-021 and Constitution Principle VI in src/core/label_store.h and src/core/label_store.cpp with unit test in tests/unit/test_label_store.cpp
- [ ] T067 Integrate LabelStore into CLI display formatters — show user-assigned label next to arbitration ID when available (e.g., `7E8 [Engine RPM]`), add `canmatik label set <id> <name>` and `canmatik label list` subcommands in src/cli/cmd_label.cpp

---

## Dependencies & Execution Order

### Phase Dependencies

- **Setup (Phase 1)**: No dependencies — start immediately
- **Foundational (Phase 2)**: Depends on Setup — **BLOCKS all user stories**
- **US1 (Phase 3)**: Depends on Foundational — first user-facing deliverable
- **US2 (Phase 4)**: Depends on US1 (needs SessionService)
- **US3 (Phase 5)**: Depends on US2 (modifies CaptureService and cmd_monitor.cpp)
- **US4 (Phase 6)**: Depends on US3 (record command uses FilterEngine)
- **US5 (Phase 7)**: Depends on US4 (readers parse writer output formats)
- **US6 (Phase 8)**: Depends on US2 (needs CaptureService and SessionStatus in place)
- **US7 (Phase 9)**: Depends on US2 (mock infrastructure in Foundational, demo wires into capture pipeline)
- **Polish (Phase 10)**: Depends on all user stories being complete

### User Story Dependencies

- **US1 (P1)**: Foundational only — no other story dependencies
- **US2 (P1)**: Depends on US1 (SessionService must exist)
- **US3 (P2)**: Depends on US2 (integrates filter into capture consumer)
- **US4 (P2)**: Depends on US3 (record command natively supports --filter)
- **US5 (P3)**: Depends on US4 (reader must parse writer output format)
- **US6 (P3)**: Depends on US2 — can run in parallel with US3/US4/US5
- **US7 (P3)**: Depends on US2 — can run in parallel with US3/US4/US5

### Parallel Opportunities

- **US6 and US7** can be developed in parallel with US3/US4/US5 (after US2 is complete)
- Within Foundational: T006–T010 all parallel, T012–T014 parallel, T016–T017 parallel, T021–T024 parallel
- Within US1: T026 and T027 parallel (DLL loader and registry scanner), T028 and T029 parallel (provider and channel)
- Within US4: T040 and T041 parallel (AscWriter and JsonlWriter), T042 and T043 parallel (writer tests)
- Within US5: T048 and T049 parallel (AscReader and JsonlReader), T050 and T051 parallel (reader tests)
- Within Polish: T059–T062 all parallel (independent docs)

### Within Each User Story

- Types/interfaces before implementations
- Implementations before CLI subcommands
- CLI subcommands before integration tests
- Story complete before moving to next priority

---

## Parallel Example: Foundational Phase

```text
# Batch 1 — Core value types (all independent):
T006: CanFrame + FrameType
T007: Timestamp helpers
T008: TransportError
T009: DeviceInfo
T010: FilterRule/FilterAction

# Batch 2 — Interfaces + SessionStatus (depend on Batch 1):
T011: SessionStatus + OperatingMode
T012: IDeviceProvider interface
T013: IChannel interface
T014: ICaptureSync interface

# Batch 3 — Infrastructure (depend on Batch 2):
T015: Config
T016: MockProvider
T017: MockChannel

# Batch 4 — CLI scaffold:
T018: CLI entry point + CLI11 app
T019: Formatters

# Batch 5 — Unit tests:
T020: tests/CMakeLists.txt
T021: test_can_frame
T022: test_timestamp
T023: test_session_status
T024: test_config
```

## Parallel Example: After US2 Completes

```text
# These three story tracks can proceed in parallel:

Track A (sequential):         Track B:              Track C:
  US3 → US4 → US5              US6 (2 tasks)         US7 (2 tasks)
  (20 tasks)                    Independent           Independent
```

---

## Implementation Strategy

### MVP First (Setup + Foundational + US1 + US2)

1. Complete Phase 1: Setup (6 tasks)
2. Complete Phase 2: Foundational (19 tasks) — **CRITICAL: blocks everything**
3. Complete Phase 3: US1 — Discover and Connect (7 tasks)
4. Complete Phase 4: US2 — CAN Monitoring (3 tasks)
5. **STOP and VALIDATE**: `canmatik scan --mock` and `canmatik monitor --mock` work end-to-end
6. **MVP milestone**: 35 tasks, user can discover adapters and watch live CAN frames

### Incremental Delivery

1. Setup + Foundational → Foundation ready (25 tasks)
2. Add US1 → Test scan independently → **First functional build** (32 tasks)
3. Add US2 → Test monitoring → **MVP: live frame viewing** (35 tasks)
4. Add US3 → Test filtering → Usable on busy bus (39 tasks)
5. Add US4 → Test recording → Captures saved to disk (47 tasks)
6. Add US5 → Test replay → Full capture-analyze workflow (55 tasks)
7. Add US6 + US7 → Session diagnostics + demo mode (59 tasks)
8. Polish → Documentation, annotation, and hardening (67 tasks)

### Parallel Team Strategy

With multiple developers after Foundational is complete:

- **Developer A**: US1 → US2 → US3 → US4 → US5 (main pipeline)
- **Developer B**: (after US2) US6 → US7 → Polish docs
- Stories integrate independently via shared service interfaces

---

## Notes

- [P] tasks = different files, no dependencies on incomplete tasks in the same phase
- [Story] label maps task to specific user story for traceability
- Each user story is independently testable at its checkpoint
- Commit after each task or logical group
- All 32-bit build: MinGW i686 toolchain required for J2534 DLL compatibility
- C++20 required: TinyLog uses `std::format`; GCC ≥ 13 supports this
- TinyLog (git submodule at third_party/tinylog/, requires recursive init for vendored zstd) handles all diagnostic logging — not CAN frame capture logs
- USB-only adapters: serial, Bluetooth, and Wi-Fi adapters are out of scope
- Passive mode only in MVP: IChannel::write() rejects calls when OperatingMode is Passive
- Exit codes: 0 = success, 1 = user error, 2 = hardware failure
