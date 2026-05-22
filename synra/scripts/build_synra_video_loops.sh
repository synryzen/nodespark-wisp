#!/usr/bin/env bash
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
ASSETS="$ROOT/web/assets/synra"
OUT="$ASSETS/videos"
WORK="$(mktemp -d)"
FFMPEG="${FFMPEG:-/opt/homebrew/bin/ffmpeg}"
MAGICK="${MAGICK:-/opt/homebrew/bin/magick}"

cleanup() {
  rm -rf "$WORK"
}
trap cleanup EXIT

if [[ ! -x "$FFMPEG" ]]; then
  FFMPEG="$(command -v ffmpeg || true)"
fi
if [[ ! -x "$MAGICK" ]]; then
  MAGICK="$(command -v magick || true)"
fi
if [[ -z "$FFMPEG" || -z "$MAGICK" ]]; then
  printf 'ffmpeg and ImageMagick are required.\n' >&2
  exit 1
fi

mkdir -p "$OUT"

make_frame() {
  local input="$1"
  local output="$2"
  local scale="${3:-1.0}"
  local x="${4:-0}"
  local y="${5:-0}"
  local brightness="${6:-0}"

  "$MAGICK" "$input" \
    -resize 900x900 \
    -background "#04070d" \
    -gravity center \
    -extent 960x960 \
    -distort SRT "${x},${y} ${scale} 0" \
    -brightness-contrast "${brightness}x0" \
    "$output"
}

make_loop() {
  local name="$1"
  shift
  local dir="$WORK/$name"
  mkdir -p "$dir"
  local index=0
  local frame
  for frame in "$@"; do
    cp "$frame" "$dir/$(printf '%03d.png' "$index")"
    index=$((index + 1))
  done

  "$FFMPEG" -y -hide_banner -loglevel error \
    -framerate 8 \
    -stream_loop 2 \
    -i "$dir/%03d.png" \
    -vf "fps=24,format=yuv420p" \
    -c:v libx264 \
    -preset veryfast \
    -crf 20 \
    -movflags +faststart \
    "$OUT/$name.mp4"

  "$FFMPEG" -y -hide_banner -loglevel error \
    -framerate 8 \
    -stream_loop 2 \
    -i "$dir/%03d.png" \
    -vf "fps=24,format=yuv420p" \
    -c:v libvpx-vp9 \
    -b:v 0 \
    -crf 34 \
    "$OUT/$name.webm"
}

tmp_frame() {
  local name="$1"
  local source="$2"
  local scale="${3:-1.0}"
  local x="${4:-0}"
  local y="${5:-0}"
  local brightness="${6:-0}"
  local output="$WORK/$name.png"
  make_frame "$source" "$output" "$scale" "$x" "$y" "$brightness"
  printf '%s\n' "$output"
}

neutral="$ASSETS/expressions/neutral.png"
listening="$ASSETS/expressions/listening.png"
thinking="$ASSETS/expressions/thinking.png"
speaking="$ASSETS/expressions/speaking.png"
happy="$ASSETS/expressions/happy.png"
concerned="$ASSETS/expressions/concerned.png"
curious="$ASSETS/expressions/curious.png"
blink="$ASSETS/rig-poses/blink-closed.png"
half_blink="$ASSETS/rig-poses/half-blink.png"
mouth_a="$ASSETS/rig-poses/mouth-a.png"
mouth_ei="$ASSETS/rig-poses/mouth-ei.png"
mouth_ou="$ASSETS/rig-poses/mouth-ou.png"
look_left="$ASSETS/rig-poses/look-left.png"
look_right="$ASSETS/rig-poses/look-right.png"
look_up="$ASSETS/rig-poses/look-up.png"
look_down="$ASSETS/rig-poses/look-down.png"

make_loop idle \
  "$(tmp_frame idle_00 "$neutral" 1.00 0 0 0)" \
  "$(tmp_frame idle_01 "$neutral" 1.015 0 -5 1)" \
  "$(tmp_frame idle_02 "$half_blink" 1.012 0 -4 0)" \
  "$(tmp_frame idle_03 "$blink" 1.01 0 -3 -1)" \
  "$(tmp_frame idle_04 "$half_blink" 1.012 0 -4 0)" \
  "$(tmp_frame idle_05 "$neutral" 1.018 -5 -7 1)" \
  "$(tmp_frame idle_06 "$look_left" 1.012 -8 -4 0)" \
  "$(tmp_frame idle_07 "$neutral" 1.00 0 0 0)" \
  "$(tmp_frame idle_08 "$look_right" 1.012 8 -4 0)" \
  "$(tmp_frame idle_09 "$neutral" 1.01 0 -4 1)"

make_loop listening \
  "$(tmp_frame listening_00 "$listening" 1.00 0 0 0)" \
  "$(tmp_frame listening_01 "$listening" 1.018 0 -6 1)" \
  "$(tmp_frame listening_02 "$look_up" 1.012 0 -6 0)" \
  "$(tmp_frame listening_03 "$listening" 1.02 -4 -8 1)" \
  "$(tmp_frame listening_04 "$half_blink" 1.012 0 -4 0)" \
  "$(tmp_frame listening_05 "$listening" 1.00 0 0 0)"

make_loop thinking \
  "$(tmp_frame thinking_00 "$thinking" 1.00 0 0 0)" \
  "$(tmp_frame thinking_01 "$thinking" 1.01 -3 -4 -1)" \
  "$(tmp_frame thinking_02 "$look_down" 1.012 0 3 -1)" \
  "$(tmp_frame thinking_03 "$thinking" 1.018 4 -5 0)" \
  "$(tmp_frame thinking_04 "$half_blink" 1.01 0 -3 -1)" \
  "$(tmp_frame thinking_05 "$thinking" 1.00 0 0 0)"

make_loop speaking \
  "$(tmp_frame speaking_00 "$speaking" 1.00 0 0 0)" \
  "$(tmp_frame speaking_01 "$mouth_ei" 1.015 0 -4 0)" \
  "$(tmp_frame speaking_02 "$mouth_a" 1.02 0 -6 1)" \
  "$(tmp_frame speaking_03 "$mouth_ou" 1.018 -3 -5 0)" \
  "$(tmp_frame speaking_04 "$mouth_ei" 1.014 3 -3 0)" \
  "$(tmp_frame speaking_05 "$speaking" 1.00 0 0 0)"

make_loop success \
  "$(tmp_frame success_00 "$happy" 1.00 0 0 1)" \
  "$(tmp_frame success_01 "$happy" 1.025 0 -8 3)" \
  "$(tmp_frame success_02 "$ASSETS/expressions/wink.png" 1.02 0 -7 2)" \
  "$(tmp_frame success_03 "$happy" 1.018 0 -6 2)" \
  "$(tmp_frame success_04 "$happy" 1.00 0 0 1)"

make_loop concerned \
  "$(tmp_frame concerned_00 "$concerned" 1.00 0 0 -2)" \
  "$(tmp_frame concerned_01 "$concerned" 1.006 -2 1 -2)" \
  "$(tmp_frame concerned_02 "$ASSETS/rig-poses/concerned.png" 1.008 2 2 -2)" \
  "$(tmp_frame concerned_03 "$half_blink" 1.00 0 0 -3)" \
  "$(tmp_frame concerned_04 "$concerned" 1.00 0 0 -2)"

make_loop approval \
  "$(tmp_frame approval_00 "$curious" 1.00 0 0 0)" \
  "$(tmp_frame approval_01 "$curious" 1.016 0 -5 1)" \
  "$(tmp_frame approval_02 "$look_right" 1.012 8 -4 0)" \
  "$(tmp_frame approval_03 "$curious" 1.018 -4 -6 1)" \
  "$(tmp_frame approval_04 "$curious" 1.00 0 0 0)"

cat > "$OUT/manifest.json" <<'JSON'
{
  "idle": "/assets/synra/videos/idle.mp4",
  "listening": "/assets/synra/videos/listening.mp4",
  "thinking": "/assets/synra/videos/thinking.mp4",
  "speaking": "/assets/synra/videos/speaking.mp4",
  "success": "/assets/synra/videos/success.mp4",
  "concerned": "/assets/synra/videos/concerned.mp4",
  "approval": "/assets/synra/videos/approval.mp4"
}
JSON

printf 'Built Synra video loops in %s\n' "$OUT"
