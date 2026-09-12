# Implementation — 10.9.5-traffic.1

## Runtime

`setup()` → `trafficSetup()`

1. First-boot capture profile (VGA / 10 fps / event-only).
2. Sidecar watcher: new AVI → `.jpg` + `.json` + CSV row.
3. Vision queue (`src/trafficQueue.cpp`): enqueue that clip, analyze only while idle, abort when capture starts, persist JSON on the card, resume later.

See [QUEUE.md](QUEUE.md).

## Files

| Path | Role |
|---|---|
| `src/trafficSidecar.cpp` | profile, sidecar, enqueue, abort-on-capture |
| `src/trafficQueue.cpp` | `/review` JSON jobs + worker |
| `ESP32-CAM_MJPEG2SD.ino` | `trafficSetup()` |
| `SPEC.md` | product rules |

## Flash

Arduino: ESP32S3 Dev Module, OPI PSRAM, 16 MB, UART Type-C, arduino-esp32 >= 3.1.1. FAT32 U1 card in before power.

Walk test: AVI + sidecar + `/review/jobs/<id>.json`. Start another walk while a job shows `running` — job must become `aborted` then `done` after the new clip closes.
