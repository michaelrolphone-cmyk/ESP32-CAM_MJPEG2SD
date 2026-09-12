/*
  Traffic monitor extras for this fork.
  - One-time capture defaults (VGA/10fps/event-only)
  - After an AVI is closed, write still JPEG + JSON sidecar + traffic_log.csv
  No neural net. Classification is SPEC.md P3+.
*/

#include "appGlobals.h"

#define BOARD_ID_LEN 16
#define TRAFFIC_LOG_PATH "/traffic_log.csv"

char boardId[BOARD_ID_LEN] = "cam-1";
float clipMotionPeak = 0.0f;

void trafficResetClipStats() {
  clipMotionPeak = 0.0f;
}

void trafficNoteMotion(float ratio) {
  if (ratio > clipMotionPeak) clipMotionPeak = ratio;
}

static bool stemFromAvi(const char* aviPath, char* folder, size_t folderLen, char* clipId, size_t clipIdLen) {
  if (!aviPath || aviPath[0] != '/') return false;
  const char* slash = strrchr(aviPath, '/');
  if (!slash || slash == aviPath) return false;
  size_t flen = (size_t)(slash - aviPath);
  if (flen >= folderLen) return false;
  memcpy(folder, aviPath, flen);
  folder[flen] = 0;

  const char* base = slash + 1;
  size_t n = 0;
  while (base[n] && base[n] != '_' && n < 8) n++;
  if (base[n] == '_' && n == 8) {
    n = 15;
    if (strlen(base) < 15) n = strlen(base);
  } else {
    const char* dot = strrchr(base, '.');
    n = dot ? (size_t)(dot - base) : strlen(base);
  }
  if (n >= clipIdLen) n = clipIdLen - 1;
  memcpy(clipId, base, n);
  clipId[n] = 0;
  return clipId[0] != 0;
}

static void isoLocal(char* out, size_t outLen) {
  time_t epoch = getEpoch();
  struct tm* t = localtime(&epoch);
  if (!t) {
    strncpy(out, "1970-01-01T00:00:00", outLen - 1);
    out[outLen - 1] = 0;
    return;
  }
  strftime(out, outLen, "%Y-%m-%dT%H:%M:%S", t);
}

static void ensureLogHeader() {
  bool needHdr = true;
  if (STORAGE.exists(TRAFFIC_LOG_PATH)) {
    File f = STORAGE.open(TRAFFIC_LOG_PATH, FILE_READ);
    needHdr = (!f || f.size() == 0);
    if (f) f.close();
  }
  if (!needHdr) return;
  File f = STORAGE.open(TRAFFIC_LOG_PATH, FILE_WRITE);
  if (!f) {
    LOG_WRN("traffic: cannot create %s", TRAFFIC_LOG_PATH);
    return;
  }
  f.println("ts,board_id,clip_id,duration_s,frames,motion_peak,status,avi,still");
  f.close();
}

static void parseAviMeta(const char* aviPath, char* sizeStr, size_t sizeLen,
                         uint8_t* recFps, uint32_t* durationSec) {
  strncpy(sizeStr, "VGA", sizeLen - 1);
  sizeStr[sizeLen - 1] = 0;
  *recFps = FPS ? FPS : 10;
  *durationSec = 0;
  const char* slash = strrchr(aviPath, '/');
  const char* base = slash ? slash + 1 : aviPath;
  const char* p = strchr(base, '_');
  if (p) p = strchr(p + 1, '_');
  if (!p) return;
  p++;
  const char* fpsTok = strchr(p, '_');
  if (!fpsTok) return;
  size_t slen = (size_t)(fpsTok - p);
  if (slen >= sizeLen) slen = sizeLen - 1;
  memcpy(sizeStr, p, slen);
  sizeStr[slen] = 0;
  *recFps = (uint8_t)atoi(fpsTok + 1);
  const char* durTok = strchr(fpsTok + 1, '_');
  if (durTok) *durationSec = (uint32_t)atoi(durTok + 1);
}

void writeTrafficSidecar(const char* aviPath, uint16_t frames, uint32_t durationSec,
                         uint8_t recFps, const char* sizeStr) {
  if (!aviPath || !aviPath[0]) return;

  char folder[FILE_NAME_LEN];
  char clipId[24];
  if (!stemFromAvi(aviPath, folder, sizeof(folder), clipId, sizeof(clipId))) {
    LOG_WRN("traffic: bad avi path %s", aviPath);
    return;
  }

  char jsonPath[IN_FILE_NAME_LEN];
  snprintf(jsonPath, sizeof(jsonPath), "%s/%s.json", folder, clipId);
  if (STORAGE.exists(jsonPath)) return;

  char stillPath[IN_FILE_NAME_LEN];
  snprintf(stillPath, sizeof(stillPath), "%s/%s.jpg", folder, clipId);

  bool haveStill = false;
  if (alertBuffer != NULL && alertBufferSize > 0) {
    File jpg = STORAGE.open(stillPath, FILE_WRITE);
    if (jpg) {
      size_t wr = jpg.write(alertBuffer, alertBufferSize);
      jpg.close();
      haveStill = (wr == alertBufferSize);
      if (!haveStill) LOG_WRN("traffic: short still write %u/%u", (unsigned)wr, (unsigned)alertBufferSize);
    } else LOG_WRN("traffic: cannot write %s", stillPath);
  }

  char ts[24];
  isoLocal(ts, sizeof(ts));

  File js = STORAGE.open(jsonPath, FILE_WRITE);
  if (js) {
    js.printf(
      "{\n"
      "  \"id\": \"%s\",\n"
      "  \"board_id\": \"%s\",\n"
      "  \"avi\": \"%s\",\n"
      "  \"still\": \"%s\",\n"
      "  \"started\": \"%s\",\n"
      "  \"duration_s\": %lu,\n"
      "  \"frames\": %u,\n"
      "  \"fps\": %u,\n"
      "  \"size\": \"%s\",\n"
      "  \"motion_peak\": %.4f,\n"
      "  \"light_level\": %u,\n"
      "  \"status\": \"captured\"\n"
      "}\n",
      clipId,
      boardId,
      aviPath,
      haveStill ? stillPath : "",
      ts,
      (unsigned long)durationSec,
      frames,
      recFps,
      sizeStr ? sizeStr : "",
      (double)clipMotionPeak,
      lightLevel
    );
    js.close();
  } else {
    LOG_WRN("traffic: cannot write %s", jsonPath);
    return;
  }

  ensureLogHeader();
  File csv = STORAGE.open(TRAFFIC_LOG_PATH, FILE_APPEND);
  if (csv) {
    csv.printf("%s,%s,%s,%lu,%u,%.4f,captured,%s,%s\n",
               ts, boardId, clipId,
               (unsigned long)durationSec, frames, (double)clipMotionPeak,
               aviPath, haveStill ? stillPath : "");
    csv.close();
  } else LOG_WRN("traffic: cannot append %s", TRAFFIC_LOG_PATH);

  LOG_INF("traffic sidecar %s still=%s peak=%.3f", jsonPath, haveStill ? "yes" : "no", (double)clipMotionPeak);
}

static void applyProfileOnce() {
  char existing[BOARD_ID_LEN] = {0};
  if (retrieveConfigVal("boardId", existing) && existing[0]) {
    strncpy(boardId, existing, BOARD_ID_LEN - 1);
    LOG_INF("traffic board_id %s", boardId);
    return;
  }

  LOG_INF("traffic: applying first-boot capture profile");
  strncpy(boardId, "cam-1", BOARD_ID_LEN - 1);
  updateStatus("boardId", boardId, true);
  updateStatus("fps", "10", true);
  updateStatus("quality", "12", true);
  updateStatus("minf", "2", true);
  updateStatus("moveStopSecs", "3", true);
  updateStatus("maxFrames", "300", true);
  updateStatus("detectMotionFrames", "3", true);
  updateStatus("dashCamOn", "0", true);
  updateStatus("timeLapseOn", "0", true);
  updateStatus("enableMotion", "1", true);
  updateStatus("vflip", "1", true);
  updateStatus("timezone", "MST7MDT,M3.2.0,M11.1.0", true);
  updateStatus("sdMinCardFreeSpace", "400", true);
  updateStatus("sdFreeSpaceMode", "1", true);
  updateStatus("framesize", "10", true);
  updateStatus("save", "1", true);
}

static void scanFolderForAvis(const char* folder) {
  File root = STORAGE.open(folder);
  if (!root || !root.isDirectory()) {
    if (root) root.close();
    return;
  }
  File file = root.openNextFile();
  while (file) {
    if (!file.isDirectory()) {
      const char* name = file.name();
      const char* base = strrchr(name, '/');
      base = base ? base + 1 : name;
      size_t nlen = strlen(base);
      if (nlen > 4 && !strcmp(base + nlen - 4, ".avi") && strcmp(base, "current.avi")) {
        char aviPath[IN_FILE_NAME_LEN];
        if (name[0] == '/') strncpy(aviPath, name, sizeof(aviPath) - 1);
        else snprintf(aviPath, sizeof(aviPath), "%s/%s", folder, base);
        aviPath[sizeof(aviPath) - 1] = 0;

        char sizeStr[12];
        uint8_t recFps = 10;
        uint32_t durationSec = 0;
        parseAviMeta(aviPath, sizeStr, sizeof(sizeStr), &recFps, &durationSec);
        uint16_t frames = (uint16_t)(recFps * durationSec);
        writeTrafficSidecar(aviPath, frames, durationSec, recFps, sizeStr);
      }
    }
    file.close();
    file = root.openNextFile();
  }
  root.close();
}

static void trafficTask(void* /*pv*/) {
  for (;;) {
    vTaskDelay(pdMS_TO_TICKS(2000));
    if (isCapturing) continue;
    char folder[FILE_NAME_LEN];
    dateFormat(folder, sizeof(folder), true);
    scanFolderForAvis(folder);
  }
}

void trafficSetup() {
#ifndef AUXILIARY
  applyProfileOnce();
  xTaskCreatePinnedToCore(trafficTask, "traffic", 4096, NULL, 1, NULL, 1);
  LOG_INF("traffic monitor ready (event AVI + sidecar)");
#endif
}
