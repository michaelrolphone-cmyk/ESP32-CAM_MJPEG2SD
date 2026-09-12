/*
  Disk-backed vision work queue.
  Jobs live in /review/jobs/<clipId>.json
  Worker runs only while !isCapturing. Abort persists; resume at step or restart after 8 attempts.
*/
#include "appGlobals.h"

#define REVIEW_DIR "/review"
#define JOBS_DIR   "/review/jobs"
#define STATE_PATH "/review/state.json"
#define MAX_JOBS_SCAN 80
#define STEP_YIELD_MS 20

struct TrafficJob {
  char id[24];
  char avi[IN_FILE_NAME_LEN];
  char still[IN_FILE_NAME_LEN];
  char sidecar[IN_FILE_NAME_LEN];
  char status[16];
  char className[16];
  uint8_t step;
  uint8_t attempts;
  uint32_t stillBytes;
  uint32_t aviBytes;
};

static volatile bool workerAbort = false;
static char activeJobId[24] = {0};

static bool captureOwnsBoard() { return isCapturing || workerAbort; }

static void isoLocalQ(char* out, size_t outLen) {
  time_t epoch = getEpoch();
  struct tm* t = localtime(&epoch);
  if (!t) { strncpy(out, "1970-01-01T00:00:00", outLen - 1); out[outLen - 1] = 0; return; }
  strftime(out, outLen, "%Y-%m-%dT%H:%M:%S", t);
}

static void ensureReviewDirs() {
  if (!STORAGE.exists(REVIEW_DIR)) STORAGE.mkdir(REVIEW_DIR);
  if (!STORAGE.exists(JOBS_DIR)) STORAGE.mkdir(JOBS_DIR);
}

static void jobPath(const char* id, char* out, size_t outLen) {
  snprintf(out, outLen, "%s/%s.json", JOBS_DIR, id);
}

static bool jsonGetStr(const char* json, const char* key, char* out, size_t outLen) {
  char pat[48]; snprintf(pat, sizeof(pat), "\"%s\"", key);
  const char* p = strstr(json, pat);
  if (!p) return false;
  p = strchr(p + strlen(pat), ':');
  if (!p) return false;
  p++; while (*p == ' ' || *p == '\t' || *p == '\n') p++;
  if (*p != '"') return false;
  p++; size_t n = 0;
  while (p[n] && p[n] != '"' && n + 1 < outLen) n++;
  memcpy(out, p, n); out[n] = 0; return true;
}

static bool jsonGetU8(const char* json, const char* key, uint8_t* out) {
  char pat[48]; snprintf(pat, sizeof(pat), "\"%s\"", key);
  const char* p = strstr(json, pat); if (!p) return false;
  p = strchr(p + strlen(pat), ':'); if (!p) return false;
  *out = (uint8_t)atoi(p + 1); return true;
}

static bool jsonGetU32(const char* json, const char* key, uint32_t* out) {
  char pat[48]; snprintf(pat, sizeof(pat), "\"%s\"", key);
  const char* p = strstr(json, pat); if (!p) return false;
  p = strchr(p + strlen(pat), ':'); if (!p) return false;
  *out = (uint32_t)atol(p + 1); return true;
}

static bool readWhole(const char* path, char* buf, size_t bufLen) {
  File f = STORAGE.open(path, FILE_READ); if (!f) return false;
  size_t n = f.read((uint8_t*)buf, bufLen - 1); f.close(); buf[n] = 0; return n > 0;
}

static void writeState(const char* worker, const char* jobId) {
  ensureReviewDirs();
  char ts[24]; isoLocalQ(ts, sizeof(ts));
  File f = STORAGE.open(STATE_PATH, FILE_WRITE); if (!f) return;
  f.printf("{\n  \"worker\": \"%s\",\n  \"active_job\": \"%s\",\n  \"updated\": \"%s\"\n}\n", worker, jobId ? jobId : "", ts);
  f.close();
}

static void writeJob(const TrafficJob* j) {
  ensureReviewDirs();
  char path[IN_FILE_NAME_LEN]; jobPath(j->id, path, sizeof(path));
  char ts[24]; isoLocalQ(ts, sizeof(ts));
  File f = STORAGE.open(path, FILE_WRITE);
  if (!f) { LOG_WRN("queue: cannot write %s", path); return; }
  f.printf("{\n  \"id\": \"%s\",\n  \"kind\": \"clip_analyze\",\n  \"avi\": \"%s\",\n  \"still\": \"%s\",\n  \"sidecar\": \"%s\",\n  \"status\": \"%s\",\n  \"step\": %u,\n  \"attempts\": %u,\n  \"still_bytes\": %lu,\n  \"avi_bytes\": %lu,\n  \"class\": \"%s\",\n  \"updated\": \"%s\"\n}\n",
    j->id, j->avi, j->still, j->sidecar, j->status, j->step, j->attempts,
    (unsigned long)j->stillBytes, (unsigned long)j->aviBytes,
    j->className[0] ? j->className : "unknown", ts);
  f.close();
}

static bool loadJob(const char* id, TrafficJob* j) {
  memset(j, 0, sizeof(*j));
  char path[IN_FILE_NAME_LEN]; jobPath(id, path, sizeof(path));
  char buf[900]; if (!readWhole(path, buf, sizeof(buf))) return false;
  strncpy(j->id, id, sizeof(j->id) - 1);
  jsonGetStr(buf, "avi", j->avi, sizeof(j->avi));
  jsonGetStr(buf, "still", j->still, sizeof(j->still));
  jsonGetStr(buf, "sidecar", j->sidecar, sizeof(j->sidecar));
  jsonGetStr(buf, "status", j->status, sizeof(j->status));
  jsonGetStr(buf, "class", j->className, sizeof(j->className));
  jsonGetU8(buf, "step", &j->step);
  jsonGetU8(buf, "attempts", &j->attempts);
  jsonGetU32(buf, "still_bytes", &j->stillBytes);
  jsonGetU32(buf, "avi_bytes", &j->aviBytes);
  return true;
}

static uint32_t fileSizeOf(const char* path) {
  if (!path || !path[0] || !STORAGE.exists(path)) return 0;
  File f = STORAGE.open(path, FILE_READ); if (!f) return 0;
  uint32_t n = (uint32_t)f.size(); f.close(); return n;
}

static void writeReview(const TrafficJob* j) {
  if (!j->id[0]) return;
  char folder[FILE_NAME_LEN] = {0};
  const char* slash = j->sidecar[0] ? strrchr(j->sidecar, '/') : NULL;
  if (slash) {
    size_t n = (size_t)(slash - j->sidecar);
    if (n >= sizeof(folder)) n = sizeof(folder) - 1;
    memcpy(folder, j->sidecar, n); folder[n] = 0;
  } else strncpy(folder, REVIEW_DIR, sizeof(folder) - 1);
  char ts[24]; isoLocalQ(ts, sizeof(ts));
  char reviewPath[IN_FILE_NAME_LEN];
  snprintf(reviewPath, sizeof(reviewPath), "%s/%s.review.json", folder, j->id);
  File f = STORAGE.open(reviewPath, FILE_WRITE); if (!f) return;
  f.printf("{\n  \"id\": \"%s\",\n  \"board_id\": \"%s\",\n  \"avi\": \"%s\",\n  \"still\": \"%s\",\n  \"status\": \"done\",\n  \"class\": \"%s\",\n  \"still_bytes\": %lu,\n  \"avi_bytes\": %lu,\n  \"analyzed\": \"%s\"\n}\n",
    j->id, boardId, j->avi, j->still, j->className[0] ? j->className : "unknown",
    (unsigned long)j->stillBytes, (unsigned long)j->aviBytes, ts);
  f.close();
}

static bool runStep(TrafficJob* j) {
  if (captureOwnsBoard()) return false;
  vTaskDelay(pdMS_TO_TICKS(STEP_YIELD_MS));
  if (captureOwnsBoard()) return false;
  switch (j->step) {
    case 0: j->stillBytes = fileSizeOf(j->still); j->step = 1; break;
    case 1: j->aviBytes = fileSizeOf(j->avi); j->step = 2; break;
    case 2:
      if (j->aviBytes == 0 && j->stillBytes == 0) strncpy(j->className, "reject", sizeof(j->className) - 1);
      else if (j->stillBytes > 0 && j->stillBytes < 2500) strncpy(j->className, "empty", sizeof(j->className) - 1);
      else strncpy(j->className, "unknown", sizeof(j->className) - 1);
      j->step = 3; break;
    case 3: strncpy(j->status, "done", sizeof(j->status) - 1); writeReview(j); j->step = 4; break;
    default: strncpy(j->status, "done", sizeof(j->status) - 1); break;
  }
  writeJob(j);
  return true;
}

static bool pickNextJob(char* idOut, size_t idLen) {
  File root = STORAGE.open(JOBS_DIR);
  if (!root || !root.isDirectory()) { if (root) root.close(); return false; }
  char best[24] = {0}; int scanned = 0;
  File file = root.openNextFile();
  while (file && scanned < MAX_JOBS_SCAN) {
    scanned++;
    if (!file.isDirectory()) {
      const char* name = file.name();
      const char* base = strrchr(name, '/'); base = base ? base + 1 : name;
      size_t nlen = strlen(base);
      if (nlen > 5 && !strcmp(base + nlen - 5, ".json")) {
        char id[24] = {0}; size_t il = nlen - 5; if (il >= sizeof(id)) il = sizeof(id) - 1;
        memcpy(id, base, il);
        TrafficJob tmp;
        if (loadJob(id, &tmp) && (!strcmp(tmp.status, "queued") || !strcmp(tmp.status, "aborted") || !strcmp(tmp.status, "running"))) {
          if (!best[0] || strcmp(id, best) < 0) strncpy(best, id, sizeof(best) - 1);
        }
      }
    }
    file.close(); file = root.openNextFile();
  }
  root.close();
  if (!best[0]) return false;
  strncpy(idOut, best, idLen - 1); idOut[idLen - 1] = 0; return true;
}

void trafficEnqueueJob(const char* clipId, const char* aviPath, const char* stillPath, const char* sidecarPath) {
  if (!clipId || !clipId[0]) return;
  ensureReviewDirs();
  TrafficJob j;
  if (loadJob(clipId, &j)) {
    if (!strcmp(j.status, "done")) return;
    if (!strcmp(j.status, "failed")) { strncpy(j.status, "queued", sizeof(j.status) - 1); j.step = 0; writeJob(&j); }
    return;
  }
  memset(&j, 0, sizeof(j));
  strncpy(j.id, clipId, sizeof(j.id) - 1);
  if (aviPath) strncpy(j.avi, aviPath, sizeof(j.avi) - 1);
  if (stillPath) strncpy(j.still, stillPath, sizeof(j.still) - 1);
  if (sidecarPath) strncpy(j.sidecar, sidecarPath, sizeof(j.sidecar) - 1);
  strncpy(j.status, "queued", sizeof(j.status) - 1);
  strncpy(j.className, "unknown", sizeof(j.className) - 1);
  writeJob(&j);
  LOG_INF("queue: enqueued %s", clipId);
}

void trafficAbortWorker(const char* reason) {
  workerAbort = true;
  if (activeJobId[0]) {
    TrafficJob j;
    if (loadJob(activeJobId, &j) && strcmp(j.status, "done") && strcmp(j.status, "failed")) {
      strncpy(j.status, "aborted", sizeof(j.status) - 1);
      writeJob(&j);
    }
  }
  writeState("paused_for_capture", activeJobId);
  LOG_INF("queue: abort (%s) job=%s", reason ? reason : "capture", activeJobId[0] ? activeJobId : "-");
}

static void processOne(const char* id) {
  TrafficJob j; if (!loadJob(id, &j)) return;
  if (!strcmp(j.status, "done") || !strcmp(j.status, "failed")) return;
  strncpy(activeJobId, id, sizeof(activeJobId) - 1);
  workerAbort = false;
  j.attempts = (uint8_t)(j.attempts + 1);
  strncpy(j.status, "running", sizeof(j.status) - 1);
  if (j.attempts > 8) { j.step = 0; j.attempts = 1; }
  writeJob(&j); writeState("running", id);
  while (j.step < 4) {
    if (captureOwnsBoard() || !runStep(&j)) {
      strncpy(j.status, "aborted", sizeof(j.status) - 1);
      writeJob(&j); writeState("paused_for_capture", id); activeJobId[0] = 0; return;
    }
    if (!loadJob(id, &j)) break;
  }
  if (j.step >= 4) {
    strncpy(j.status, "done", sizeof(j.status) - 1);
    writeJob(&j); writeState("idle", "");
    LOG_INF("queue: done %s class=%s still=%lu avi=%lu", j.id, j.className,
            (unsigned long)j.stillBytes, (unsigned long)j.aviBytes);
  }
  activeJobId[0] = 0;
}

static void queueWorker(void* /*pv*/) {
  writeState("idle", "");
  for (;;) {
    if (isCapturing) {
      if (activeJobId[0]) trafficAbortWorker("capture");
      workerAbort = false;
      vTaskDelay(pdMS_TO_TICKS(500));
      continue;
    }
    workerAbort = false;
    char id[24];
    if (pickNextJob(id, sizeof(id))) processOne(id);
    else vTaskDelay(pdMS_TO_TICKS(1500));
    vTaskDelay(pdMS_TO_TICKS(200));
  }
}

void trafficQueueSetup() {
#ifndef AUXILIARY
  ensureReviewDirs();
  writeState("idle", "");
  xTaskCreatePinnedToCore(queueWorker, "visionQ", 6144, NULL, 1, NULL, 1);
  LOG_INF("vision queue ready (%s)", JOBS_DIR);
#endif
}
