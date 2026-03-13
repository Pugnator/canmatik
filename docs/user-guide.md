# CANmatik User Guide

## Overview

CANmatik is a J2534 CAN bus scanner, logger, and OBD-II diagnostic tool for
Windows. It ships as a single `canmatik.exe` — double-click to open the GUI,
or pass command-line arguments for scripted/headless usage.

---

## Installation

### Installer (recommended)

Run `canmatik-0.1.0-setup.exe`. The installer will:

1. Copy `canmatik.exe` and `fake_j2534.dll` to `C:\Program Files\CANmatik`
2. Create a Start Menu shortcut
3. Optionally create a Desktop shortcut
4. Add an uninstaller entry to Windows Add/Remove Programs

### Portable

Copy `canmatik.exe` and `fake_j2534.dll` to a folder of your choice and run
directly. Settings are saved to `canmatik_gui.json` next to the executable.

---

## Getting Started

### 1. Launch the application

Double-click `canmatik.exe`. The GUI opens with the **Bus Messages** tab active.

<!-- Screenshot: docs/interface.jpg -->
![Main interface](interface.jpg)

### 2. Select your adapter

Switch to the **Settings** tab. Under **Connection**:

- Click **Scan** to discover installed J2534 providers
- Select your adapter from the **J2534 Interface** dropdown
- Choose the **Bus Protocol** (CAN for most vehicles)
- Set the **Bitrate** (500 kbps is standard for OBD-II)

### 3. Connect

Click the **Connect** button in the toolbar at the top. The status indicator
changes to "Connected" and CAN frames begin appearing in the Bus Messages tab.

### 4. Explore

- **Bus Messages** — watch live CAN traffic
- **OBD Data** — query and stream engine parameters
- **DTC** — read and clear diagnostic trouble codes

---

## GUI Reference

### Toolbar

The toolbar is always visible above the tab bar:

| Control | Description |
|---------|-------------|
| **Connect / Disconnect** | Open or close the J2534 channel |
| **Live / File** | Switch between live capture and file replay |
| **Save Buffer** | Save the current frame buffer to ASC or JSONL |
| **Open File** | Load a previously recorded log (File mode) |
| **Playback controls** | Play, Pause, Stop, Rewind, Loop, Speed (File mode) |

### Bus Messages Tab

Displays CAN frames in a table with real-time diff highlighting.

<!-- Screenshot: docs/can_messages.png -->
![CAN Messages panel](can_messages.png)

**Columns:**

| Column | Description |
|--------|-------------|
| **ID** | Arbitration ID in hex (e.g., `0x7E0`) |
| **DLC** | Data Length Code |
| **Data** | Payload bytes, color-coded per byte |
| **Time** | Session-relative timestamp (seconds) |
| **Count** | Number of times this ID has been seen |
| **Watch** | `*` indicator for watchdog entries |

**Color coding:**

- **Green** — new ID (first appearance)
- **Red** — changed byte (different from previous frame)
- **Yellow** — DLC changed
- **Cyan** — watched ID (in the watchdog list)

**Controls:**

- **Raw Stream** — toggle between grouped-by-ID view and chronological raw
  frame stream. Raw mode shows every frame in arrival order with a sequential
  frame number instead of a count.
- **Changed only** — show only IDs whose data has changed recently
- **Last N** — how many recent transmissions to compare for change detection
- **Display Filter** — OBD+Broadcast / OBD Only / Broadcast Only

**Interactions:**

- **Click** a row to select it (enables the byte graph below the table)
- **Double-click** a row to copy its data to the clipboard
  (format: `0x7E0  8  02 01 00 00 00 00 00 00`)
- **Right-click** a row for the context menu:
  - Add/Remove Watchdog
  - Exclude ID / Include Only ID

**Byte graph:** When a row is selected, a line graph below the table shows
the value of a specific byte over time. Use the **Byte** input to select which
byte index to plot (0–7).

### OBD Data Tab

Displays decoded OBD-II PID values in real time.

<!-- Screenshot: docs/obd.png -->
![OBD-II diagnostics](obd.png)

| Column | Description |
|--------|-------------|
| **PID** | Parameter ID in hex |
| **Name** | Human-readable parameter name |
| **Value** | Decoded value with unit (e.g., "3500 RPM") |
| **Raw** | Raw hex bytes from the ECU response |

**Buttons:**

- **Query Supported** — ask the ECU which PIDs it supports (Mode $01 PID $00)
- **Start / Stop Stream** — begin or end continuous PID polling

### DTC Tab

Read and clear Diagnostic Trouble Codes.

- **Read DTCs** — retrieve stored (Mode $03) and pending (Mode $07) codes
- **Clear DTCs** — erase stored DTCs (requires confirmation)

Each DTC is displayed with its code (e.g., `P0300`) and description if known.

### Logs Tab

Shows CANmatik's internal diagnostic log messages (info, warnings, errors).
Useful for troubleshooting connection or adapter issues.

### Settings Tab

#### Appearance

- **Color scheme** — Dark (default), Light, or Retro (green-on-black)
- **Font scales** — separate sliders for Bus Messages and OBD Data panels

#### Font Colors

Customize the colors for new IDs, changed bytes, DLC changes, watched IDs,
and default text. Click any color swatch to open a color picker. Use **Reset
Colors to Defaults** to restore the originals.

#### Connection

- **J2534 Interface** — select from discovered providers, or click **Scan**
- **Bus Protocol** — CAN (default), J1850 VPW, J1850 PWM
- **Bitrate** — 125 / 250 / 500 / 1000 kbps
- **Mock mode** — use the simulated backend (no hardware needed)

#### Proxy Mode

Register CANmatik as a J2534 provider so that other diagnostic tools
(ROM editors, ECU flashers) route their J2534 API calls through CANmatik.

- **Terminated mode** — accept J2534 calls without a real adapter. Useful to
  see what a third-party tool is asking for without any hardware connected.
- **Target adapter** — select which real adapter to forward calls to
  (hidden in terminated mode).
- **Enable proxy** — start/stop the named-pipe proxy server.

<!-- Screenshot: docs/proxy_terminated.png -->
![Proxy terminated mode](proxy_terminated.png)

#### Proxy DLL Registration

Install or remove the fake J2534 DLL in the Windows Registry:

- Choose a **preset** profile (e.g., "Tactrix OpenPort 2.0") or enter a custom
  interface name
- Click **Install Proxy Interface** (may require running as Administrator)
- Use the **Installed J2534 Interfaces** section to view or remove registered
  providers

#### Capture Buffer

- **Buffer capacity** — how many frames to keep in memory (default: 100,000)
- **Overwrite when full** — if enabled, oldest frames are discarded (ring
  buffer). If disabled, capture stops when the buffer reaches capacity.

#### Watchdog

- **History size** — number of decoded value samples to keep per watched ID

#### OBD Settings

- **Query interval** — polling rate for PID streaming (50–5000 ms)
- **Selected PIDs** — choose which PIDs to query. Pick from the dropdown of
  common PIDs or enter a hex PID manually.
- **Query ECU Supported PIDs** — auto-detect supported PIDs from the live ECU

---

## File Formats

CANmatik supports two log formats:

### Vector ASC (`.asc`)

Industry-standard ASCII log. Compatible with Vector CANalyzer, CANoe, and most
CAN analysis tools.

### JSON Lines (`.jsonl`)

One JSON object per line. Ideal for scripting, filtering with `jq`, and
programmatic analysis. See [log-format-spec.md](log-format-spec.md) for the
full schema.

---

## CLI Reference

When `canmatik.exe` is invoked with arguments, it runs in console mode.

### Global Options

| Option | Description |
|--------|-------------|
| `--config <file>` | Path to configuration file (default: `canmatik.json`) |
| `--provider <name>` | J2534 provider name |
| `--mock` | Use mock backend instead of real hardware |
| `--json` | Output in JSON Lines format |
| `--verbose` | Enable verbose diagnostic output |
| `--debug` | Enable debug-level logging |
| `--version` | Print version and exit |

### Commands

#### `scan`

Discover and list installed J2534 providers.

```powershell
canmatik.exe scan
canmatik.exe scan --json
```

#### `monitor`

Connect to an adapter and display CAN frames in real time.

```powershell
canmatik.exe monitor --provider "Tactrix OpenPort 2.0" --bitrate 500000
canmatik.exe monitor --mock --filter 0x7E8
```

#### `record`

Monitor and record traffic to a log file.

```powershell
canmatik.exe record --provider "Tactrix OpenPort 2.0" -o session.asc
canmatik.exe record --mock -o session.jsonl -f jsonl
```

#### `replay`

Open and inspect a previously captured log.

```powershell
canmatik.exe replay session.asc
canmatik.exe replay session.asc --summary
canmatik.exe replay session.asc --search 0x7E8
```

#### `obd`

OBD-II diagnostics with subcommands:

```powershell
# Query supported PIDs
canmatik.exe obd query --supported --provider "Tactrix OpenPort 2.0"

# Stream live engine data
canmatik.exe obd stream --provider "Tactrix OpenPort 2.0"
canmatik.exe obd stream --obd-config engine_only.yaml --interval 200

# Read DTCs
canmatik.exe obd dtc --provider "Tactrix OpenPort 2.0"

# Clear DTCs (requires --force)
canmatik.exe obd dtc --clear --force --provider "Tactrix OpenPort 2.0"

# Vehicle info (VIN, Cal ID, ECU name)
canmatik.exe obd info --provider "Tactrix OpenPort 2.0"
```

#### `demo`

Run with simulated traffic (no hardware needed).

```powershell
canmatik.exe demo
canmatik.exe demo --trace captures/session.asc
```

#### `label`

Manage human-readable labels for arbitration IDs.

```powershell
canmatik.exe label set 0x7E8 "ECU Response"
canmatik.exe label list
canmatik.exe label remove 0x7E8
```

#### `sniff`

Show only changed CAN frames with diff highlighting (console mode).

```powershell
canmatik.exe sniff --mock
canmatik.exe sniff --provider "Tactrix OpenPort 2.0" --filter 0x7E0-0x7EF
```

---

## Troubleshooting

### No adapters found

- Ensure the adapter's driver is installed and the device is plugged in
- Run `canmatik.exe scan --verbose` to see registry lookup details
- Check that the J2534 DLL is registered under
  `HKLM\SOFTWARE\PassThruSupport.04.04`
- See [adapter-compatibility.md](adapter-compatibility.md)

### Connection fails

- Verify another application isn't already using the adapter
- Try a different bitrate (some vehicles use 250 kbps instead of 500 kbps)
- Check the **Logs** tab for error messages

### Proxy DLL installation fails

- The DLL must be copied to `C:\Windows\SysWOW64` — this requires
  Administrator privileges
- Right-click `canmatik.exe` → **Run as administrator**, then install the
  proxy interface from Settings

### No OBD responses

- Confirm the vehicle ignition is ON (engine running or key in position II)
- Ensure the adapter supports CAN protocol at 500 kbps
- Some vehicles respond only to physical addressing (0x7E0) — try the OBD
  settings in the Settings tab

### Log file not created (proxy DLL)

- The fake J2534 DLL writes its log to `%TEMP%\<dllname>.log`
  (e.g., `%TEMP%\op20pt32.log`) when run from a protected directory
- Check: `Get-Content "$env:TEMP\op20pt32.log" -Tail 50`
