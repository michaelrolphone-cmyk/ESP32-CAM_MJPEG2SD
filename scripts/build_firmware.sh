#!/usr/bin/env bash
set -euo pipefail

ESP32_CORE_VERSION="${ESP32_CORE_VERSION:-3.3.11}"
FQBN="${FQBN:-esp32:esp32:esp32s3:FlashSize=16M,PartitionScheme=app3M_fat9M_16MB,PSRAM=opi}"
BUILD_DIR="${BUILD_DIR:-build/esp32s3-n16r8}"
DIST_DIR="${DIST_DIR:-dist}"

if ! command -v arduino-cli >/dev/null 2>&1; then
  echo "arduino-cli is required" >&2
  exit 1
fi

VERSION="${RELEASE_VERSION:-}"
if [[ -z "$VERSION" ]]; then
  VERSION="$(sed -nE 's/^[[:space:]]*#define[[:space:]]+APP_VER[[:space:]]+"([^"]+)".*/\1/p' src/appGlobals.h | head -n1)"
fi
if [[ -z "$VERSION" ]]; then
  echo "Could not determine APP_VER from src/appGlobals.h" >&2
  exit 1
fi

rm -rf "$BUILD_DIR" "$DIST_DIR"
mkdir -p "$BUILD_DIR" "$DIST_DIR"

echo "Building ESP32-CAM_MJPEG2SD ${VERSION}"
echo "Core: esp32:esp32@${ESP32_CORE_VERSION}"
echo "FQBN: ${FQBN}"

arduino-cli compile \
  --fqbn "$FQBN" \
  --build-path "$BUILD_DIR" \
  --warnings all \
  .

APP_BIN="$(find "$BUILD_DIR" -maxdepth 1 -type f -name '*.ino.bin' -print -quit)"
APP_ELF="$(find "$BUILD_DIR" -maxdepth 1 -type f -name '*.ino.elf' -print -quit)"
FLASH_ARGS="$(find "$BUILD_DIR" -maxdepth 1 -type f -name 'flash_args' -print -quit)"

if [[ -z "$APP_BIN" || -z "$APP_ELF" ]]; then
  echo "Expected Arduino app binary/ELF was not produced" >&2
  find "$BUILD_DIR" -maxdepth 2 -type f -print | sort
  exit 1
fi

if ! grep -a -F -q "$VERSION" "$APP_BIN"; then
  echo "Compiled firmware does not contain APP_VER ${VERSION}" >&2
  exit 1
fi

PREFIX="ESP32-CAM_MJPEG2SD-${VERSION}-esp32s3-n16r8"
cp "$APP_BIN" "$DIST_DIR/${PREFIX}-ota.bin"
cp "$APP_ELF" "$DIST_DIR/${PREFIX}.elf"

if [[ -z "$FLASH_ARGS" ]]; then
  echo "flash_args was not generated; cannot create a complete 0x0 flash image" >&2
  exit 1
fi

ARDUINO_DATA="${ARDUINO_DATA_DIR:-$HOME/.arduino15}"
ESPTOOL="$(find "$ARDUINO_DATA/packages/esp32/tools" -type f \( -name 'esptool' -o -name 'esptool.py' \) -print 2>/dev/null | head -n1 || true)"
if [[ -z "$ESPTOOL" ]]; then
  echo "Could not locate the esptool bundled with arduino-esp32" >&2
  exit 1
fi

run_esptool() {
  if [[ "$ESPTOOL" == *.py ]]; then
    python3 "$ESPTOOL" "$@"
  else
    "$ESPTOOL" "$@"
  fi
}

FULL_BIN="$PWD/$DIST_DIR/${PREFIX}-full.bin"
(
  cd "$BUILD_DIR"
  if ! run_esptool --chip esp32s3 merge_bin -o "$FULL_BIN" @flash_args; then
    echo "merge_bin spelling not accepted; retrying merge-bin"
    run_esptool --chip esp32s3 merge-bin -o "$FULL_BIN" @flash_args
  fi
)

if [[ ! -s "$FULL_BIN" ]]; then
  echo "Merged full-flash image was not created" >&2
  exit 1
fi

if [[ -d data ]]; then
  zip -qr "$DIST_DIR/${PREFIX}-data.zip" data
fi

cat > "$DIST_DIR/BUILD_INFO.txt" <<EOF
project=ESP32-CAM_MJPEG2SD
version=${VERSION}
target=ESP32-S3-CAM OV3660 N16R8
fqbn=${FQBN}
arduino_esp32_core=${ESP32_CORE_VERSION}
git_commit=${GITHUB_SHA:-$(git rev-parse HEAD 2>/dev/null || echo unknown)}
ota_image=${PREFIX}-ota.bin
full_image=${PREFIX}-full.bin
EOF

echo "Build artifacts:"
ls -lh "$DIST_DIR"
