# CANmatik — J2534 CAN Bus Scanner & Logger

An open-source desktop tool that connects to vehicles via a USB J2534 Pass-Thru
adapter, captures CAN bus traffic in real time, and saves it for later analysis.

> **Safety**: CANmatik defaults to **passive monitoring mode**. No frames are
> transmitted to the vehicle bus unless you explicitly enable active mode in a
> future release. See [SAFETY.md](SAFETY.md) for details.

## Features

- **Discover** installed J2534 providers and USB-attached adapters
- **Monitor** raw CAN frames in real time with timestamps, IDs, DLC, and payload
- **Filter** traffic by arbitration ID (exact, range, mask, pass/block)
- **Record** sessions to Vector ASC or JSON Lines format
- **Replay** and inspect previously captured logs offline
- **Demo mode** — exercise all features without hardware via a mock backend
- **Label** arbitration IDs with human-readable names for progressive reverse engineering

## Prerequisites

- Windows 10 or later (64-bit OS, 32-bit application)
- A USB J2534-compatible adapter (e.g., Tactrix OpenPort 2.0) with its
  driver/provider installed
  - Only USB adapters are supported; serial, Bluetooth, and Wi-Fi are out of scope
- [MSYS2](https://www.msys2.org/) with the **i686** (32-bit) MinGW toolchain:

```bash
# In MSYS2 terminal:
pacman -S mingw-w64-i686-gcc mingw-w64-i686-cmake mingw-w64-i686-make
```

- Git (for cloning and submodules)

## Build

```powershell
git clone --recursive https://github.com/<org>/canmatik.git
cd canmatik

cmake -B build -G "MinGW Makefiles" -DCMAKE_BUILD_TYPE=Release
cmake --build build --parallel

# Run tests
cd build
ctest --output-on-failure
cd ..
```

The binary is at `build/canmatik.exe`.

## Quick Start

### With a real adapter

```powershell
# 1. Discover providers
.\build\canmatik.exe scan

# 2. Monitor traffic
.\build\canmatik.exe monitor --provider "Tactrix OpenPort 2.0" --bitrate 500000

# 3. Filter by ID
.\build\canmatik.exe monitor --provider "Tactrix OpenPort 2.0" --filter 0x7E8

# 4. Record to file
.\build\canmatik.exe record --provider "Tactrix OpenPort 2.0" --output captures/session.asc

# 5. Replay offline
.\build\canmatik.exe replay captures/session.asc --summary
```

### Without hardware (demo mode)

```powershell
.\build\canmatik.exe demo
.\build\canmatik.exe demo --trace captures/session.asc
```

### Machine-readable output

```powershell
.\build\canmatik.exe monitor --provider "Tactrix OpenPort 2.0" --json | jq 'select(.id == "7E8")'
```

### Label IDs for easier reading

```powershell
.\build\canmatik.exe label set 0x7E8 "Engine RPM"
.\build\canmatik.exe label list
```

## Project Structure

```
src/
├── core/          # Platform-agnostic domain (CanFrame, FilterEngine, etc.)
├── transport/     # IDeviceProvider / IChannel interfaces
├── platform/      # Windows J2534 implementation
├── mock/          # Mock backend for testing and demo
├── services/      # SessionService, CaptureService, RecordService, ReplayService
├── logging/       # ASC and JSONL writers/readers
├── cli/           # CLI frontend (CLI11)
├── gui/           # ImGui frontend (future)
└── config/        # Configuration loading
third_party/
├── tinylog/       # Diagnostic logger (git submodule)
└── CMakeLists.txt # Builds vendored deps as static libraries
tests/
├── unit/          # Catch2 unit tests
└── integration/   # End-to-end tests with mock backend
```

## Technology

| Component | Choice |
|-----------|--------|
| Language | C++20 (GCC ≥ 13) |
| Build | CMake ≥ 3.24, MinGW Makefiles |
| Target | Windows 10+, 32-bit (for J2534 DLL compatibility) |
| CLI | CLI11 2.4 |
| JSON | nlohmann/json 3.11 |
| Testing | Catch2 v3 |
| Diagnostic logging | TinyLog (git submodule) |
| Log formats | Vector ASC (text), JSON Lines (structured) |

## Exit Codes

| Code | Meaning |
|------|---------|
| 0 | Success |
| 1 | User error (bad arguments, invalid config) |
| 2 | Hardware/runtime failure |

## Contributing

See [CONTRIBUTING.md](CONTRIBUTING.md) for development setup, coding standards,
and the pull request workflow.

## License

TBD
