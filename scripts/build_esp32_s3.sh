#!/usr/bin/env bash
set -euo pipefail

SKETCH_DIR="${SKETCH_DIR:-firmware/esp32-s3-wisp/nodespark_wisp_esp32}"
FQBN="${FQBN:-esp32:esp32:esp32s3:FlashSize=16M,PSRAM=opi}"

if [ ! -f "$SKETCH_DIR/config.h" ]; then
  cp "$SKETCH_DIR/config.example.h" "$SKETCH_DIR/config.h"
  echo "Created $SKETCH_DIR/config.h from config.example.h."
  echo "Edit Wi-Fi and Hub settings before uploading to real hardware."
fi

arduino-cli compile --fqbn "$FQBN" "$SKETCH_DIR"

