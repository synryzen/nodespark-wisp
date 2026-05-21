#!/usr/bin/env bash
set -euo pipefail

URL="${1:-http://127.0.0.1:8788}"
CHROME_FLAGS=(
  --kiosk
  --noerrdialogs
  --disable-infobars
  --disable-gpu
  --disable-software-rasterizer=false
  --use-gl=swiftshader
)

if command -v chromium-browser >/dev/null 2>&1; then
  exec chromium-browser "${CHROME_FLAGS[@]}" "$URL"
fi

if command -v chromium >/dev/null 2>&1; then
  exec chromium "${CHROME_FLAGS[@]}" "$URL"
fi

if command -v google-chrome >/dev/null 2>&1; then
  exec google-chrome "${CHROME_FLAGS[@]}" "$URL"
fi

echo "No Chromium/Chrome browser found. Open $URL manually."
