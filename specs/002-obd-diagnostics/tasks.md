# Tasks: OBD-II Diagnostics — J1979 Modes & PID Decoding

**Input**: Design documents from `/specs/002-obd-diagnostics/`  
**Prerequisites**: plan.md (required), spec.md (required), data-model.md (required)  
**Depends on**: Feature `001-can-bus-scanner` fully complete (122 tests passing)

**Tests**: Every phase includes unit and/or integration tests.

## Format: `[ID] [P?] [Story] Description`

- **[P]**: Can run in parallel (different files, no dependencies on incomplete tasks)
- **[Story]**: Which user story (US-OBD-1 through US-OBD-6, or Infra)

---

## Phase 1: Foundation — YAML, Intervals, Configuration

**Purpose**: Add yaml-cpp dependency, implement interval parsing, YAML config loader, and default config generation. No OBD protocol logic yet.

- [x] T101 [Infra] Add yaml-cpp 0.8 via FetchContent in top-level CMakeLists.txt with `YAML_CPP_BUILD_TESTS=OFF`, `YAML_CPP_BUILD_TOOLS=OFF`, `YAML_CPP_BUILD_CONTRIB=OFF`, `YAML_CPP_INSTALL=OFF`; verify static linking. Build must succeed with no new DLL dependencies.
- [x] T102 [Infra] Create `canmatik_obd` STATIC library target in CMakeLists.txt. Source dir: `src/obd/`. Link: `canmatik_core`, `yaml-cpp`. Include dir: `src/`. Ensure CLI target links `canmatik_obd`.
- [x] T103 [P] [US-OBD-5] Implement `IntervalSpec` parser in `src/obd/interval_spec.h` and `src/obd/interval_spec.cpp`. Parse formats: `"500ms"`, `"1s"`, `"2.5s"`, `"2hz"`, `"0.5hz"`, `"100ms"`. Return `uint32_t` milliseconds. Enforce min=10ms, max=60000ms (SC-OBD-3). Return error string on invalid input.
- [x] T104 [P] [Infra] Write unit tests for IntervalSpec in `tests/unit/test_interval_spec.cpp`. Cover: all format variants, case-insensitivity, boundary values (10ms, 60000ms), invalid input ("abc", "-1s", "0hz", "5ms" below min), edge cases ("0.001hz"=1000000ms exceeds max).
- [x] T105 [US-OBD-5] Implement `ObdConfig` YAML loader in `src/obd/obd_config.h` and `src/obd/obd_config.cpp`. Load `interval`, `addressing` (mode, tx_id, rx_base), `groups` (name, interval, pids), `standalone_pids`. Use yaml-cpp `YAML::LoadFile`. Return error string with context on parse failure.
- [x] T106 [US-OBD-5] Implement default config generation in `ObdConfig::generate_default(path)`. Write the default YAML from plan.md to disk. Called when no config exists and none specified.
- [x] T107 [P] [US-OBD-5] Write unit tests for ObdConfig in `tests/unit/test_obd_config.cpp`. Cover: load valid YAML, load YAML with groups, load YAML with standalone PIDs only, missing fields use defaults, invalid YAML returns error with line info, generate_default creates parseable file, round-trip (generate → load → verify).

---

## Phase 2: PID Tables & Decoding

**Purpose**: Built-in Mode $01 PID definitions, formula evaluation, DTC decoding. Pure data + math — no CAN I/O.

- [x] T108 [US-OBD-2] Implement `PidDefinition` and `PidFormula` types in `src/obd/pid_table.h`. Include all fields from data-model.md.
- [x] T109 [US-OBD-2] Implement `PidTable` in `src/obd/pid_table.cpp` with built-in Mode $01 coverage. At minimum, include PIDs: $00 (supported bitmap), $04 (engine load), $05 (coolant temp), $06/$07 (fuel trims), $0C (RPM), $0D (speed), $0F (intake temp), $10 (MAF), $11 (throttle), $1C (OBD standard), $1F (runtime), $2F (fuel level), $33 (barometric), $42 (control module voltage), $46 (ambient temp), $51 (fuel type). Provide `lookup(mode, pid)` returning `const PidDefinition*` (nullptr if unknown).
- [x] T110 [US-OBD-2] Implement `PidDecoder` in `src/obd/pid_decoder.h` and `src/obd/pid_decoder.cpp`. Apply `PidFormula` to raw bytes → `double` value. Handle Linear, BitEncoded, Enumerated formula types. Return `DecodedPid` struct.
- [x] T111 [US-OBD-3] Implement `DtcDecoder` in `src/obd/dtc_decoder.h` and `src/obd/dtc_decoder.cpp`. Parse 2-byte DTC pairs from Mode $03/$07 responses. Map to P/C/B/U category + 4-digit code. Return `std::vector<DtcCode>`.
- [x] T112 [P] [US-OBD-2] Write unit tests for PidTable in `tests/unit/test_pid_table.cpp`. Verify: lookup known PIDs returns correct definition, lookup unknown PIDs returns nullptr, all registered PIDs have non-empty name and valid formula, PID $00 is present (supported bitmap).
- [x] T113 [P] [US-OBD-2] Write unit tests for PidDecoder in `tests/unit/test_pid_decoder.cpp`. Verify with known values: RPM (0x1A,0xF8)→1726, coolant (0xA0)→120°C, speed (0x78)→120 km/h, throttle (0x80)→50.2%, MAF (0x01,0xF4)→50.0 g/s, fuel trim (0x80)→0%.
- [x] T114 [P] [US-OBD-3] Write unit tests for DtcDecoder in `tests/unit/test_dtc_decoder.cpp`. Verify: P0300→(0x03,0x00), C0035→(0x40,0x35), B0001→(0x80,0x01), U0100→(0xC1,0x00), zero DTCs returns empty, odd byte count returns error.

---

## Phase 3: ISO 15765-4 Transport

**Purpose**: Encode OBD requests as CAN frames, parse responses from CAN frames, handle single-frame and multi-frame (ISO-TP) for VIN.

- [x] T115 [Infra] Implement ISO 15765-4 constants and helpers in `src/obd/iso15765.h`. Include: standard addressing IDs (0x7DF, 0x7E0–0x7E7, 0x7E8–0x7EF), ISO-TP PCI byte encoding, 0x55 padding, P2CAN timeout constants.
- [x] T116 [US-OBD-1] Implement `ObdRequest` encoding in `src/obd/obd_request.h`. Build a `CanFrame` from (mode, pid, tx_id): set DLC=8, Data[0]=0x02, Data[1]=mode, Data[2]=pid, Data[3..7]=0x55.
- [x] T117 [US-OBD-1] Implement `ObdResponse` parsing in `src/obd/obd_response.h` and `src/obd/obd_response.cpp`. Extract mode, pid, data bytes A/B/C/D from a `CanFrame`. Validate: response mode = request mode + 0x40, response PID = request PID, rx_id in expected range.
- [x] T118 [US-OBD-4] Implement ISO-TP multi-frame assembly in `ObdResponse` for Mode $09 (VIN). Handle First Frame → Flow Control TX → Consecutive Frames → reassemble payload. Support up to 256 bytes (sufficient for all Mode $09 PIDs).
- [x] T119 [P] [US-OBD-1] Write unit tests for ObdRequest in `tests/unit/test_obd_request.cpp`. Verify: frame encoding for Mode $01 PID $0C, functional broadcast ID, padding bytes are 0x55, DLC=8.
- [x] T120 [P] [US-OBD-1] Write unit tests for ObdResponse in `tests/unit/test_obd_response.cpp`. Verify: single-frame parse, mode+0x40 validation, PID echo, data byte extraction, reject wrong mode, reject wrong rx_id range.
- [x] T121 [P] [US-OBD-4] Write unit tests for ISO-TP multi-frame assembly. Verify: VIN reassembly from First Frame + Consecutive Frames fixture data.

---

## Phase 4: Query Engine

**Purpose**: ObdSession orchestrates send→receive→decode. QueryScheduler manages multi-group timing.

- [x] T122 [US-OBD-1] Implement `ObdSession` in `src/obd/obd_session.h` and `src/obd/obd_session.cpp`. Takes `IChannel*` reference. Methods: `query_supported_pids()`, `query_pid(mode, pid)`, `read_dtcs()`, `clear_dtcs()`, `read_vehicle_info()`. Each sends a request, waits for response (P2CAN timeout), parses and decodes. Log every TX frame before sending (SC-OBD-5).
- [x] T123 [US-OBD-2] Implement `QueryScheduler` in `src/obd/query_scheduler.h` and `src/obd/query_scheduler.cpp`. Takes `ObdSession*` + `ObdConfig`. Manages multiple `QueryGroup`s with independent intervals. Round-robin within each group. Runs in caller's thread (blocking loop with stop flag). Emits `DecodedPid` via `std::function<void(const DecodedPid&)>` callback.
- [x] T124 [US-OBD-6] Add concurrent operation support: `QueryScheduler` accepts an `std::atomic<bool>* stop` flag. CaptureService and QueryScheduler can share the same IChannel — capture reads raw frames, scheduler writes requests and reads responses. If channel read returns frames that aren't OBD responses, scheduler ignores them (filter by rx_id range).
- [x] T125 [US-OBD-3] Add Mode $04 clear-DTC safety gate in `ObdSession::clear_dtcs(bool force)`. If `force=false`, return error requiring confirmation. CLI handles the interactive prompt.
- [x] T126 [P] [US-OBD-1] Write integration tests for ObdSession in `tests/integration/test_obd_session.cpp` using MockChannel. Inject canned CAN frames for: supported PIDs response, RPM response, coolant response, DTC response (2 DTCs), VIN multi-frame response. Verify decoded values.
- [x] T127 [P] [US-OBD-2] Write unit tests for QueryScheduler in `tests/unit/test_query_scheduler.cpp`. Verify: round-robin order, timing (mock clock or short intervals), stop flag terminates, callback receives DecodedPid.

---

## Phase 5: CLI Integration

**Purpose**: `canmatik obd` subcommand with query/stream/dtc/info sub-modes, composable with monitor and record.

- [x] T128 [US-OBD-1] Implement `register_obd_command()` and `dispatch_obd()` in `src/cli/cmd_obd.cpp`. Register nested subcommands: `query`, `stream`, `dtc`, `info` via CLI11. Global options inherited: `--provider`, `--mock`, `--json`, `--bitrate`.
- [x] T129 [US-OBD-1] Implement `obd query --supported` dispatch. Connect to provider, run `ObdSession::query_supported_pids()`, display table (text or JSON). Sort by ECU address, PID number.
- [x] T130 [US-OBD-2] Implement `obd stream` dispatch. Load ObdConfig (YAML or default), create QueryScheduler, start streaming loop. Display decoded values as they arrive. Accept `--interval` CLI override. Accept `--obd-config` for custom YAML path.
- [x] T131 [US-OBD-6] Implement `--monitor` and `--record` flags on `obd stream`. When `--monitor`, start CaptureService alongside scheduler (dual output). When `--record -o FILE`, add RecordService as capture sink. Both use the same IChannel instance.
- [x] T132 [US-OBD-3] Implement `obd dtc` dispatch. Show stored + pending DTCs. `--clear` sends Mode $04 (with confirmation prompt unless `--force`).
- [x] T133 [US-OBD-4] Implement `obd info` dispatch. Query Mode $09 PIDs $02 (VIN), $04 (Cal ID), $0A (ECU name). Display in text or JSON.
- [x] T134 [Infra] Register `obd` subcommand in `cli_app.cpp` (`build_cli`, `dispatch`). Update `--help` descriptions. Add forward declarations.
- [x] T135 [US-OBD-5] Implement default YAML generation on first use: if `obd stream` runs with no `--obd-config` and no `obd.yaml` in cwd, call `ObdConfig::generate_default("obd.yaml")`, log info message, then load it.
- [x] T136 [P] [US-OBD-6] Write integration tests for CLI OBD in `tests/integration/test_obd_cli.cpp` using `--mock`. Test: `obd query --supported --mock --json`, `obd stream --mock --interval 100ms` (run briefly), `obd dtc --mock --json`, `obd info --mock --json`.

---

## Phase 6: Polish & Documentation

**Purpose**: Edge cases, error messages, sample configs, documentation.

- [x] T137 [P] [Infra] Create reference YAML configs in `samples/configs/`: `obd_default.yaml` (generated content), `obd_engine_only.yaml` (RPM + coolant + load at 5Hz), `obd_fuel_economy.yaml` (speed + MAF + fuel level at 1Hz).
- [x] T138 [P] [Infra] Add OBD section to `docs/` — document YAML schema, CLI usage, supported PIDs table, interval format.
- [x] T139 [Infra] Edge-case tests: query PID not supported by ECU (negative response 0x7F), channel timeout (no ECU response), YAML with 0 groups (error), interval below 10ms floor (error), Mode $04 without --force (rejected).
- [x] T140 [Infra] Verify full build: all tests pass, no new DLL dependencies from yaml-cpp, `canmatik.exe` and `fake_j2534.dll` both build cleanly.
