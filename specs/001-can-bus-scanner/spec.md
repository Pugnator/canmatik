# Feature Specification: J2534 CAN Bus Scanner & Logger

**Feature Branch**: `001-can-bus-scanner`  
**Created**: 2026-03-08  
**Status**: Draft  
**Input**: User description: "Open-source CAN bus scanner and logger for J2534 Pass-Thru interfaces"

## Project Overview

An open-source desktop tool that connects to vehicles via a USB-connected J2534 Pass-Thru adapter, captures CAN bus traffic in real time, and saves it for later analysis. The tool exists because closed-source alternatives frequently fail on vehicles like Ducati motorcycles and Mazda MX-5, and because vehicle owners, hobbyists, and researchers deserve full transparency into what runs on the bus. Only USB-attached adapters are supported; serial, Bluetooth, and Wi-Fi adapters are out of scope.

The MVP focuses exclusively on **reliable passive CAN traffic observation**: connect, capture, filter, log, and review. Active transmission, ECU diagnostics, and brand-specific decoding are future scope.

## User Personas

### Persona 1 — Vehicle Hacker / Reverse Engineer

A technically skilled user who connects to a vehicle to discover unknown CAN messages, identify arbitration IDs, and map out the communication patterns between ECUs. They work iteratively: capture a session, study the log, form hypotheses, capture again with tighter filters.

**Primary workflow**: Connect → sniff → filter → save → analyze offline → repeat.

**Key needs**: Bit-perfect timestamps, raw payloads, fast filtering, structured export for external tooling.

### Persona 2 — Embedded Developer

A developer building or testing hardware that speaks CAN (custom ECU, sensor module, data logger). They use the tool to verify that their device transmits correctly and to inspect bus traffic during integration testing.

**Primary workflow**: Connect → monitor traffic while testing their device → filter by their device's IDs → save evidence for documentation.

**Key needs**: Real-time visibility, frame-level detail, easy identification of their own messages vs. background traffic.

### Persona 3 — Automotive Hobbyist

A vehicle enthusiast who wants to understand what their car or motorcycle is saying on the bus. They may not have deep protocol expertise but are comfortable with command-line tools and following documented procedures.

**Primary workflow**: Follow quick-start guide → connect adapter → watch traffic → save a capture → share with community.

**Key needs**: Clear instructions, minimal configuration, human-readable output, obvious error messages.

### Persona 4 — Diagnostics Researcher

An engineer or academic studying vehicle communication patterns, protocol behaviors, or diagnostic session flows. They need long-running captures and structured data for statistical analysis.

**Primary workflow**: Configure a long capture session → let it run → export structured logs → process with external analysis tools.

**Key needs**: No frame loss under sustained load, structured export (JSON/CSV), session metadata, ability to annotate captures.

## User Scenarios & Testing *(mandatory)*

### User Story 1 — Discover and Connect to a J2534 Interface (Priority: P1)

A user plugs in their USB J2534-compatible adapter, launches the tool, and connects to the device. The tool discovers available J2534 providers installed on the system, presents them to the user, and allows selection. The user sees confirmation of a successful connection along with basic adapter information (name, provider).

**Why this priority**: Nothing else works without a device connection. This is the entry point for every workflow.

**Independent Test**: Can be fully tested by connecting a real adapter (or using a mock backend) and verifying the tool reports connection success and adapter details.

**Acceptance Scenarios**:

1. **Given** one or more J2534 providers are installed on the system, **When** the user requests device discovery, **Then** the tool lists all available providers with their names.
2. **Given** a provider is selected, **When** the user requests a connection, **Then** the tool opens the device and displays adapter name and connection status.
3. **Given** no J2534 providers are installed, **When** the user requests device discovery, **Then** the tool displays a clear message explaining that no providers were found and suggests checking adapter installation.
4. **Given** a provider is selected but the USB adapter is not physically connected, **When** the user requests a connection, **Then** the tool displays a specific error identifying the hardware as unreachable.
5. **Given** an active connection exists, **When** the user requests disconnection, **Then** the tool cleanly closes the device and confirms disconnection.

---

### User Story 2 — Start a CAN Monitoring Session (Priority: P1)

A connected user opens a CAN channel at a chosen bitrate and begins receiving frames. The tool enters passive monitoring mode, displaying a continuous stream of frames with timestamps, arbitration IDs, DLC, and payload bytes.

**Why this priority**: Real-time frame observation is the core value proposition — the reason the tool exists.

**Independent Test**: Can be tested by opening a channel on a live bus (or mock) and verifying frames appear with all required fields, in the correct order, without corruption.

**Acceptance Scenarios**:

1. **Given** an active device connection, **When** the user starts a CAN session at 500 kbps, **Then** the tool opens a CAN channel and begins displaying received frames.
2. **Given** an active CAN session, **When** frames arrive on the bus, **Then** each frame is displayed with: monotonic timestamp, arbitration ID (hex), frame type, DLC, and payload bytes (hex).
3. **Given** an active CAN session, **When** the user requests session stop, **Then** the tool closes the channel and reports final session statistics (frame count, duration, error count).
4. **Given** an active CAN session, **When** the user requests a restart with a different bitrate, **Then** the tool closes the current channel and opens a new one at the requested bitrate.
5. **Given** a device connection but an unsupported bitrate is requested, **When** the user attempts to start a session, **Then** the tool reports the bitrate is not supported and lists available options.

---

### User Story 3 — Filter Traffic by Arbitration ID (Priority: P2)

While monitoring, a user sets up filters to focus on specific arbitration IDs or ranges. Unwanted traffic is hidden from the display but optionally still captured in the background log.

**Why this priority**: On a busy CAN bus with hundreds of message types, filtering is essential for usability, but the tool still delivers value without it (User Stories 1 and 2).

**Independent Test**: Can be tested by applying a filter on a busy bus and verifying that only matching frames appear in output while non-matching frames are suppressed.

**Acceptance Scenarios**:

1. **Given** an active CAN session, **When** the user applies a filter for a single arbitration ID (e.g., 0x7E8), **Then** only frames matching that ID appear in the display.
2. **Given** an active CAN session, **When** the user applies an ID range or mask filter, **Then** only frames within the specified range appear.
3. **Given** an active filter, **When** the user clears the filter, **Then** all frames resume appearing in the display.
4. **Given** an active filter on the display, **When** recording is active, **Then** the user can choose whether the recording captures all frames or only filtered frames.
5. **Given** multiple filters are defined, **When** they are applied together, **Then** the tool combines them logically and shows only frames matching the combined criteria.

---

### User Story 4 — Record and Save a Capture Session (Priority: P2)

A user starts recording during a monitoring session. All frames (or filtered frames, per user preference) are written to a log file. The user can stop recording, name the capture, and save it for later analysis.

**Why this priority**: Capturing traffic for later analysis is a core use case, but a user can still observe traffic in real time without saving (User Stories 1 and 2).

**Independent Test**: Can be tested by recording a session, stopping, and verifying the output file contains all expected frames with correct fields and formatting.

**Acceptance Scenarios**:

1. **Given** an active CAN session, **When** the user starts recording, **Then** the tool begins writing frames to a log file and indicates that recording is active.
2. **Given** an active recording, **When** frames are received, **Then** every frame is written to the log with timestamp, channel, arbitration ID, frame type, DLC, and payload.
3. **Given** an active recording, **When** the user stops recording, **Then** the tool finalizes the log file and reports the number of frames captured and the file location.
4. **Given** a completed recording, **When** the user inspects the output file, **Then** it is available in at least one human-readable text format and one structured format (JSON or CSV).
5. **Given** a recording session, **When** the user wants to specify a custom file name or location, **Then** the tool allows naming the output before or after capture.
6. **Given** a long recording session with sustained high traffic, **When** the recording completes, **Then** no frames were dropped or corrupted in the output file.

---

### User Story 5 — Open and Inspect a Previously Captured Log (Priority: P3)

A user opens a previously saved log file in offline mode. They can scroll through the traffic history, search for specific arbitration IDs, and inspect message patterns without a live device connection.

**Why this priority**: Offline analysis completes the capture-and-analyze workflow, but a user can still capture and review output files manually with external tools.

**Independent Test**: Can be tested by opening a known log file and verifying all frames load correctly, search finds expected IDs, and display matches the original capture.

**Acceptance Scenarios**:

1. **Given** a saved log file in a supported format, **When** the user opens it for inspection, **Then** the tool loads and displays the captured frames with all original fields intact.
2. **Given** a loaded log file, **When** the user searches for a specific arbitration ID, **Then** the tool shows all frames matching that ID.
3. **Given** a loaded log file, **When** the user scrolls through the frame list, **Then** frames are displayed in their original chronological order.
4. **Given** a loaded log file, **When** the user requests session summary information, **Then** the tool shows total frame count, unique IDs observed, session duration, and capture metadata.
5. **Given** a log file in an unsupported or corrupted format, **When** the user attempts to open it, **Then** the tool reports a clear error describing the problem.

---

### User Story 6 — View Session Information and Health (Priority: P3)

During an active session, the user can check live session information: connection state, selected bitrate, session duration, frame counts (read/write), error counts, and whether recording is active.

**Why this priority**: Observability improves the user experience and aids troubleshooting, but the core capture workflow functions without it.

**Independent Test**: Can be tested by running a session and checking that all status fields update correctly, including error counters when bus errors are simulated.

**Acceptance Scenarios**:

1. **Given** an active CAN session, **When** the user requests session status, **Then** the tool displays: connection state, adapter name, bitrate, session duration, frames received, frames transmitted (if any), error count.
2. **Given** a session with bus errors occurring, **When** the user checks status, **Then** error counters reflect the actual number of errors observed.
3. **Given** an active recording, **When** the user checks status, **Then** the display indicates recording is active and shows the recording file name.
4. **Given** filters are applied, **When** the user checks status, **Then** the active filter criteria are displayed.
5. **Given** the adapter reports dropped frames, **When** the user checks status, **Then** a dropped-frame warning is visible.

---

### User Story 7 — Run Without Hardware Using a Test Backend (Priority: P3)

A developer or new user runs the tool in a test/demo mode without a physical J2534 adapter. A built-in mock backend generates simulated CAN traffic so that all features (monitoring, filtering, recording, offline review) can be exercised.

**Why this priority**: Enables development, CI testing, and evaluation without hardware, but is not required for the primary user workflow.

**Independent Test**: Can be tested by launching in test mode and verifying frames appear, filters work, and recordings produce valid log files — all without any hardware connected.

**Acceptance Scenarios**:

1. **Given** no physical adapter is connected, **When** the user launches the tool in test/demo mode, **Then** the tool starts with a mock backend and generates simulated CAN traffic.
2. **Given** test mode is active, **When** the user applies filters, records, or inspects logs, **Then** all features behave identically to a real session.
3. **Given** test mode is active, **When** the user views session status, **Then** the display clearly indicates the session is running against a simulated backend.

---

### Edge Cases

- **USB adapter unplugged during session**: The tool MUST detect the USB disconnection, stop the session gracefully, finalize any active recording, and report the event clearly.
- **Silent bus (no traffic)**: The tool MUST remain connected and waiting, display a "no frames received" status, and not time out or error.
- **Extremely busy bus (sustained high frame rate)**: The tool MUST continue logging without dropping frames up to the adapter's throughput limit, and indicate if the adapter reports overflow.
- **Duplicate recording request**: The tool MUST reject a second concurrent recording request with a clear error message explaining that a recording is already active — never silently drop data.
- **Disk space exhaustion during recording**: The tool MUST detect the condition, stop recording gracefully, and warn the user with a clear message.
- **Future-version log file opened**: The tool MUST report a version mismatch clearly rather than silently misinterpreting the data.

## Requirements *(mandatory)*

### Functional Requirements

- **FR-001**: The system MUST discover and list all J2534 providers installed on the host system.
- **FR-002**: The system MUST allow the user to select a specific J2534 provider and establish a device connection.
- **FR-003**: The system MUST allow the user to cleanly disconnect from a connected device at any time.
- **FR-004**: The system MUST allow the user to open a CAN channel at a specified bitrate (at minimum: 250 kbps, 500 kbps, 1 Mbps).
- **FR-005**: The system MUST display received CAN frames in real time with: monotonic timestamp, arbitration ID, frame type (standard/extended), DLC, and raw payload bytes.
- **FR-006**: The system MUST support continuous frame reception without user-initiated polling (stream mode).
- **FR-007**: The system MUST allow the user to stop and restart a CAN session without disconnecting from the device.
- **FR-008**: The system MUST support ID-based filtering — the user can specify one or more arbitration IDs or ID/mask pairs to include or exclude.
- **FR-009**: The system MUST allow the user to start and stop recording at any time during an active session.
- **FR-010**: The system MUST save recorded frames in at least one human-readable text format and one structured format (JSON or CSV).
- **FR-011**: Each recorded frame MUST include: timestamp, channel identifier, arbitration ID, frame type, DLC, and unmodified payload bytes.
- **FR-012**: The system MUST allow the user to open a previously saved log file and inspect its contents offline (without a device connection).
- **FR-013**: The system MUST allow the user to search or filter within a loaded log file by arbitration ID.
- **FR-014**: The system MUST expose session information at any time: connection state, bitrate, session duration, frame counts, error counts, recording status, and active filters.
- **FR-015**: The system MUST default to passive monitoring mode — no frames are transmitted unless the user explicitly enables transmission.
- **FR-016**: The system MUST provide a test/demo mode using a simulated backend that generates mock CAN traffic, allowing all features to work without hardware.
- **FR-017**: The system MUST report clear, actionable error messages for: connection failures, channel open failures, adapter disconnection, unsupported bitrates, file I/O errors, and format mismatches.
- **FR-018**: The system MUST support both human-readable (text) and machine-readable (JSON/CSV) output on the console.
- **FR-019**: The system MUST indicate when the adapter reports dropped frames or bus errors.
- **FR-020**: The system MUST allow the user to specify a custom file name or location for recorded captures.
- **FR-021**: The system MUST allow users to assign human-readable labels to arbitration IDs and persist those labels across sessions, enabling progressive identification of unknown traffic.

### Key Entities

- **J2534 Provider**: A registered interface provider on the host system. Has a name and represents the ability to connect to a USB-attached physical adapter. Multiple providers may be present.
- **Device Connection**: An active USB link between the tool and a J2534 adapter. Has a state (connected/disconnected), an associated provider, and adapter metadata (name, version).
- **CAN Channel**: A logical channel opened on a connected device at a specific bitrate. Has a state (open/closed/error), a configured bitrate, and read/write capabilities.
- **CAN Frame**: A single message observed on the bus. Contains: timestamp, channel, arbitration ID, frame type, DLC, payload bytes. Immutable once captured.
- **Filter**: A user-defined rule that determines which frames are visible or captured. Defined by arbitration ID values, masks, or ranges. Can be applied to display, recording, or both.
- **Recording Session**: A period during which captured frames are written to a log file. Has a start time, end time, frame count, file path, and format.
- **Log File**: A saved capture in text, JSON, or CSV format. Contains frame data and session metadata (adapter, bitrate, duration, timestamps). Can be opened for offline inspection.
- **Session Status**: A live summary of the current operating state: connection info, channel state, bitrate, frame counters, error counters, recording status, active filters.

## Success Criteria *(mandatory)*

### Measurable Outcomes

- **SC-001**: A user can go from launching the tool to viewing live CAN frames in under 60 seconds, assuming the adapter is connected and the vehicle ignition is on.
- **SC-002**: The tool captures frames without data loss under normal CAN bus load (up to 100% bus utilization at the configured bitrate, limited only by the adapter's own throughput).
- **SC-003**: Every captured frame in a log file is bit-identical to what was observed on the bus — no payload truncation, reordering, or field omission.
- **SC-004**: A user with no prior experience with the tool can complete their first capture by following the repository documentation alone, without external help.
- **SC-005**: Offline log inspection loads a 1-million-frame capture and allows ID search within 5 seconds on a system with ≥8 GB RAM and SSD storage.
- **SC-006**: The tool produces identical log output for identical input traffic — deterministic, reproducible captures.
- **SC-007**: All error conditions produce a user-facing message that identifies the problem and suggests a corrective action (no silent failures, no raw stack traces).
- **SC-008**: The test/demo mode exercises all user-facing features identically to a real hardware session.
- **SC-009**: The tool runs a complete capture session with zero crashes or hangs, recovering gracefully from adapter disconnection or bus errors.
- **SC-010**: (Aspirational — measured post-MVP) 90% of users in the target personas can successfully complete a capture-filter-save-review workflow on their first attempt. Not validated in MVP; measurement method to be defined when usability testing is planned.

## Safety Behavior

The tool defaults to **passive monitoring** — no frames leave the host unless the user explicitly enables transmission.

- On startup, the tool is always in receive-only mode.
- The tool clearly indicates its current mode (passive / active) in the console and session status.
- Any future active operation (frame injection, diagnostic request) MUST require an explicit command or flag.
- Users are warned before any action that causes frames to be transmitted on the bus.
- No ECU write, flash, or coding functionality is included in the MVP.

## Usability Goals

- **Quick startup**: The user should be capturing frames within seconds of launching the tool.
- **Minimal configuration**: Sensible defaults for bitrate, output format, and filter state. The user can start with zero configuration and refine as needed.
- **Clear status feedback**: The tool always communicates what it is doing — connecting, monitoring, recording, idle, error.
- **Predictable behavior**: Same inputs produce same outputs. No hidden state changes.
- **Actionable errors**: Every error message tells the user what went wrong and what to try next.

## MVP Scope

### Included in MVP

1. Discover and connect to a J2534 interface.
2. Open a CAN channel at common bitrates (250 kbps, 500 kbps, 1 Mbps).
3. View CAN frames in real time with full field detail.
4. Filter displayed traffic by arbitration ID.
5. Record traffic to a log file (text + structured format).
6. Open and inspect previously saved logs offline.
7. View session information and health status.
8. Test/demo mode with mock backend.
9. Clear error messages and diagnostics.

### Excluded from MVP (Future Scope)

- ECU flashing or reprogramming.
- Coding or adaptation value changes.
- Automated OBD-II or UDS diagnostics.
- Vehicle-specific decoders or brand profiles (Ducati, Mazda, etc.).
- DBC file import or named signal decoding.
- ISO-TP segmentation or reassembly.
- Frame transmission or injection.
- Graphical user interface.
- Live signal charts or visualizations.
- Scripting or automation APIs.
- Plugin system for third-party extensions.

## Assumptions

- The user has a USB-connected J2534-compatible adapter and its provider/driver installed on their system. Serial, Bluetooth, and Wi-Fi adapters are not supported.
- The host system is a desktop computer running a supported operating system (Windows is the primary target, given J2534's Windows-centric ecosystem).
- The vehicle's CAN bus is accessible via the OBD-II port or a direct harness connection.
- Common CAN bitrates (250 kbps, 500 kbps, 1 Mbps) cover the majority of target vehicles; exotic bitrates are not required for MVP.
- The J2534 adapter correctly implements the SAE J2534 standard; adapter firmware bugs are out of scope but should be surfaced through clear diagnostics.
- Log file sizes for typical sessions (minutes to hours) are manageable on modern storage; multi-gigabyte archival captures are out of MVP scope.
- The tool operates as a single-user, single-device application in MVP; multi-adapter and multi-channel support are future scope.
