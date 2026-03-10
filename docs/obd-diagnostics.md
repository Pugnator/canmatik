# OBD-II Diagnostics

CANmatik supports OBD-II diagnostic queries over ISO 15765-4 (CAN) using
J2534 pass-thru adapters.

## CLI Usage

### Query Supported PIDs

```
canmatik obd query --supported [--json]
```

Lists PIDs supported by the connected ECU(s).

### Query a Single PID

```
canmatik obd query --pid 0x0C [--json]
```

Queries and decodes a single PID value.

### Stream Live Data

```
canmatik obd stream --config obd_default.yaml [--json]
```

Continuously polls PID groups at configured intervals. Press Ctrl+C to stop.

### Read / Clear DTCs

```
canmatik obd dtc [--json]
canmatik obd dtc --clear --force
```

Reads stored and pending DTCs. Clearing requires `--force` flag (Mode $04 is
destructive and resets readiness monitors).

### Vehicle Info

```
canmatik obd info [--json]
```

Reads VIN, Calibration ID, and ECU name via Mode $09.

## YAML Configuration Schema

OBD streaming uses a YAML config file. Generate a default:

```
canmatik obd stream --generate-config obd.yaml
```

### Schema

```yaml
# Default polling interval (all groups inherit this unless overridden)
interval: "500ms"      # Supports: "500ms", "1s", "2hz"

# CAN addressing
addressing:
  mode: functional     # "functional" (0x7DF broadcast) or "physical" (0x7E0)
  tx_id: 0x7DF         # Request CAN ID
  rx_base: 0x7E8       # Response base CAN ID

# PID groups — each group is polled at its own interval
groups:
  - name: engine_core
    interval: "200ms"  # Override group interval
    pids:
      - id: 0x0C       # Engine RPM
      - id: 0x0D       # Vehicle Speed
      - id: 0x05       # Coolant Temperature

  - name: fuel_system
    interval: "2s"
    pids:
      - id: 0x06       # Short Term Fuel Trim (Bank 1)
      - id: 0x10       # MAF Air Flow Rate

# Standalone PIDs (use default interval, no group name)
standalone_pids:
  - 0x42               # Control Module Voltage
```

### Interval Format

| Format | Example  | Meaning            |
|--------|----------|--------------------|
| `Nms`  | `500ms`  | N milliseconds     |
| `Ns`   | `2s`     | N seconds          |
| `Nhz`  | `10hz`   | N queries/second   |

Valid range: 10ms – 60000ms (60 seconds).

## Supported PIDs (Mode $01)

| PID   | Name                          | Unit   | Formula                |
|-------|-------------------------------|--------|------------------------|
| 0x04  | Calculated Engine Load        | %      | A × 100 / 255         |
| 0x05  | Engine Coolant Temperature    | °C     | A − 40                |
| 0x06  | Short Term Fuel Trim (Bank 1) | %      | (100A − 12800) / 128  |
| 0x07  | Long Term Fuel Trim (Bank 1)  | %      | (100A − 12800) / 128  |
| 0x0B  | Intake Manifold Pressure      | kPa    | A                      |
| 0x0C  | Engine RPM                    | rpm    | (256A + B) / 4        |
| 0x0D  | Vehicle Speed                 | km/h   | A                      |
| 0x0E  | Timing Advance                | °      | A / 2 − 64            |
| 0x0F  | Intake Air Temperature        | °C     | A − 40                |
| 0x10  | MAF Air Flow Rate             | g/s    | (256A + B) / 100      |
| 0x11  | Throttle Position             | %      | A × 100 / 255         |
| 0x1C  | OBD Standard                  | —      | A (enumerated)         |
| 0x1F  | Run Time Since Start          | s      | 256A + B               |
| 0x2F  | Fuel Tank Level               | %      | A × 100 / 255         |
| 0x31  | Distance Since DTC Cleared    | km     | 256A + B               |
| 0x33  | Barometric Pressure           | kPa    | A                      |
| 0x42  | Control Module Voltage        | V      | (256A + B) / 1000     |
| 0x46  | Ambient Air Temperature       | °C     | A − 40                |
| 0x5C  | Engine Oil Temperature        | °C     | A − 40                |

## ISO 15765-4 Transport

- **Functional addressing**: Request ID 0x7DF (broadcast to all ECUs)
- **Physical addressing**: Request IDs 0x7E0–0x7E7 (specific ECU)
- **Response IDs**: 0x7E8–0x7EF
- **Single frame**: PCI byte 0x0N (N = payload length)
- **Multi-frame**: First Frame (0x1N), Consecutive Frames (0x2N), Flow Control (0x30)
- **Padding**: 0x55 fill to 8-byte DLC
- **Timeout**: 50ms (P2 CAN), 5000ms (P2* extended)
