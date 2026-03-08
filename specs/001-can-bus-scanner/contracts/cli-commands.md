# CLI Command Contracts: CANmatik

**Feature Branch**: `001-can-bus-scanner`
**Created**: 2026-03-08

This document defines the public CLI interface contract. These commands are the
primary user-facing surface of the tool. All commands must exit with:
- `0` on success
- `1` on user error (bad arguments, invalid config)
- `2` on hardware/runtime failure

---

## Global Options

These flags are available on all subcommands:

| Flag | Type | Default | Description |
|------|------|---------|-------------|
| `--config <path>` | string | `canmatik.json` | Path to configuration file |
| `--provider <name>` | string | auto-detect | J2534 provider name |
| `--mock` | bool | false | Use mock backend instead of real hardware |
| `--json` | bool | false | Output in JSON Lines format (machine-readable) |
| `--verbose` | bool | false | Enable verbose diagnostic output |
| `--debug` | bool | false | Enable debug-level logging to file (`canmatik.log`) and stderr |
| `--help` | bool | | Show help for the command |
| `--version` | bool | | Print version and exit |

---

## `canmatik scan`

**Purpose**: Discover and list installed J2534 providers.

**Usage**:
```
canmatik scan [--json] [--verbose]
```

**Output (text)**:
```
J2534 Providers:
  1. Tactrix OpenPort 2.0    (Tactrix)     CAN ISO15765
  2. Drew Technologies       (Drew Tech)   CAN ISO15765 ISO9141
```

**Output (JSON)**:
```json
{"providers":[{"name":"Tactrix OpenPort 2.0","vendor":"Tactrix","dll_path":"C:\\...\\op20pt32.dll","supports_can":true,"supports_iso15765":true}]}
```

**Exit codes**:
- `0`: Providers found and listed
- `0`: No providers found (empty list printed with guidance message)
- `1`: Invalid arguments
- `2`: Registry access failure

---

## `canmatik monitor`

**Purpose**: Connect to a device, open a CAN channel, and display frames in real
time. This is the core passive sniffing command.

**Usage**:
```
canmatik monitor --provider <name> [--bitrate <bps>] [--filter <spec>]...
                 [--json] [--verbose]
```

**Options**:

| Flag | Type | Default | Description |
|------|------|---------|-------------|
| `--provider <name>` | string | required (or auto if single provider) | Provider to connect to |
| `--bitrate <bps>` | uint32 | `500000` | CAN bitrate: 250000, 500000, 1000000 |
| `--filter <spec>` | string[] | none (pass all) | Filter specification (see Filter Syntax below) |

**Output (text)** — one frame per line, continuous:
```
[PASSIVE] Connected to Tactrix OpenPort 2.0 @ 500 kbps
   +0.000000  7E8  Std  [8]  02 41 0C 1A F8 00 00 00
   +0.001234  7E0  Std  [8]  02 01 0C 00 00 00 00 00
   +0.002500  3B0  Std  [4]  A1 B2 C3 D4
^C
Session ended: 3 frames | 0 errors | 0 dropped | 2.5 ms
```

**Output (JSON)** — one JSON object per line:
```json
{"ts":0.000000,"id":"7E8","ext":false,"dlc":8,"data":"0241 0C1A F800 0000"}
```

**Behavior**:
- Runs until Ctrl+C (SIGINT)
- On Ctrl+C: closes channel, prints session summary, exits 0
- On adapter disconnect: prints error, finalizes, exits 2

**Exit codes**:
- `0`: Session completed normally (user stopped)
- `1`: Invalid arguments (bad bitrate, unknown provider)
- `2`: Connection failed, channel failed, adapter disconnected

---

## `canmatik record`

**Purpose**: Monitor and simultaneously record traffic to a log file.

**Usage**:
```
canmatik record --provider <name> --output <path>
               [--bitrate <bps>] [--format <fmt>] [--filter <spec>]...
               [--filter-recording] [--json] [--verbose]
```

**Options**:

| Flag | Type | Default | Description |
|------|------|---------|-------------|
| `--output <path>` | string | required | Output file path |
| `--format <fmt>` | string | `asc` | Output format: `asc`, `jsonl` |
| `--filter-recording` | bool | false | Apply display filters to recording too (default: record all) |

**Output (text)**: Same as `monitor`, plus:
```
[PASSIVE] [REC captures/session.asc] Connected to Tactrix OpenPort 2.0 @ 500 kbps
...
^C
Recording saved: captures/session.asc (1523 frames)
Session ended: 1523 frames | 0 errors | 0 dropped | 12.4 s
```

**Exit codes**: Same as `monitor`. Recording is finalized on any exit.

---

## `canmatik replay`

**Purpose**: Open a previously saved log file and inspect its contents offline.

**Usage**:
```
canmatik replay <file> [--filter <spec>]... [--summary] [--search <id>]
               [--json] [--verbose]
```

**Options**:

| Flag | Type | Default | Description |
|------|------|---------|-------------|
| `<file>` | positional | required | Path to log file (.asc or .jsonl) |
| `--summary` | bool | false | Print session summary only (no frame listing) |
| `--search <id>` | string | none | Show only frames matching this arbitration ID |

**Output (text — full)**:
```
Log: captures/session.asc
Adapter: Tactrix OpenPort 2.0 | Bitrate: 500 kbps | Duration: 12.4 s
Frames: 1523 | Unique IDs: 47

   +0.000000  7E8  Std  [8]  02 41 0C 1A F8 00 00 00
   +0.001234  7E0  Std  [8]  02 01 0C 00 00 00 00 00
   ...
```

**Output (text — summary only)**:
```
Log: captures/session.asc
Adapter: Tactrix OpenPort 2.0 | Bitrate: 500 kbps | Duration: 12.4 s
Frames: 1523 | Unique IDs: 47 | Errors: 0

ID Distribution:
  7E8    412 frames (27.1%)
  7E0    398 frames (26.1%)
  3B0    201 frames (13.2%)
  ...
```

**Exit codes**:
- `0`: File loaded and displayed
- `1`: Invalid arguments, file not found
- `2`: File corrupt or unsupported format

---

## `canmatik status`

**Purpose**: Show current session information (when piped from another instance,
or as a one-shot diagnostic).

**Usage**:
```
canmatik status [--provider <name>] [--json]
```

In MVP this command likely queries the same provider for connectivity info
rather than connecting to a running instance. Primary use: verify adapter is
reachable before starting a session.

**Output (text)**:
```
Provider: Tactrix OpenPort 2.0
Status: Reachable
Firmware: v1.4.2
Protocols: CAN, ISO15765
```

**Exit codes**:
- `0`: Provider reachable
- `1`: Invalid arguments
- `2`: Provider not reachable

---

## `canmatik demo`

**Purpose**: Run with the mock backend. Generates simulated CAN traffic so all
features can be exercised without hardware.

**Usage**:
```
canmatik demo [--bitrate <bps>] [--filter <spec>]... [--trace <file>]
             [--frame-rate <fps>] [--json] [--verbose]
```

**Options**:

| Flag | Type | Default | Description |
|------|------|---------|-------------|
| `--trace <file>` | string | none | Replay this log file as simulated input |
| `--frame-rate <fps>` | uint32 | `100` | Frames per second for generated traffic |

**Behavior**: Identical to `monitor` but uses `MockProvider`. Session status
shows `[MOCK]` indicator.

**Output (text)**:
```
[PASSIVE] [MOCK] Demo session @ 500 kbps (100 fps)
   +0.000000  100  Std  [8]  DE AD BE EF CA FE 00 01
   +0.010000  200  Std  [4]  01 02 03 04
   ...
```

**Exit codes**: Same as `monitor`.

---

## Filter Syntax

Filters are specified with the `--filter` flag. Multiple `--filter` flags can be
combined.

| Syntax | Meaning | Example |
|--------|---------|---------|
| `0x7E8` | Pass only ID 0x7E8 | `--filter 0x7E8` |
| `0x7E0-0x7EF` | Pass IDs in range | `--filter 0x7E0-0x7EF` |
| `0x700/0xFF0` | Pass IDs matching mask | `--filter 0x700/0xFF0` |
| `!0x000` | Block specific ID | `--filter !0x000` |
| `pass:0x7E8` | Explicit pass (same as bare ID) | `--filter pass:0x7E8` |
| `block:0x000` | Explicit block | `--filter block:0x000` |

**Combination rules**:
- If any `pass` filter is present: only matching frames are shown (whitelist mode)
- `block` filters always exclude, regardless of pass filters
- No filters = show all frames (default)

---

## Output Format Contract

### Text Mode (default)

Frame lines follow this format:
```
   +<seconds.microseconds>  <ID>  <Std|Ext>  [<DLC>]  <hex payload bytes>
```

Fields are separated by two or more spaces. Timestamps are session-relative with
microsecond resolution. IDs are hex without prefix (3 characters for 11-bit, 8
for 29-bit). Payload bytes are space-separated hex pairs.

### JSON Mode (`--json`)

Each line is a complete JSON object (JSONL format):
```json
{"ts":<float>,"ats":<int>,"id":"<hex>","ext":<bool>,"dlc":<int>,"data":"<hex>"}
```

| Field | Type | Description |
|-------|------|-------------|
| `ts` | float | Session-relative timestamp in seconds |
| `ats` | int | Adapter timestamp in microseconds |
| `id` | string | Arbitration ID in hex |
| `ext` | bool | Whether ID is 29-bit extended |
| `dlc` | int | Data Length Code |
| `data` | string | Payload as hex string (space-separated pairs) |
