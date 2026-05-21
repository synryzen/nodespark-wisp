#!/usr/bin/env bash
set -euo pipefail

URL="${1:-http://127.0.0.1:8788}"

if command -v chromium-browser >/dev/null 2>&1; then
  exec chromium-browser --kiosk --noerrdialogs --disable-infobars "$URL"
fi

if command -v chromium >/dev/null 2>&1; then
  exec chromium --kiosk --noerrdialogs --disable-infobars "$URL"
fi

if command -v google-chrome >/dev/null 2>&1; then
  exec google-chrome --kiosk --noerrdialogs --disable-infobars "$URL"
fi

echo "No Chromium/Chrome browser found. Open $URL manually."

