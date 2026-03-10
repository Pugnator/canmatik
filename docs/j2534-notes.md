# J2534 Implementation Notes

Technical notes on the SAE J2534 Pass-Thru integration in CANmatik.
Aimed at contributors working on the `src/platform/win32/` layer.

---

## Architecture Overview

```
┌──────────────┐   ┌─────────────────┐   ┌─────────────────┐
│ IChannel     │◄──│ J2534Channel    │──►│ J2534DllLoader  │
│ (interface)  │   │ (CAN read/write)│   │ (RAII DLL wrap) │
└──────────────┘   └─────────────────┘   └─────────────────┘
                          ▲                       ▲
┌──────────────┐   ┌──────┴──────────┐   ┌───────┴─────────┐
│ IDeviceProvider│◄─│ J2534Provider  │──►│ RegistryScanner │
│ (interface)  │   │ (open/close dev)│   │ (RegOpenKeyExA) │
└──────────────┘   └─────────────────┘   └─────────────────┘
```

## Registry Discovery

`RegistryScanner::scan()` reads from:

```
HKEY_LOCAL_MACHINE\SOFTWARE\PassThruSupport.04.04
```

Key flags:
- `KEY_READ | KEY_WOW64_32KEY` — ensures 32-bit view on WoW64

Each subkey represents one installed J2534 provider. Values read:

| Value | Type | Usage |
|-------|------|-------|
| `Name` | REG_SZ | Display name (falls back to subkey name) |
| `Vendor` | REG_SZ | Vendor identification |
| `FunctionLibrary` | REG_SZ | **Full path** to the J2534 DLL |
| `CAN` | REG_DWORD | CAN protocol support (0/1) |
| `ISO15765` | REG_DWORD | ISO 15765 support (0/1) |

Providers without a `FunctionLibrary` value are skipped.

## DLL Loading (`J2534DllLoader`)

RAII wrapper using `LoadLibraryA` / `GetProcAddress` / `FreeLibrary`.

### Resolved Exports

| Function | Mandatory | Description |
|----------|:---------:|-------------|
| `PassThruOpen` | ✅ | Open device by name |
| `PassThruClose` | ✅ | Close device handle |
| `PassThruConnect` | ✅ | Open a protocol channel |
| `PassThruDisconnect` | ✅ | Close a protocol channel |
| `PassThruReadMsgs` | ✅ | Read messages from channel |
| `PassThruWriteMsgs` | ✅ | Write messages to channel |
| `PassThruStartMsgFilter` | ✅ | Install message filter |
| `PassThruStopMsgFilter` | ✅ | Remove message filter |
| `PassThruIoctl` | ✅ | I/O control operations |
| `PassThruGetLastError` | ✅ | Retrieve error description |
| `PassThruReadVersion` | ❌ | Read firmware/DLL/API version |
| `PassThruStartPeriodicMsg` | ❌ | Start periodic message |
| `PassThruStopPeriodicMsg` | ❌ | Stop periodic message |

Loading fails with `TransportError` if any mandatory export is missing.

### Calling Convention

All J2534 functions use `__stdcall` (`WINAPI`). The function pointer
typedefs in `j2534_defs.h` are declared accordingly.

### Lifecycle

```
J2534DllLoader loader;
loader.load("C:\\path\\to\\j2534.dll");  // LoadLibraryA + resolve all
// ... use loader.PassThruOpen, etc.
loader.unload();                          // FreeLibrary (also in dtor)
```

The loader is **move-only** (no copy). Owned by `J2534Provider`.

## Channel Implementation (`J2534Channel`)

### Open Sequence

1. `PassThruConnect(device_id, CAN, CAN_ID_BOTH, bitrate)` — opens
   channel for both 11-bit and 29-bit IDs
2. `PassThruStartMsgFilter(PASS, mask=0, pattern=0)` — pass-all filter
   so all frames are received (some adapters require this)
3. `PassThruIoctl(CLEAR_RX_BUFFER)` — flush stale data

### Read Batch

`PassThruReadMsgs` retrieves up to 16 messages per call. Processing:

- **TX echo suppression**: frames with `RX_MSG_TX_DONE` flag are skipped
- **Timestamp handling**: dual-timestamp strategy:
  - `adapter_timestamp_us` — from `PASSTHRU_MSG.Timestamp` (µs, 32-bit
    with rollover tracking at 2³² µs ≈ 71 minutes)
  - `host_timestamp_us` — `std::chrono::steady_clock` immediately after
    successful read
- **ID extraction**: from `Data[0..3]` (big-endian), masked to 11 or
  29 bits based on `RxStatus & CAN_29BIT_ID`

### Write

`write()` builds a `PASSTHRU_MSG` from a `CanFrame` (inverse of the read
conversion) and calls `PassThruWriteMsgs` with a 100ms timeout:

- `Data[0..3]` = arbitration ID (big-endian)
- `Data[4..]` = payload (up to 8 bytes, classic CAN)
- `TxFlags` = `CAN_29BIT_ID` for extended frames, `0` for standard
- Timeout and buffer-full errors are marked recoverable; all others are fatal

### Hardware Filtering

`setFilter()` installs a J2534 PASS filter with the given mask/pattern.
`clearFilters()` removes it and reinstalls the pass-all filter.

If hardware filter setup fails (some adapters don't support it well),
the failure is logged as a warning and software-level filtering in
`CaptureService` takes over.

## Error Handling

Every J2534 API call is logged at `DEBUG` level with parameters and
return code per Constitution Principle II (Faithful Capture).

Error retrieval:
```cpp
char err_buf[256] = {};
dll_.PassThruGetLastError(err_buf);
```

Errors are translated to `TransportError` with:
- `code` — the J2534 status code
- `source` — the J2534 function name
- `recoverable` — `true` for transient errors (buffer empty, timeout),
  `false` for fatal errors (device disconnected)

## 32-bit Build Requirement

J2534 DLLs are almost universally 32-bit. A 64-bit process cannot
`LoadLibraryA` a 32-bit DLL. CANmatik must be built with the MSYS2
MinGW 32-bit (i686) toolchain:

```bash
# From MSYS2 MinGW 32-bit shell:
cmake -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build
```

The CMake configuration detects 64-bit builds and warns.
