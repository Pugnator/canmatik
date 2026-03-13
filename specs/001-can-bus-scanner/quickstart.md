# Quickstart: CANmatik — J2534 CAN Bus Scanner & Logger

**Feature Branch**: `001-can-bus-scanner`
**Created**: 2026-03-08

---

## Prerequisites

- Windows 10 or later (64-bit OS, 32-bit application)
- A USB J2534-compatible adapter (e.g., Tactrix OpenPort 2.0 J2534 ISO/CAN/VPW/PWM, Drew Technologies MongoosePro) with its driver/provider installed — only USB adapters are supported; serial/Bluetooth/Wi-Fi adapters are out of scope
- Git (for cloning)
- MinGW-w64 with GCC ≥ 13 (POSIX threading model)
- CMake ≥ 3.24

### Recommended MinGW distribution

Install via [MSYS2](https://www.msys2.org/):

```powershell
# In MSYS2 terminal:
pacman -S mingw-w64-i686-gcc mingw-w64-i686-cmake mingw-w64-i686-make
```

Use the **i686** (32-bit) toolchain — J2534 DLLs are almost universally 32-bit.

---

## Build

```powershell
git clone https://github.com/<org>/canmatik.git
cd canmatik

cmake -B build -G "MinGW Makefiles" -DCMAKE_BUILD_TYPE=Release
cmake --build build --parallel

# Run tests
cd build
ctest --output-on-failure
cd ..
```

The binary is at `build/src/cli/canmatik.exe`.

---

## Quick Start — Real Hardware

### 1. Discover adapters

```powershell
.\build\src\cli\canmatik.exe scan
```

Expected output:
```
J2534 Providers:
  1. Tactrix OpenPort 2.0 J2534 ISO/CAN/VPW/PWM    (Tactrix)     CAN ISO15765
```

### 2. Monitor CAN traffic

```powershell
.\build\src\cli\canmatik.exe monitor --provider "Tactrix OpenPort 2.0 J2534 ISO/CAN/VPW/PWM" --bitrate 500000
```

Expected output (continuous, press Ctrl+C to stop):
```
[PASSIVE] Connected to Tactrix OpenPort 2.0 J2534 ISO/CAN/VPW/PWM @ 500 kbps
   +0.000000  7E8  Std  [8]  02 41 0C 1A F8 00 00 00
   +0.001234  7E0  Std  [8]  02 01 0C 00 00 00 00 00
   +0.002500  3B0  Std  [4]  A1 B2 C3 D4
   ...
^C
Session ended: 847 frames | 0 errors | 0 dropped | 5.2 s
```

### 3. Filter traffic

Show only OBD-II response IDs (0x7E8):

```powershell
.\build\src\cli\canmatik.exe monitor --provider "Tactrix OpenPort 2.0 J2534 ISO/CAN/VPW/PWM" --filter 0x7E8
```

### 4. Record to file

```powershell
.\build\src\cli\canmatik.exe record --provider "Tactrix OpenPort 2.0 J2534 ISO/CAN/VPW/PWM" --output captures/session1.asc
```

Press Ctrl+C to stop. The file is saved in Vector ASC format.

For JSON Lines format:

```powershell
.\build\src\cli\canmatik.exe record --provider "Tactrix OpenPort 2.0 J2534 ISO/CAN/VPW/PWM" --output captures/session1.jsonl --format jsonl
```

### 5. Replay a saved log

```powershell
.\build\src\cli\canmatik.exe replay captures/session1.asc
```

Summary only:

```powershell
.\build\src\cli\canmatik.exe replay captures/session1.asc --summary
```

Search for a specific ID:

```powershell
.\build\src\cli\canmatik.exe replay captures/session1.asc --search 0x7E8
```

---

## Quick Start — Demo Mode (No Hardware)

Run without an adapter using the built-in mock backend:

```powershell
.\build\src\cli\canmatik.exe demo
```

All features work identically. Traffic is simulated:

```
[PASSIVE] [MOCK] Demo session @ 500 kbps (100 fps)
   +0.000000  100  Std  [8]  DE AD BE EF CA FE 00 01
   +0.010000  200  Std  [4]  01 02 03 04
   ...
```

Replay a captured log as simulated input:

```powershell
.\build\src\cli\canmatik.exe demo --trace captures/session1.asc
```

---

## JSON Output

Add `--json` to any command for machine-readable output:

```powershell
.\build\src\cli\canmatik.exe monitor --provider "Tactrix OpenPort 2.0 J2534 ISO/CAN/VPW/PWM" --json
```

Pipe to `jq` for filtering:

```powershell
.\build\src\cli\canmatik.exe monitor --provider "Tactrix OpenPort 2.0 J2534 ISO/CAN/VPW/PWM" --json | jq 'select(.id == "7E8")'
```

---

## Configuration File

Create `canmatik.json` in the working directory for persistent settings:

```json
{
  "provider": "Tactrix OpenPort 2.0 J2534 ISO/CAN/VPW/PWM",
  "bitrate": 500000,
  "output": {
    "format": "asc",
    "directory": "./captures"
  }
}
```

CLI flags override config file values.

---

## Safety Notice

**CANmatik defaults to passive monitoring mode.** No frames are transmitted
to the vehicle bus unless you explicitly enable active mode in a future release.

The MVP does not include any transmission, ECU writing, flashing, or
programming capabilities. It is safe to use for passive observation.

When active features are added in later versions, they will require explicit
opt-in flags, user confirmation, and clear warnings.

**Use at your own risk.** CAN bus tools interact with vehicle safety systems.
Always verify your adapter and vehicle configuration before connecting.

---

## Troubleshooting

| Problem | Solution |
|---------|----------|
| `No J2534 providers found` | Check that your adapter's driver is installed. Run `regedit` and verify entries exist under `HKLM\SOFTWARE\PassThruSupport.04.04\` |
| `Device not connected` | Verify the USB cable is plugged in and the adapter powers on. Check Device Manager for the adapter. |
| `Unsupported bitrate` | Try 250000, 500000, or 1000000. Your adapter may not support all bitrates. |
| `No frames received` | Verify the vehicle ignition is on. Check that the bitrate matches the vehicle's CAN bus speed. Some vehicles require key-on/engine-running. |
| `Build error: threading` | Ensure MinGW-w64 was installed with POSIX threading model (winpthreads). Check with `gcc -v` and look for `--enable-threads=posix`. |
| `LoadLibrary failed` | Ensure you built as 32-bit (i686). A 64-bit binary cannot load 32-bit J2534 DLLs. |
