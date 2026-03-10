<!--
  Sync Impact Report
  ──────────────────
  Version change : N/A → 1.0.0 (initial ratification)
  Modified principles: N/A (all new)
  Added sections :
    - Core Principles (8 principles)
    - Architecture & Safety Constraints
    - Development Workflow & Standards
    - Governance
  Removed sections: N/A
  Templates requiring updates:
    - .specify/templates/plan-template.md        ✅ compatible (no changes needed)
    - .specify/templates/spec-template.md         ✅ compatible (no changes needed)
    - .specify/templates/tasks-template.md        ✅ compatible (no changes needed)
  Follow-up TODOs: none
-->

# J2534 CAN Bus Scanner Constitution

## Core Principles

### I. Read First, Transmit Later

Passive observation is the default operating mode. The system MUST
start every session in receive-only state. Any active transmission
(frame injection, diagnostic request, or replay) MUST be:

- explicitly requested by the user through a distinct command or flag,
- clearly visible in the console output and log stream, and
- gated behind a confirmation step when the action could alter ECU state.

**Rationale**: CAN buses carry safety-critical traffic. Unintended
writes can brick ECUs, trigger limp mode, or cause physical harm.
Default-passive eliminates an entire class of accidental damage.

### II. Open and Inspectable

All significant behavior MUST be traceable to source code that is
publicly available under the project's open-source license. Specifically:

- No opaque binary blobs for decoding or protocol logic.
- Every filter, transformation, and decode step MUST be documented
  or self-documenting in code.
- Third-party J2534 DLLs are the only permitted closed-source
  dependency, and interaction with them MUST be logged at debug level.

**Rationale**: Users connecting to vehicles they own deserve full
visibility into what the tool does on the bus.

### III. Vehicle-Agnostic Core

The core architecture MUST NOT hardcode any brand-specific knowledge
(Ducati, Mazda, or otherwise). Brand-specific details MUST reside in
isolated adapter modules, profile files, or decoder plugins that are
loaded at runtime.

- Core libraries MUST compile and pass tests with zero brand modules
  present.
- Brand modules MUST declare which CAN IDs, protocols, and ECU
  addresses they claim.

**Rationale**: A vehicle-agnostic core keeps the project extensible
to any CAN-equipped vehicle without accumulating tangled conditionals.

### IV. Deterministic Logging

Every captured frame MUST be recorded with at minimum:

| Field       | Requirement                              |
|-------------|------------------------------------------|
| Timestamp   | Monotonic, microsecond resolution or better |
| Channel     | Logical channel identifier               |
| Arbitration ID | Full 11-bit or 29-bit ID              |
| Frame type  | Standard / Extended / FD / Error / Remote |
| DLC         | Data Length Code as observed              |
| Payload     | Raw bytes, unmodified                    |

No field may be omitted, truncated, or re-ordered by default.
Log output MUST be reproducible given the same input stream.

**Rationale**: Reliable reverse engineering depends on bit-perfect,
timestamped captures. Lossy or reordered logs waste hours.

### V. Safe by Design

The system MUST clearly distinguish three operating modes:

1. **Passive sniffing** — receive only, no frames transmitted.
2. **Active querying** — diagnostic requests sent, responses expected.
3. **Active injection** — arbitrary frames placed on the bus.

Transition from mode 1 to mode 2 or 3 MUST:

- require explicit opt-in (flag, command, or confirmation prompt),
- be visible in console output and persisted logs,
- include a user-facing warning describing potential risks, and
- default to the safest possible behavior on ambiguity.

No ECU write, flash, or coding functionality is permitted in the MVP.

**Rationale**: A CAN tool that can silently write to the bus is a
liability. Safety modes make risk explicit and auditable.

### VI. Incremental Reverse Engineering

The system MUST be useful even when message definitions are unknown.
Specifically:

- Unknown arbitration IDs MUST be displayed and logged without error.
- The absence of a decode definition MUST NOT block capture or
  filtering.
- Users MUST be able to annotate, label, or tag unknown IDs
  progressively.

**Rationale**: Reverse engineering is iterative. Forcing upfront
knowledge of every ID makes the tool useless for discovery.

### VII. CLI-First Foundation

Core capture, filtering, and analysis features MUST be fully
operable from the command line before any GUI is built.

- Every user-facing action MUST be invocable via CLI arguments or
  an interactive CLI session.
- Output MUST support both human-readable (text) and
  machine-readable (JSON / CSV) formats on stdout.
- Exit codes MUST distinguish success, user error, and hardware
  failure.

A graphical interface is an optional future layer that consumes
the same libraries; it MUST NOT introduce functionality absent
from the CLI.

**Rationale**: CLI-first ensures scriptability, testability, and
headless operation — all critical for vehicle diagnostics workflows.

### VIII. Portable Architecture

The codebase MUST maintain clear separation between the following
layers:

1. **J2534 transport** — DLL loading, device enumeration, raw API calls.
2. **CAN session** — channel management, bitrate config, frame I/O.
3. **Logging / storage** — file writers, format converters, replay readers.
4. **Decoding** — DBC import, brand profiles, message interpretation.
5. **UI / CLI** — presentation, user interaction, output formatting.

Each layer MUST be independently testable. No layer may import from
a layer above it in this stack. Vendor-specific J2534 handling MUST
sit behind a narrow abstraction so the rest of the stack can be
tested with a mock backend.

**Rationale**: Clean layering lets contributors work on decoding
without touching transport code, and lets CI run without hardware.

## Architecture & Safety Constraints

### Functional Requirements

The system MUST support:

- Loading a J2534 DLL / provider at runtime.
- Enumerating available J2534 devices and providers.
- Opening and closing device connections.
- Opening CAN channels with configurable bitrate.
- Reading raw CAN frames continuously.
- Optional transmission of raw CAN frames (gated per Principle I).
- Frame filtering by arbitration ID and mask where the adapter
  supports it.
- Saving logs in at least one human-readable text format and one
  structured format (JSON or CSV).
- Replaying previously captured logs in an offline mode.
- Protocol and session diagnostics output for troubleshooting.

### MVP Scope

The MVP MUST deliver:

- A CLI application implementing all functional requirements above.
- J2534 device open / close.
- CAN channel open at common bitrates (250 kbps, 500 kbps, 1 Mbps).
- Continuous read loop with human-readable console output.
- File logging (text + structured).
- Simple ID-based filters.
- Robust error reporting with clear diagnostics.
- A test mode with a mocked J2534 backend for CI and development.

The MVP MUST NOT include ECU write, flash, or coding features.

### Quality Requirements

- The build MUST be reproducible from a documented set of
  dependencies and toolchain versions.
- Non-hardware logic MUST have unit test coverage.
- Hardware-dependent code paths MUST include clear diagnostics and
  graceful failure (no silent crashes).
- Logs and error messages MUST help debug incompatible adapters,
  unexpected protocols, and ECU-level failures.
- The codebase MUST be structured so that a new contributor can add
  a decoder or adapter without modifying core libraries.

### Observability

The tool MUST expose at runtime:

- Adapter / provider name and version.
- Selected protocol and bitrate.
- Channel state (closed / open / error).
- Read and write frame counters.
- Error counters (bus errors, overflow, dropped frames).
- Timestamps on every frame and event.
- Dropped-frame indications where the adapter reports them.

### Storage Formats

Priority order for log formats:

1. **Text log** — human-readable, one line per frame minimum.
2. **JSON or CSV** — structured, suitable for automated tooling.
3. **Binary capture** (optional, deferred) — compact archival format.

All formats MUST be documented in the repository.

## Development Workflow & Standards

### GitHub & Open-Source Expectations

The repository MUST include:

- `README.md` with supported use cases and quick-start instructions.
- Hardware assumptions and known caveats for tested adapters.
- A safety notice visible in the README and CLI help output.
- Build instructions reproducible on a clean checkout.
- Example CLI commands.
- Sample log files.
- Contribution guidelines (`CONTRIBUTING.md`).

### Decision Rules

When facing trade-offs the team MUST prefer, in order:

1. Reliability over feature count.
2. Passive capture over active probing.
3. Simple architecture over premature abstraction.
4. Documented unknowns over false confidence.
5. Generic extensibility over one-off hacks.

### Non-Goals for MVP

The MVP MUST NOT:

- Flash ECUs or modify coding / adaptation values.
- Send unsafe write commands by default.
- Implement brand-specific reverse-engineered features before
  stable raw capture exists.
- Depend on closed-source SDKs beyond the standard J2534 DLL
  interface.

### Future Extensions (post-MVP roadmap)

Possible later phases may include:

- ISO-TP segmentation helpers.
- UDS request / response support.
- DBC file import for named signal decoding.
- Decoder plugin system.
- Ducati and Mazda MX-5 profile modules.
- Graphical interface.
- Live charts and signal visualization.
- Scripting API.

### Definition of Done — MVP

The MVP is complete when a user can:

1. Connect to a J2534 device.
2. Open a CAN channel at a chosen bitrate.
3. Sniff traffic reliably without frame loss under normal load.
4. Apply ID-based filters.
5. Save captured frames to text and structured log files.
6. Inspect logs offline.
7. Troubleshoot failures through clear diagnostic messages.
8. Build and run the tool from the documented GitHub repository.

## Governance

This constitution is the highest-authority document for the
CANmatik project. All plans, specifications, and code reviews
MUST verify compliance with the principles defined above.

### Amendment Procedure

1. Propose an amendment as a pull request modifying this file.
2. The PR description MUST state which principles or sections
   change, and the rationale.
3. At least one maintainer MUST approve the amendment.
4. On merge, increment `CONSTITUTION_VERSION` per semver:
   - **MAJOR**: Principle removed or redefined incompatibly.
   - **MINOR**: New principle or section added, or material
     expansion of existing guidance.
   - **PATCH**: Clarification, wording fix, or non-semantic
     refinement.
5. Update `LAST_AMENDED_DATE` to the merge date.

### Compliance Review

- Every pull request MUST include a brief constitution-compliance
  note (even if "no principles affected").
- Quarterly, maintainers SHOULD review this document against actual
  project practice and propose amendments for drift.

### Complexity Justification

Any deviation from the principles above MUST be justified in
writing (PR description or ADR) and approved by a maintainer.
Unjustified deviations are grounds for PR rejection.

**Version**: 1.0.0 | **Ratified**: 2026-03-08 | **Last Amended**: 2026-03-08
