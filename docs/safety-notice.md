# Safety Notice — Technical Analysis

This document supplements the [user-facing SAFETY.md](../SAFETY.md) with
implementation-level details aimed at contributors and code reviewers.

## Operating Mode Design

CANmatik defines two operating modes in `OperatingMode` (session_status.h):

| Mode | Description |
|------|-------------|
| `Passive` | Receive-only. Default on every session start. |
| `Active` | Request/response or arbitrary TX. **Not implemented in MVP.** |

The CLI always displays a mode badge: `[PASSIVE]` or `[ACTIVE]`.

## Write-Guard Implementation (Defense in Depth)

Both channel implementations unconditionally reject writes
in the MVP:

- **`J2534Channel::write()`** (`src/platform/win32/j2534_channel.cpp`):
  Throws `TransportError` with a clear message before any J2534 API call.

- **`MockChannel::write()`** (`src/mock/mock_channel.cpp`):
  Throws `TransportError` — the mock mirrors real-hardware behavior.

This is a defense-in-depth control: even if a future code path
accidentally calls `write()`, the frame will never reach the bus.

## Active-Mode Transition Requirements (Future)

Transitioning from `Passive` to `Active` will require ALL of:

1. An explicit CLI flag (`--active`) or GUI toggle
2. A user-confirmation prompt
3. A visible `TX` marker in the capture stream for every transmitted frame
4. An audit log entry recording the mode change and user acknowledgement
5. `[ACTIVE]` badge displayed persistently in all output

## Risk Assessment

### Read-Only Risks (Current MVP)

| Risk | Mitigation |
|------|-----------|
| Incorrect bitrate interrupts bus | User selects bitrate; documented in quickstart |
| Adapter firmware hang | Ctrl+C + close/disconnect in destructors |
| Excessive bus load from polling | J2534 `PassThruReadMsgs` is non-intrusive |

### Active-Mode Risks (Future — Not Present)

| Risk | Mitigation |
|------|-----------|
| Unintended frame TX | Write-guard + mode badge + confirmation prompt |
| ECU reprogramming | No Ioctl programming commands in MVP; future gating |
| Bus contention | Rate-limiting in `TransmitService` (planned) |

## J2534 API Safety Boundary

The MVP uses only these J2534 calls — all are read-only:

- `PassThruOpen` / `PassThruClose` — device lifecycle
- `PassThruConnect` / `PassThruDisconnect` — channel lifecycle
- `PassThruReadMsgs` — passive frame reception
- `PassThruStartMsgFilter` / `PassThruStopMsgFilter` — hardware filtering
- `PassThruIoctl(CLEAR_RX_BUFFER)` — buffer management
- `PassThruReadVersion` — firmware version query
- `PassThruGetLastError` — error message retrieval

**Not used in MVP**: `PassThruWriteMsgs`, `PassThruStartPeriodicMsg`,
`PassThruStopPeriodicMsg`, any programming `Ioctl` commands.
