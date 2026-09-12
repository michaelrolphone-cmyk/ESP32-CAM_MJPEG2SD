/*
* User modifiable features
*
* s60sc 2026
*
* This fork (michaelrolphone-cmyk/ESP32-CAM_MJPEG2SD) targets the
* ESP32-S3-CAM OV3660 N16R8 dual Type-C board. Read SPEC.md before
* changing capture defaults. Classification is offline — do not add
* a neural net on the AVI write path.
*/

#pragma once

#if defined(CONFIG_IDF_TARGET_ESP32)
#define CAMERA_MODEL_AI_THINKER
#elif defined(CONFIG_IDF_TARGET_ESP32S3)
#define CAMERA_MODEL_ESP32_S3_CAM
#endif

#define INCLUDE_FTP_HFS false
#define INCLUDE_TGRAM false
#define INCLUDE_AUDIO false
#define INCLUDE_PERIPH false
#define INCLUDE_SMTP false
#define INCLUDE_MQTT false
#define INCLUDE_HASIO false
#define INCLUDE_CERTS false
#define INCLUDE_UART false
#define INCLUDE_TELEM false
#define INCLUDE_WEBDAV false
#define INCLUDE_EXTHB false
#define INCLUDE_PGRAM false
#define INCLUDE_MCPWM false
#define INCLUDE_RTSP false
#define INCLUDE_DS18B20 false
#define INCLUDE_AF false
#define INCLUDE_NEW_JPG false
#define INCLUDE_I2C false
#define USE_SSD1306 false
#define USE_BMx280 false
#define USE_MPU false
#define USE_DS3231 false
#define USE_LCD1602 false
#define USE_MS6511 false
#define INCLUDE_TINYML false
#define TINY_ML_LIB "your_impulse_edge_library.h"
#define ALLOW_SPACES false
#define HTTP_PORT 80
#define HTTPS_PORT 443
#define USE_IP6 false

#include "src/appGlobals.h"

void trafficSetup();
void trafficQueueSetup();
void trafficEnqueueJob(const char* clipId, const char* aviPath, const char* stillPath, const char* sidecarPath);
void trafficAbortWorker(const char* reason);
