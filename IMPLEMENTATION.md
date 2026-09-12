# Implementation log — 10.9.5-traffic.1

Capture firmware is MJPEG2SD. New code lives in `src/trafficSidecar.cpp` (already on this branch).

The Arduino sketch compiles every `.cpp` under `src/`. The sidecar file is safe to flash as-is. The calls that *use* it are the remaining hooks below — apply them if they are not already in the tree.

## Already on `master`

- `CAMERA_MODEL_ESP32_S3_CAM` (OV3660 pinout + SD MMC CMD 38 / CLK 39 / D0 40)
- `src/trafficSidecar.cpp` — still JPEG + JSON + `/traffic_log.csv` at clip close
- Fork notes in `ESP32-CAM_MJPEG2SD.h` and `SPEC.md`

## Hooks (P1 / P2)

### `src/appGlobals.h`

```
#define APP_VER "10.9.5-traffic.1"
#define CFG_VER 40
```

Add next to `keepFrame`:

```
void trafficResetClipStats();
void trafficNoteMotion(float ratio);
void writeTrafficSidecar(const char* aviPath, uint16_t frames, uint32_t durationSec,
                         uint8_t recFps, const char* sizeStr);
```

Add globals:

```
extern char boardId[];
extern float clipMotionPeak;
```

### `src/mjpeg2sd.cpp`

- `moveStopSecs = 3`
- `maxFrames = 300`
- `minSeconds = 2`
- in `openAvi()` after zeroing counters: `trafficResetClipStats();`
- in `closeAvi()` after `STORAGE.rename(AVITEMP, aviFileName);`:

```
writeTrafficSidecar(aviFileName, frameCnt, vidDurationSecs, actualFPSint,
                    frameData[fsizePtr].frameSizeStr);
```

### `src/motionDetect.cpp`

After computing `changeCount` / `lightLevel`:

```
{
  int span = max(1, (int)((endPixel - startPixel) / colorDepth));
  trafficNoteMotion((float)changeCount / (float)span);
}
```

### `src/prefs.cpp`

After the `hostName` branch:

```
else if (!strcmp(variable, "boardId")) strncpy(boardId, value, 15);
```

### `src/appSpecific.cpp` default config string

| key | old | new |
|---|---|---|
| fps | 20 | **10** |
| framesize | 10 (VGA) | keep |
| quality | 12 | keep |
| minf | 5 | **2** |
| timezone | GMT0 | **MST7MDT,M3.2.0,M11.1.0** |
| vflip | 0 | **1** (OV3660) |
| maxFrames | 20000 | **300** |
| moveStopSecs | 2 | **3** |
| detectMotionFrames | 5 | **3** |
| sdMinCardFreeSpace | 100 | **400** |
| dashCamOn | 0 | keep |
| boardId | (new) | **cam-1** |

Add line:

```
boardId~cam-1~0~T~Board id for traffic log
```

`CFG_VER 40` forces a fresh `configs.txt` on first boot of this build so the new defaults stick.

## Flash (day the boards arrive)

1. FAT32 the U1 16 GB card, insert it.
2. Arduino IDE: ESP32S3 Dev Module, OPI PSRAM, 16 MB flash, 16MB partition, UART Type-C.
3. Core arduino-esp32 >= 3.1.1.
4. Upload `ESP32-CAM_MJPEG2SD.ino`.
5. Join `ESP-CAM_MJPEG_...` AP, set Wi-Fi, reboot.
6. Walk in front of the camera.
7. Expect:
   - `/YYYYMMDD/YYYYMMDD_HHMMSS_VGA_10_N.avi`
   - `/YYYYMMDD/YYYYMMDD_HHMMSS.jpg`
   - `/YYYYMMDD/YYYYMMDD_HHMMSS.json`
   - `/traffic_log.csv`

If camera init fails, switch the define to `CAMERA_MODEL_FREENOVE_ESP32S3_CAM` and flash once more. Do not invent a third pin map.

## Not in this commit (P3+)

No person/animal classifier. No face gallery. Sidecar `status` stays `captured` until an idle pass updates it.
