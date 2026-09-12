# Foot Traffic Monitor — Product Spec

**Project:** Event-first wildlife / foot-traffic recorder  
**Repo:** fork of [s60sc/ESP32-CAM_MJPEG2SD](https://github.com/s60sc/ESP32-CAM_MJPEG2SD) @ v10.9.5  
**Owner:** michaelrolphone-cmyk  
**Status:** spec locked 2026-09-12 — capture firmware first, classification later  
**Hardware arriving:** 3× ESP32-S3-CAM Dev Board, OV3660, WROOM-1 **N16R8**, dual Type-C

This document is the source of truth for this fork. Do not add real-time neural detection to the capture path.

---

## 1. Problem

Count people (and animals) over time at a doorway / path. Estimate repeat visits when a face is usable. Keep photo + short video of each visit on a 16 GB U1 microSD card.

Capture must not wait on AI. The board records when something moves in frame. Identity, species, and unique-visitor math run **after the clip is closed**, either:

- on the same ESP32-S3 during idle, or
- on a PC that reads the SD card / WebDAV / FTP pull.

## 2. Non-goals

- Continuous dashcam or 24/7 AVI.
- On-the-fly YOLO / face ReID while frames are being written.
- HD / QXGA video.
- Cloud subscription or required always-on Wi-Fi for capture.
- Airport-grade re-identification from backs / hats / distance.

## 3. Hardware (this kit)

| Item | Value |
|---|---|
| Module | ESP32-S3-WROOM-1-N16R8 (16 MB flash, 8 MB OPI PSRAM) |
| Sensor | OV3660, DVP 8-bit |
| USB | Dual Type-C: UART (CH340) for flash/log, native USB-OTG unused in v1 |
| SD | On-board slot, **1-bit SDMMC**, U1 16 GB FAT32 |
| Units | 3 identical boards |

### 3.1 Camera pins (Freenove / S3-EYE family)

| Signal | GPIO |
|---|---|
| XCLK | 15 |
| PCLK | 13 |
| VSYNC | 6 |
| HREF | 7 |
| SIOD (SDA) | 4 |
| SIOC (SCL) | 5 |
| D0..D7 | 11, 9, 8, 10, 12, 18, 17, 16 |
| PWDN / RESET | unused (-1) |

### 3.2 SD pins (Keyestudio MB0184 / GOOUUU-style dual Type-C CAM)

| SDMMC | GPIO |
|---|---|
| CMD | 38 |
| CLK | 39 |
| DATA0 | 40 |

If `CAMERA_MODEL_ESP32_S3_CAM` already maps SD this way, keep it. If a board comes up with `Camera init error` or `Check SD card inserted`, try `CAMERA_MODEL_FREENOVE_ESP32S3_CAM` next — do not invent a third pin map until both have been tested on the physical PCB.

Users of the same AliExpress dual-C OV3660 kit report **`CAMERA_MODEL_ESP32_S3_CAM`** as the working MJPEG2SD define.

### 3.3 Arduino / IDF board settings

- Board: **ESP32S3 Dev Module**
- PSRAM: **OPI PSRAM**
- Flash: **16 MB**
- Partition: **16MB (3MB APP / 9.9MB FATFS)** or equivalent
- Upload: UART Type-C, 921600
- USB CDC on boot: as required by the CH340 port (usually off for UART upload)

## 4. Capture policy (v1 — what the S3 does live)

Upstream MJPEG2SD already does motion-triggered AVI. This fork **keeps that path** and tightens defaults for a 16 GB U1 card.

### 4.1 Trigger

Record **only when the frame-difference motion detector fires** (people *or* animals — cheap grayscale delta, not a classifier).

- Sensitivity: start mid; calibrate with **Show Motion** on the web UI.
- Optional later: PIR / radar on a free GPIO in parallel (`INCLUDE_PERIPH`). Not required for v1.
- No dashcam slider. No time-lapse unless explicitly enabled for a quiet site.

### 4.2 Clip shape

| Parameter | v1 default | Why |
|---|---|---|
| Resolution | **VGA 640×480** | Fits 1-bit SD + U1; faces still usable for later ReID |
| JPEG quality | **12** (range 10–16) | ~30–50 KB/frame |
| Record FPS | **10** | Smooth enough for gait; well under U1 write |
| Pre-roll | use existing min-frames / buffer if present | Do not miss the walk-in |
| Stop | **3 s** with no motion | One visit = one file |
| Min frames | **15** (~1.5 s) | Delete leaf-flicker junk |
| Max clip | **30 s** | Stop a stuck trigger from eating the card |
| Format | AVI Motion-JPEG | Plays in VLC; no H.264 on this SoC |

Still JPEG: save **one** best frame per clip (largest motion blob or first frame after trigger) as a sibling `.jpg`. Cheaper to scan later than opening every AVI.

### 4.3 Why this is not a bandwidth problem

Wi-Fi is not on the write path. Bottleneck is 1-bit SDMMC + FAT32 + U1.

Budget at VGA / Q12 / 10 fps ≈ **400 KB/s** while recording.

U1 = 10 MB/s *rated sequential*. Sustained small-block FAT writes are slower, but 0.4 MB/s is comfortable if:

- card is genuine, FAT32, formatted in-camera or SD Association formatter
- AVI writer uses large aligned buffers (upstream already does this)
- motion analysis **drops to 1-in-10 after trigger** (upstream already does this)

Do **not** run a neural net, WebDAV push, or HD stream during an open AVI.

## 5. Storage on a 16 GB U1 card

Usable FAT32 ≈ 14.5 GB after format + `/data` UI files.

| Content | Size |
|---|---|
| 15 s VGA clip | ~6 MB |
| sibling still | ~0.15 MB |
| 200 visits/day | ~1.2 GB/day |
| Card full | ~12 days at that rate |

Policy when free space **< 400 MB**:

1. Delete oldest **day folder** that has been post-processed (`status=done` in the log).
2. Never delete `/data` (web UI).
3. If nothing is marked done, delete oldest day anyway and log `purge_unprocessed`.

Keep `traffic_log.csv` at card root; it is tiny.

## 6. On-card layout

```
/
  data/                          MJPEG2SD web assets (do not touch)
  20260912/
    20260912_143012_VGA_10_16.avi
    20260912_143012.jpg
    20260912_143012.json         written at clip close (see §7)
  traffic_log.csv
  review/
    gallery.json                 written by post-process, not by capture
```

Upstream filename pattern `YYYYMMDD_HHMMSS_VGA_10_16.avi` stays. Do not rename AVI files in v1; parsers key off that string.

## 7. Sidecar written at clip close (capture task)

No neural net. Just facts the recorder already knows.

```json
{
  "id": "20260912_143012",
  "board_id": "cam-1",
  "avi": "20260912/20260912_143012_VGA_10_16.avi",
  "still": "20260912/20260912_143012.jpg",
  "started": "2026-09-12T14:30:12-06:00",
  "duration_s": 16,
  "frames": 160,
  "fps": 10,
  "size": "VGA",
  "motion_peak": 0.42,
  "status": "captured"
}
```

Append one CSV row the same moment:

```
ts,board_id,clip_id,duration_s,frames,motion_peak,status
2026-09-12T14:30:12-06:00,cam-1,20260912_143012,16,160,0.42,captured
```

`status` later becomes `reviewed` / `person` / `animal` / `reject` / `done`.

## 8. Post-process (v2 — after the file is closed)

Runs only when **no clip is open**. May run on-device at 1 clip / few seconds, or on a laptop.

### 8.1 Jobs, in order

1. Open still first. If still is empty / too dark, sample 1 frame / 0.5 s from the AVI.
2. Classify: `empty | person | animal | vehicle | unknown`.
3. If person and face large enough: embed and match gallery (`known:<name>` or `anon:<hash8>`).
4. If animal: coarse class if cheap (`dog | cat | bird | other`), else `animal`.
5. Count logic:
   - **crossings:** +1 per clip that classifies as person or animal (one clip = one visit unless §8.2 merge).
   - **unique people:** distinct `known:*` + distinct `anon:*` that do not merge.
6. Write results back into the sidecar and `review/gallery.json`.
7. Mark `status=done` so purge can delete the day later.

On-device models (when added): ESP-WHO / ESP-DL pedestrian + face, or a tiny Edge Impulse FOMO. **Never** call them from `motionDetect.cpp` / the AVI writer.

Off-device is acceptable and likely more accurate. The card is the API.

### 8.2 Repeat-visitor heuristic

| Signal | Use |
|---|---|
| Face embedding match above threshold | same `known` or same `anon` |
| No face, same board, gap **< 120 s** | merge into previous visit (same person lingering) |
| No face, gap ≥ 120 s | new `anon` |
| Animal clips | never merge with person IDs |

Document the lie: unique counts are **faces we could embed**, not ground truth of every body that passed.

## 9. Time and identity of the board

- NTP when Wi-Fi is up. Timezone: America/Denver (`MST7MDT,M3.2.0,M11.1.0`) unless a site is set otherwise.
- If no network at boot, use last RTC offset; filenames may drift. Fix clocks before trusting unique-across-days stats.
- `board_id`: NVS string `cam-1` / `cam-2` / `cam-3`.

## 10. Network (optional, capture does not depend on it)

Keep upstream AP-on-first-boot and web UI.

Allowed while **idle**:

- browse files, download AVI, WebDAV/FTP pull
- live MJPEG preview at QVGA for aiming the camera

Forbidden while **recording**:

- extra stream clients
- OTA
- post-process inference
- NAS upload of the open file

## 11. Three-board deployment

| Unit | Role | Notes |
|---|---|---|
| cam-1 | primary doorway, face-on if possible | best unique-person numbers |
| cam-2 | side / path count | crossings only |
| cam-3 | spare or second entrance | same firmware |

Same firmware image. Only `board_id` and Wi-Fi NVS differ.

## 12. Build / flash checklist (day the boards land)

1. Format 16 GB card FAT32, insert before power.
2. In `ESP32-CAM_MJPEG2SD.h` uncomment **only** `#define CAMERA_MODEL_ESP32_S3_CAM` under the S3 block.
3. Arduino-ESP32 ≥ 3.1.1, OPI PSRAM, 16 MB partition.
4. Flash over the **UART** Type-C port.
5. Join `ESP-CAM_MJPEG_...` AP → set router + timezone → Save → reboot.
6. Confirm `/data` downloaded, live stream works, **Show Motion** outlines walking.
7. Walk in front of the camera. Confirm a new `YYYYMMDD_*.avi` plus `.jpg` / `.json`.
8. Open AVI in VLC from the card or the Download button.

If camera init fails: swap to `CAMERA_MODEL_FREENOVE_ESP32S3_CAM` and retry once.

## 13. Phased delivery

| Phase | Deliverable | Done when |
|---|---|---|
| **P0** | This spec + fork | you are reading it |
| **P1** | Stock MJPEG2SD on this pinout, VGA motion AVI + still | VLC plays a walk-by clip from the U1 card |
| **P2** | Sidecar JSON + `traffic_log.csv` at clip close | log row exists for every kept AVI |
| **P3** | Idle / offline classifier (person vs animal vs reject) | CSV `status` not stuck on `captured` |
| **P4** | Face gallery + 120 s merge | unique-person column in a daily summary |

P1 is the only phase that must work the week the hardware arrives.

## 14. Risks

| Risk | Mitigation |
|---|---|
| Wrong `CAMERA_MODEL_*` | two known defines only; test both |
| Counterfeit / slow SD | one branded U1; format FAT32; abort record if write stalls |
| Wind / shadows | raise min-frames; tune sensitivity; P3 rejects empty clips |
| Night / IR-less OV3660 | expect missed events after dusk unless a lamp is added later |
| Unique-ID overclaim | report crossings and face-IDs as separate metrics |
| 16 GB fills | auto-purge processed days at 400 MB free |

## 15. License / attribution

Upstream code remains under the license in this repository (s60sc / ESP32-CAM_MJPEG2SD). This spec is project documentation for the fork.
