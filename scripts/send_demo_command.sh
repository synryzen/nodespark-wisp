#!/usr/bin/env bash
set -euo pipefail

HUB_URL="${HUB_URL:-http://127.0.0.1:8787}"
DEVICE_ID="${1:-}"
TYPE="${2:-display}"
TEXT="${3:-NodeSparkHub can now command this physical NodeSpark Wisp.}"

if [ -z "$DEVICE_ID" ]; then
  echo "Usage: $0 <device-id> [display|card|approval|notify|dashboard|graphics|demo|speak|led|ping|logo|qr] [text]" >&2
  echo "Tip: get device IDs with: curl $HUB_URL/devices" >&2
  exit 2
fi

case "$TYPE" in
  display)
    BODY=$(printf '{"type":"display","title":"NodeSparkHub Live","body":%s,"rgb":[45,160,255]}' "$(python3 -c 'import json,sys; print(json.dumps(sys.argv[1]))' "$TEXT")")
    ;;
  card)
    BODY=$(printf '{"type":"card","style":"ai","icon":"ai","title":"AI Result","subtitle":"Rich Wisp Card","body":%s,"progress":0.82,"rgb":[0,190,255]}' "$(python3 -c 'import json,sys; print(json.dumps(sys.argv[1]))' "$TEXT")")
    ;;
  approval)
    BODY=$(printf '{"type":"approval","title":"Approval Needed","body":%s,"subtitle":"Short press approves. Hold rejects.","choices":["Approve","Reject"],"rgb":[255,180,50]}' "$(python3 -c 'import json,sys; print(json.dumps(sys.argv[1]))' "$TEXT")")
    ;;
  notify|notification)
    BODY=$(printf '{"type":"notification","title":"NodeSparkHub Alert","body":%s,"icon":"bell","rgb":[80,220,140]}' "$(python3 -c 'import json,sys; print(json.dumps(sys.argv[1]))' "$TEXT")")
    ;;
  dashboard)
    BODY='{"type":"dashboard","title":"Workflow Monitor","metricLabel":"Hub","metricValue":"Live","items":["Server online","Watchers active","Device paired"],"rgb":[45,160,255]}'
    ;;
  graphics|icons)
    BODY=$(printf '{"type":"graphics","title":"NodeSparkHub","subtitle":"Physical automation","body":%s,"items":["ai","workflow","phone","spark"],"rgb":[120,190,255]}' "$(python3 -c 'import json,sys; print(json.dumps(sys.argv[1]))' "$TEXT")")
    ;;
  demo|showcase)
    BODY=$(printf '{"type":"demo","title":"Physical Node","body":%s,"rgb":[0,190,255]}' "$(python3 -c 'import json,sys; print(json.dumps(sys.argv[1]))' "$TEXT")")
    ;;
  speak)
    BODY=$(printf '{"type":"speak","text":%s,"rgb":[120,90,255]}' "$(python3 -c 'import json,sys; print(json.dumps(sys.argv[1]))' "$TEXT")")
    ;;
  led)
    BODY='{"type":"led","rgb":[255,180,50]}'
    ;;
  ping)
    BODY='{"type":"ping"}'
    ;;
  logo|splash|startup)
    BODY=$(printf '{"type":"logo","body":%s,"rgb":[0,190,255]}' "$(python3 -c 'import json,sys; print(json.dumps(sys.argv[1]))' "$TEXT")")
    ;;
  qr)
    BODY='{"type":"qr"}'
    ;;
  *)
    echo "Unknown demo command type: $TYPE" >&2
    exit 2
    ;;
esac

curl -sS -X POST "$HUB_URL/devices/$DEVICE_ID/commands" \
  -H "Content-Type: application/json" \
  -d "$BODY"
