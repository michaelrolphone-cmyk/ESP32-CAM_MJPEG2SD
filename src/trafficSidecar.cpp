/*
  Write still JPEG + JSON sidecar + traffic_log.csv when a motion clip closes.
  No neural net. Classification is a later idle/offline pass (SPEC.md P3+).

  Fork: michaelrolphone-cmyk/ESP32-CAM_MJPEG2SD
*/

#include "appGlobals.h"

#define BOARD_ID_LEN 16
#define TRAFFIC_LOG_PATH "/traffic_log.csv"

char boardId[BOARD_ID_LEN] = "cam-1";

#ifndef AUXILIARY
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

void writeTrafficSidecar(const char* aviPath, uint16_t frames, uint32_t durationSec,
                         uint8_t recFps, const char* sizeStr) {
  if (!aviPath || !aviPath[0]) return;

  char folder[FILE_NAME_LEN];
  char clipId[24];
  if (!stemFromAvi(aviPath, folder, sizeof(folder), clipId, sizeof(clipId))) {
    LOG_WRN("traffic: bad avi path %s", aviPath);
    return;
  }

  char stillPath[IN_FILE_NAME_LEN];
  char jsonPath[IN_FILE_NAME_LEN];
  snprintf(stillPath, sizeof(stillPath), "%s/%s.jpg", folder, clipId);
  snprintf(jsonPath, sizeof(jsonPath), "%s/%s.json", folder, clipId);

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
  } else LOG_WRN("traffic: cannot write %s", jsonPath);

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

#endif
