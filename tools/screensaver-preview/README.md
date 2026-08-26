# Screensaver Preview

Local browser preview tool for iterating on CrossPoint screensaver layouts before flashing firmware.

## What it helps with

- Preview X4 Pro sized screens at `480 x 800`
- Compare multiple layout variants quickly
- Try calendar and life-grid concepts
- Export PNG snapshots for reference

## Start it

From the repository root:

```bash
python3 -m http.server 8123
```

Then open:

```text
http://localhost:8123/tools/screensaver-preview/
```

## Current preview modes

- `Calendar`
  - `Firmware Match`
  - `Quiet Card`
  - `Poster Date`
- `Life Grid`
  - uses the same visual variants

## Notes

- This is for visual iteration only.
- Real e-ink behavior still needs testing on-device after a firmware flash.
- The preview is intentionally lightweight and dependency-free so it is easy to tweak.
