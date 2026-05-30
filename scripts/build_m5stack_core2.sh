#!/usr/bin/env bash
set -euo pipefail

SKETCH_DIR="${SKETCH_DIR:-firmware/m5stack-core2-synra/nodespark_synra_core2}"
FQBN="${FQBN:-esp32:esp32:m5stack-core2:PartitionScheme=default,PSRAM=enabled}"

if [ ! -f "$SKETCH_DIR/config.h" ]; then
  cp "$SKETCH_DIR/config.example.h" "$SKETCH_DIR/config.h"
  echo "Created $SKETCH_DIR/config.h from config.example.h."
  echo "Edit Wi-Fi and Hub settings, or use the on-device Synra setup portal after flashing."
fi

arduino-cli compile --fqbn "$FQBN" "$SKETCH_DIR"
