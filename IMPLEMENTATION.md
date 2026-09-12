# Implementation — 10.9.5-traffic.1

P1/P2 are wired without editing the large upstream AVI writer.

## What runs

`setup()` calls `trafficSetup()` after `prepRecording()`.

1. **First boot only** (`boardId` not in configs.txt): set VGA (`framesize=10`), 10 fps, quality 12, 3 s stop, 2 s min, max 300 frames, motion on, dashcam off, OV3660 vflip, America/Denver TZ, 400 MB SD reserve, `boardId=cam-1`. Then save. Later UI changes are kept.
2. **Idle watcher** (core 1, 2 s): while `!isCapturing`, scan today's date folder. For each `*.avi` that has no sibling JSON, write:
   - `/YYYYMMDD/YYYYMMDD_HHMMSS.jpg` from `alertBuffer` if still present
   - `/YYYYMMDD/YYYYMMDD_HHMMSS.json`
   - append `/traffic_log.csv`

Motion-triggered AVI itself is still upstream MJPEG2SD. Classification is not on this path (SPEC P3).

## Files

| Path | Role |
|---|---|
| `src/trafficSidecar.cpp` | profile + sidecar + watcher |
| `ESP32-CAM_MJPEG2SD.ino` | `trafficSetup()` |
| `ESP32-CAM_MJPEG2SD.h` | `CAMERA_MODEL_ESP32_S3_CAM`, `trafficSetup()` decl |
| `SPEC.md` | product rules |

## Flash

Arduino: ESP32S3 Dev Module, OPI PSRAM, 16 MB flash, 16MB partition, UART Type-C, arduino-esp32 >= 3.1.1. FAT32 U1 card inserted before power.

Walk test: AVI plays in VLC, sibling `.jpg`/`.json`, CSV row. If camera init fails, switch define to `CAMERA_MODEL_FREENOVE_ESP32S3_CAM` once.

Rename `boardId` to `cam-2` / `cam-3` in the web config for the other two units.
