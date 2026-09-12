# This fork

Fork of [s60sc/ESP32-CAM_MJPEG2SD](https://github.com/s60sc/ESP32-CAM_MJPEG2SD) (v10.9.5).

Target product: event-only person/animal recorder on ESP32-S3-CAM **OV3660 N16R8** dual Type-C, 16 GB U1 microSD.

Read in this order:

1. [SPEC.md](SPEC.md) — product rules
2. [IMPLEMENTATION.md](IMPLEMENTATION.md) — what landed and the remaining hook sites
3. `src/trafficSidecar.cpp` — clip-close still + JSON + CSV

Classification and unique-visitor logic stay offline. Do not put a neural net on the AVI write path.
