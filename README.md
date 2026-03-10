# CANmatik — J2534 CAN Bus Scanner, Logger & OBD-II Diagnostics

An open-source desktop tool that connects to vehicles via a USB J2534 Pass-Thru
adapter, captures CAN bus traffic in real time, decodes OBD-II diagnostic data,
and saves sessions for later analysis.

> **Safety**: CANmatik defaults to **passive monitoring mode**. OBD-II queries
> are the only active transmissions, and destructive operations (e.g., DTC
> clearing) require explicit `--force` confirmation. See [SAFETY.md](SAFETY.md)
> for details.

## Features

### CAN Bus Scanning & Logging
- **Discover** installed J2534 providers and USB-attached adapters
- **Monitor** raw CAN frames in real time with timestamps, IDs, DLC, and payload
- **Filter** traffic by arbitration ID (exact, range, mask, pass/block)
- **Record** sessions to Vector ASC or JSON Lines format
- **Replay** and inspect previously captured logs offline
- **Demo mode** — exercise all features without hardware via a mock backend
- **Label** arbitration IDs with human-readable names for progressive reverse engineering

### OBD-II Diagnostics
- **Query** supported PIDs and decode live engine data (RPM, speed, coolant temp, etc.)
- **Stream** multiple PID groups at configurable intervals via YAML config
- **Read & clear** Diagnostic Trouble Codes (stored and pending DTCs)
- **Vehicle info** — retrieve VIN, Calibration ID, and ECU name (Mode $09)
- **19 built-in Mode $01 PIDs** with J1979 formula decoding
- **ISO 15765-4** transport with single-frame and multi-frame (ISO-TP) support

## Prerequisites

- Windows 10 or later (64-bit OS, 32-bit application)
- A USB J2534-compatible adapter (e.g., Tactrix OpenPort 2.0) with its
  driver/provider installed
  - Only USB adapters are supported; serial, Bluetooth, and Wi-Fi are out of scope
- [MSYS2](https://www.msys2.org/) with the **i686** (32-bit) MinGW toolchain:

```bash
# In MSYS2 terminal:
pacman -S mingw-w64-i686-gcc mingw-w64-i686-cmake mingw-w64-i686-ninja
```

- Git (for cloning and submodules)

## Build

```powershell
git clone --recursive https://github.com/<org>/canmatik.git
cd canmatik

cmake -B build32 -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build32

# Run tests
cd build32
ctest --output-on-failure
cd ..
```

The binary is at `build32/canmatik.exe`.

## Quick Start

### With a real adapter

```powershell
# 1. Discover providers
.\build32\canmatik.exe scan

# 2. Monitor traffic
.\build32\canmatik.exe monitor --provider "Tactrix OpenPort 2.0" --bitrate 500000

# 3. Filter by ID
.\build32\canmatik.exe monitor --provider "Tactrix OpenPort 2.0" --filter 0x7E8

# 4. Record to file
.\build32\canmatik.exe record --provider "Tactrix OpenPort 2.0" --output captures/session.asc

# 5. Replay offline
.\build32\canmatik.exe replay captures/session.asc --summary
```

### OBD-II diagnostics

```powershell
# Query supported PIDs
.\build32\canmatik.exe obd query --supported --provider "Tactrix OpenPort 2.0"

# Stream live engine data (uses default or custom YAML config)
.\build32\canmatik.exe obd stream --provider "Tactrix OpenPort 2.0"
.\build32\canmatik.exe obd stream --obd-config obd_engine_only.yaml

# Read diagnostic trouble codes
.\build32\canmatik.exe obd dtc --provider "Tactrix OpenPort 2.0"

# Clear DTCs (requires --force)
.\build32\canmatik.exe obd dtc --clear --force --provider "Tactrix OpenPort 2.0"

# Vehicle info (VIN, Cal ID, ECU name)
.\build32\canmatik.exe obd info --provider "Tactrix OpenPort 2.0"
```

### Without hardware (demo mode)

```powershell
.\build32\canmatik.exe demo
.\build32\canmatik.exe demo --trace captures/session.asc
```

### Machine-readable output

All commands support `--json` for structured output:

```powershell
.\build32\canmatik.exe obd query --supported --mock --json
.\build32\canmatik.exe monitor --provider "Tactrix OpenPort 2.0" --json | jq 'select(.id == "7E8")'
```

### Label IDs for easier reading

```powershell
.\build32\canmatik.exe label set 0x7E8 "Engine RPM"
.\build32\canmatik.exe label list
```

## OBD-II Configuration

OBD streaming uses a YAML config file to define PID groups and polling intervals.
A default config is generated automatically on first use, or create your own:

```yaml
interval: "500ms"          # Default polling interval
addressing:
  mode: functional         # functional (0x7DF) or physical (0x7E0)
groups:
  - name: engine_basics
    interval: "200ms"
    pids:
      - id: 0x0C           # Engine RPM
      - id: 0x0D           # Vehicle Speed
      - id: 0x05           # Coolant Temperature
  - name: fuel_system
    interval: "2s"
    pids:
      - id: 0x06           # Short Term Fuel Trim
      - id: 0x10           # MAF Air Flow Rate
```

Interval formats: `500ms`, `1s`, `2hz` (valid range: 10ms–60s).
See [docs/obd-diagnostics.md](docs/obd-diagnostics.md) for the full schema and
supported PIDs table.

Sample configs are available in `samples/configs/`.

## Project Structure

```
src/
├── core/          # Platform-agnostic domain (CanFrame, FilterEngine, Result<T>)
├── transport/     # IDeviceProvider / IChannel interfaces
├── platform/      # Windows J2534 implementation
├── mock/          # Mock backend for testing and demo
├── services/      # SessionService, CaptureService, RecordService, ReplayService
├── logging/       # ASC and JSONL writers/readers
├── obd/           # OBD-II: PID tables, decoders, ISO-TP, session, scheduler
├── cli/           # CLI frontend (CLI11)
├── gui/           # ImGui frontend (future)
└── config/        # Configuration loading
third_party/
├── tinylog/       # Diagnostic logger (git submodule)
└── CMakeLists.txt # Builds vendored deps as static libraries
tests/
├── unit/          # Catch2 unit tests (153 tests)
└── integration/   # End-to-end tests with mock backend (34 tests)
```

## Technology

| Component | Choice |
|-----------|--------|
| Language | C++20 (GCC ≥ 13) |
| Build | CMake ≥ 3.24, Ninja |
| Target | Windows 10+, 32-bit (for J2534 DLL compatibility) |
| CLI | CLI11 2.4 |
| JSON | nlohmann/json 3.11 |
| YAML | yaml-cpp 0.8 (OBD config) |
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
