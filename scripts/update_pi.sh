#!/usr/bin/env bash
set -euo pipefail

APP_ROOT="${APP_ROOT:-/opt/nodespark-wisp}"

if [ ! -x "$APP_ROOT/.venv/bin/nodespark-wisp" ]; then
  echo "NodeSpark Wisp is not installed at $APP_ROOT. Run scripts/install_pi.sh first." >&2
  exit 2
fi

"$APP_ROOT/.venv/bin/nodespark-wisp" update --app-root "$APP_ROOT"

echo "NodeSpark Wisp update complete."
