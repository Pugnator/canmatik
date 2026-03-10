# Safety Notice

## Passive by Default

CANmatik **always starts in passive monitoring mode**. No CAN frames are
transmitted to the vehicle bus unless you explicitly enable active mode.

The MVP release does **not** include any transmission, ECU writing, flashing,
or programming capabilities. It is safe to use for passive observation.

## What This Means

| Mode | Behavior | Frames Sent? |
|------|----------|:---:|
| **Passive** (default) | Receive and log only | No |
| Active Query (future) | Diagnostic request/response | Yes — gated |
| Active Inject (future) | Arbitrary frame transmission | Yes — gated |

## Safety Guarantees in the MVP

1. The tool **cannot** send frames to the bus — the code path does not exist.
2. `IChannel::write()` **rejects** all calls when the operating mode is `Passive`.
3. The console always displays `[PASSIVE]` or `[ACTIVE]` to show the current mode.
4. No ECU write, flash, or coding functionality is included.

## When Active Mode is Added (Future)

Transitioning from passive to any active mode will require:

- An explicit CLI flag (`--active`) or GUI toggle
- User confirmation prompt
- A visible mode change logged in the capture stream
- Clear warnings describing potential risks

## Your Responsibility

> **CAN bus tools interact with vehicle safety systems.** Always verify your
> adapter and vehicle configuration before connecting. Ensure the vehicle is
> in a safe state (parked, engine off where appropriate) before beginning
> diagnostic work.

**Use at your own risk.** The authors are not responsible for any damage to
vehicles, ECUs, or other equipment resulting from use of this tool.
