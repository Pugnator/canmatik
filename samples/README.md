# Samples

## configs/

- **default.json** — Example `canmatik.json` configuration file showing all
  available settings with their default values. Copy to your working directory
  and customize as needed. CLI flags override config file values.

## captures/

Sample CAN bus capture files for testing and demonstration:

- **idle_500kbps.asc** — (created during development) Sample Vector ASC capture
  of an idle CAN bus at 500 kbps.
- **idle_500kbps.jsonl** — (created during development) Same capture in JSON
  Lines format.

These files can be used with:

```powershell
# Replay a sample capture
canmatik replay samples/captures/idle_500kbps.asc

# Use as mock input
canmatik demo --trace samples/captures/idle_500kbps.asc
```
