# Log Format Specification

CANmatik supports two log formats: **Vector ASC** (text) and **JSONL**
(machine-readable). Both are designed for deterministic, lossless
recording and bit-identical replay (SC-006).

---

## Vector ASC Format (`.asc`)

Based on the Vector CANalyzer/CANoe ASCII logging format. Human-readable,
widely supported by third-party CAN tools.

### Structure

```
; CANmatik ASC v1.0
date <Day Mon DD HH:MM:SS.mmm YYYY>
base hex  timestamps absolute
; adapter: <adapter name or provider>
; bitrate: <bitrate in bps>
internal events logged
Begin Triggerblock <Day Mon DD HH:MM:SS.mmm YYYY>
   <frame lines...>
End TriggerBlock
```

### Header

| Line | Description |
|------|-------------|
| `; CANmatik ASC v1.0` | Tool identification comment |
| `date <timestamp>` | UTC date/time when recording started |
| `base hex  timestamps absolute` | Numeric base and timestamp mode |
| `; adapter: <name>` | Adapter/provider metadata (comment) |
| `; bitrate: <bps>` | Bitrate metadata (comment) |
| `internal events logged` | Standard ASC header line |
| `Begin Triggerblock <ts>` | Start of recorded data section |

### Frame Lines

```
   <timestamp> <channel> <id>            Rx   d <dlc> <hex data bytes>
```

| Field | Format | Example | Description |
|-------|--------|---------|-------------|
| `timestamp` | `%12.6f` | `   0.001234` | Seconds since session start |
| `channel` | integer | `1` | Channel number (1-based) |
| `id` | `%03X` or `%08Xx` | `7E8` / `1ABCDEF0x` | Arb ID; `x` suffix = extended (29-bit) |
| direction | `Rx` | `Rx` | Always `Rx` in passive mode |
| type | `d` | `d` | Data frame |
| `dlc` | integer | `8` | Data length code (0–8, or 0–64 for FD) |
| data | `%02X`... | `06 41 00 FF` | Space-separated hex payload bytes |

### Footer

```
End TriggerBlock
```

### Example

```
; CANmatik ASC v1.0
date Mon Mar 09 12:00:00.000 2026
base hex  timestamps absolute
; adapter: MockProvider
; bitrate: 500000
internal events logged
Begin Triggerblock Mon Mar 09 12:00:00.000 2026
   0.000000 1  7E0             Rx   d 2 02 01
   0.010000 1  7E8             Rx   d 3 06 41 00
End TriggerBlock
```

---

## JSON Lines Format (`.jsonl`)

One JSON object per line. Machine-parseable, grep-friendly, supports
streaming append.

### Structure

```
{"_meta":true, "format":"canmatik-jsonl", ...}    ← header
{"ts":..., "id":"...", ...}                        ← frame (repeated)
{"_meta":true, "type":"session_summary", ...}      ← footer
```

### Header (Metadata Line)

The first line is a metadata object identified by `"_meta": true` with
**no** `"type"` field:

```json
{
  "_meta": true,
  "format": "canmatik-jsonl",
  "version": "1.0",
  "tool": "canmatik 0.1.0",
  "adapter": "<adapter name>",
  "bitrate": 500000,
  "created_utc": "2026-03-09T12:00:00Z"
}
```

### Frame Lines

Each subsequent line (excluding metadata) is a frame object:

```json
{
  "ts": 0.001234,
  "ats": 1234,
  "id": "7E8",
  "ext": false,
  "dlc": 3,
  "data": "06 41 00"
}
```

| Field | Type | Description |
|-------|------|-------------|
| `ts` | float | Seconds since session start (host-relative) |
| `ats` | integer | Adapter timestamp in microseconds |
| `id` | string | Arbitration ID — `"7E8"` (3 hex, 11-bit) or `"1ABCDEF0"` (8 hex, 29-bit) |
| `ext` | boolean | `true` for extended (29-bit) ID frames |
| `dlc` | integer | Data length code |
| `data` | string | Space-separated uppercase hex payload bytes |

### Footer (Session Summary)

The last line is a metadata object with `"type": "session_summary"`:

```json
{
  "_meta": true,
  "type": "session_summary",
  "frames": 12345,
  "errors": 0,
  "dropped": 0,
  "duration": 60.5
}
```

### Distinguishing Metadata from Frames

When reading JSONL:
1. If the object has `"_meta": true` and **no** `"type"` field → header
2. If the object has `"_meta": true` and **has** a `"type"` field → summary/metadata (skip)
3. Otherwise → frame data

### Example

```jsonl
{"_meta":true,"format":"canmatik-jsonl","version":"1.0","tool":"canmatik 0.1.0","adapter":"MockProvider","bitrate":500000,"created_utc":"2026-03-09T12:00:00Z"}
{"ts":0.0,"ats":0,"id":"7E0","ext":false,"dlc":2,"data":"02 01"}
{"ts":0.01,"ats":10000,"id":"7E8","ext":false,"dlc":3,"data":"06 41 00"}
{"_meta":true,"type":"session_summary","frames":2,"errors":0,"dropped":0,"duration":0.01}
```

---

## Format Selection

| Criterion | ASC | JSONL |
|-----------|-----|-------|
| Human-readable | ✅ | ✅ (JSON) |
| Third-party tool support | ✅ CANalyzer, SavvyCAN | ❌ Custom |
| Machine-parseable | ⚠️ Regex | ✅ JSON parser |
| Streaming append | ⚠️ Need to update footer | ✅ Append lines |
| File size | Smaller | ~20% larger |

Default recording format: **ASC** (`--format asc`).
Override with `--format jsonl`.
