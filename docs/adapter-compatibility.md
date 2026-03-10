# Adapter Compatibility Guide

## Overview

CANmatik connects to vehicles via **SAE J2534 Pass-Thru** adapters. Any
adapter that provides a compliant 32-bit DLL registered in the Windows
Registry should work. In practice, behavior varies between vendors.

## Requirements

- **32-bit J2534 04.04 DLL** — the DLL must be registered under
  `HKLM\SOFTWARE\PassThruSupport.04.04`
- **CAN protocol support** — the adapter's registry key must have
  `CAN = 1` (DWORD)
- **Windows** — J2534 is a Windows-only standard; CANmatik uses the
  Win32 API (`LoadLibraryA`, `RegOpenKeyExA`)

## Tested Adapters

| Adapter | Vendor | Status | Notes |
|---------|--------|--------|-------|
| *Not yet tested* | — | — | MVP is validated with MockProvider |

> **Call for testers**: If you have a J2534 adapter, please run
> `canmatik scan` and `canmatik monitor --verbose` and share the output
> in a GitHub issue. This helps us populate this table.

## Known Compatibility Considerations

### Registry Discovery

CANmatik scans `HKLM\SOFTWARE\PassThruSupport.04.04` using the
`KEY_WOW64_32KEY` flag to ensure 32-bit keys are read correctly even
from a 32-bit process on a 64-bit OS. Each subkey yields:

| Registry Value | Type | Description |
|----------------|------|-------------|
| `Name` | REG_SZ | Display name |
| `Vendor` | REG_SZ | Vendor name |
| `FunctionLibrary` | REG_SZ | Full path to the DLL |
| `CAN` | REG_DWORD | CAN support flag (0/1) |
| `ISO15765` | REG_DWORD | ISO 15765 support flag (0/1) |

### DLL Loading

The DLL is loaded via `LoadLibraryA` and 13 standard J2534 exports are
resolved by `GetProcAddress`. The 10 mandatory exports are validated;
loading fails if any are missing:

`PassThruOpen`, `PassThruClose`, `PassThruConnect`,
`PassThruDisconnect`, `PassThruReadMsgs`, `PassThruWriteMsgs`,
`PassThruStartMsgFilter`, `PassThruStopMsgFilter`, `PassThruIoctl`,
`PassThruGetLastError`

### 32-bit Requirement

Most J2534 DLLs are compiled as 32-bit. A 64-bit process cannot load a
32-bit DLL. CANmatik is built with the MSYS2 MinGW 32-bit toolchain
(`i686`) to ensure compatibility:

```
pacman -S mingw-w64-i686-gcc mingw-w64-i686-cmake mingw-w64-i686-ninja
```

### Calling Convention

All J2534 functions use `__stdcall` (WINAPI). The function pointer
typedefs in `j2534_defs.h` are declared with `WINAPI` to match.

### Adapter Quirks

| Issue | Description | Workaround |
|-------|-------------|------------|
| Missing pass-all filter | Some adapters require explicit `PassThruStartMsgFilter` | CANmatik always installs a pass-all filter on channel open |
| TX echo in read buffer | Some adapters echo transmitted frames | CANmatik skips frames with `RX_MSG_TX_DONE` flag |
| 32-bit timestamp rollover | Adapter timestamps may wrap at 2³² µs (~71 min) | CANmatik tracks rollover and adds offset |
| No ISO 15765 | Some adapters only support raw CAN | CANmatik only requires `CAN = 1` |

## Troubleshooting

| Symptom | Possible Cause | Solution |
|---------|---------------|----------|
| `canmatik scan` returns empty | No J2534 drivers installed | Install adapter vendor software |
| `canmatik scan` returns empty | 64-bit build | Rebuild with 32-bit toolchain |
| `canmatik monitor` hangs | Wrong bitrate | Try `--bitrate 250000` |
| Connection error | DLL path in registry is stale | Reinstall adapter software |
| No frames received | Bus is silent or adapter not connected | Check wiring, verify with `--mock` |
