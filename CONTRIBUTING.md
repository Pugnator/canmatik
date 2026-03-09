# Contributing to CANmatik

Thank you for your interest in contributing to CANmatik! This document covers
the development setup, coding standards, and pull request workflow.

## Development Setup

### Prerequisites

- Windows 10 or later
- [MSYS2](https://www.msys2.org/) with the **i686** (32-bit) MinGW toolchain
- Git

### Toolchain Installation

```bash
# In MSYS2 terminal:
pacman -S mingw-w64-i686-gcc mingw-w64-i686-cmake mingw-w64-i686-make
```

### Clone and Build

```powershell
git clone --recursive https://github.com/<org>/canmatik.git
cd canmatik
cmake -B build -G "MinGW Makefiles" -DCMAKE_BUILD_TYPE=Debug
cmake --build build --parallel
```

### Run Tests

```powershell
cd build
ctest --output-on-failure
```

## Project Architecture

CANmatik follows a layered architecture. Understanding which layer your change
affects helps keep the codebase clean:

| Layer | Directory | Purpose |
|-------|-----------|---------|
| Core | `src/core/` | Platform-agnostic value types and logic |
| Transport | `src/transport/` | Hardware abstraction interfaces |
| Platform | `src/platform/win32/` | Windows J2534 implementation |
| Mock | `src/mock/` | Test/demo backend |
| Services | `src/services/` | Application services (shared by CLI & GUI) |
| Logging | `src/logging/` | Log format writers/readers |
| CLI | `src/cli/` | Command-line interface |
| GUI | `src/gui/` | Graphical interface (future) |
| Config | `src/config/` | Configuration loading |

**Key rule**: No layer may import from a layer above it in this stack. Core code
must not reference platform, CLI, or GUI code.

## Coding Standards

### C++20

- Use C++20 features where they improve clarity (`std::format`, `std::optional`,
  structured bindings, etc.)
- Compiler: MinGW-w64 GCC ≥ 13
- Standard: C++20 with no extensions (`CMAKE_CXX_EXTENSIONS OFF`)

### Naming Conventions

| Element | Style | Example |
|---------|-------|---------|
| Classes/Structs | PascalCase | `CanFrame`, `FilterEngine` |
| Functions/Methods | snake_case | `open_channel()`, `write_frame()` |
| Variables | snake_case | `frame_count`, `adapter_name` |
| Constants | UPPER_SNAKE | `MAX_PAYLOAD_SIZE` |
| Enum values | PascalCase | `FrameType::Standard` |
| Files | snake_case | `can_frame.h`, `session_service.cpp` |
| Interfaces | I-prefix PascalCase | `IDeviceProvider`, `IChannel` |

### Headers

- Use `#pragma once` for header guards
- Include what you use — no transitive dependency reliance
- Prefer forward declarations where possible

### Safety

- Never add frame transmission code without safety gates (see Constitution
  Principle I — Read First, Transmit Later)
- `IChannel::write()` must always enforce mode checks
- Any new active feature requires `--active` flag + user confirmation

## Pull Request Workflow

1. **Branch** from `main` with a descriptive name: `feature/filter-engine`,
   `fix/timestamp-rollover`
2. **Write tests** — all new logic must have unit tests. Use Catch2 BDD macros
   (`SCENARIO` / `GIVEN` / `WHEN` / `THEN`) where they map to spec acceptance
   scenarios.
3. **Build and test locally**: `cmake --build build && cd build && ctest`
4. **Constitution compliance note** — every PR description must include a brief
   note on which constitution principles are affected, even if the answer is
   "no principles affected." This is required by the project governance.
5. **One concern per PR** — keep PRs focused. A filter engine change should not
   also refactor the CLI parser.
6. **Respond to review** — be responsive to feedback and willing to adjust.

### PR Description Template

```
## Summary
Brief description of what this PR does.

## Constitution Compliance
- [ ] No principles affected
- [ ] Principle(s) affected: <list which and why compliance is maintained>

## Testing
- [ ] Unit tests added/updated
- [ ] Integration tests added/updated (if applicable)
- [ ] Manual testing performed (describe)
```

## Commit Messages

Use conventional commit-style messages:

```
feat: add ID range filter support
fix: handle timestamp rollover past 71 minutes
test: add FilterEngine mask match tests
docs: update adapter compatibility notes
refactor: extract DLL loader from J2534Provider
```

## Questions?

Open an issue or start a discussion. We're happy to help contributors get
oriented in the codebase.
