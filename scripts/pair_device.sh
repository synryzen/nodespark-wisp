#!/usr/bin/env bash
set -euo pipefail

CODE="${1:-}"
APP_ROOT="${APP_ROOT:-/opt/nodespark-wisp}"

if [ -z "$CODE" ]; then
  echo "Usage: $0 <pairing-code-from-NodeSparkHub>" >&2
  exit 2
fi

"$APP_ROOT/.venv/bin/nodespark-wisp" pair --code "$CODE"
