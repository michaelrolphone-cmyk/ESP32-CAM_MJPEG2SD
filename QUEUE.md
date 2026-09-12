# Vision work queue

Post-process never runs during an open AVI. Capture always wins.

## On card

```
/review/state.json              worker: idle | running | paused_for_capture
/review/jobs/<clipId>.json      one job per clip (queued/running/aborted/done/failed)
/YYYYMMDD/<clipId>.review.json  analysis result (capture sidecar is not overwritten)
```

Job JSON:

```json
{
  "id": "20260912_143012",
  "kind": "clip_analyze",
  "avi": "/20260912/20260912_143012_VGA_10_16.avi",
  "still": "/20260912/20260912_143012.jpg",
  "sidecar": "/20260912/20260912_143012.json",
  "status": "queued",
  "step": 0,
  "attempts": 0,
  "still_bytes": 0,
  "avi_bytes": 0,
  "class": "unknown",
  "updated": "2026-09-12T14:30:20"
}
```

## Flow

1. Clip closes → sidecar + still → `trafficEnqueueJob()`.
2. `visionQ` task picks oldest `queued` / `aborted` / `running` job while `!isCapturing`.
3. Steps (each yields 20 ms and re-checks capture):
   - 0 stat still
   - 1 stat AVI
   - 2 placeholder class (`unknown` / `empty` / `reject`)
   - 3 write `.review.json`
4. If `isCapturing` goes true mid-job: status → `aborted`, `state.json` → `paused_for_capture`, worker sleeps.
5. When idle again the same job **resumes at `step`**. After 8 attempts it **restarts at step 0**.

## What is not in this worker

No ESP-WHO / FOMO / face embed yet. Replace the body of step 2 in `src/trafficQueue.cpp` when a model is ready. Do not call it from `motionDetect.cpp`.
