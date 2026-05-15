#!/usr/bin/env bash
set -euo pipefail

cd "$(dirname "${BASH_SOURCE[0]}")/.."
python3 -m venv .venv
. .venv/bin/activate
pip install -r requirements.txt
pip install -e .
exec nodespark-wisp "$@"
