#!/usr/bin/env bash
set -euo pipefail

REPO_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
WORKDIR="${WISP_IMAGE_WORKDIR:-$HOME/.cache/nodespark-wisp/pi-gen}"
PI_GEN_REPO="${WISP_PI_GEN_REPO:-https://github.com/RPi-Distro/pi-gen.git}"
PI_GEN_BRANCH="${WISP_PI_GEN_BRANCH:-arm64}"
IMAGE_NAME="${WISP_IMAGE_NAME:-nodespark-wisp}"
RELEASE="${WISP_IMAGE_RELEASE:-trixie}"

if ! command -v git >/dev/null 2>&1; then
  echo "git is required" >&2
  exit 1
fi

if ! command -v tar >/dev/null 2>&1; then
  echo "tar is required" >&2
  exit 1
fi

mkdir -p "$(dirname "$WORKDIR")"

if [ ! -d "$WORKDIR/.git" ]; then
  git clone --depth 1 --branch "$PI_GEN_BRANCH" "$PI_GEN_REPO" "$WORKDIR"
else
  git -C "$WORKDIR" fetch origin "$PI_GEN_BRANCH"
  git -C "$WORKDIR" checkout "$PI_GEN_BRANCH"
  git -C "$WORKDIR" pull --ff-only
fi

STAGE_DIR="$WORKDIR/stage-nodespark-wisp"
rm -rf "$STAGE_DIR"
mkdir -p "$STAGE_DIR/00-install-wisp/files"
rsync -a "$REPO_DIR/image/pi-gen-stage/" "$STAGE_DIR/"
cp "$REPO_DIR/image/nodespark-wisp.toml.example" "$STAGE_DIR/00-install-wisp/files/"
cp "$REPO_DIR/image/BOOT_CONFIG_README.txt" "$STAGE_DIR/00-install-wisp/files/"

tar \
  --exclude='.git' \
  --exclude='.venv' \
  --exclude='src/nodespark_wisp.egg-info' \
  --exclude='__pycache__' \
  --exclude='.DS_Store' \
  -czf "$STAGE_DIR/00-install-wisp/files/nodespark-wisp.tar.gz" \
  -C "$REPO_DIR" .

cat > "$WORKDIR/config" <<EOF
IMG_NAME='$IMAGE_NAME'
PI_GEN_RELEASE='NodeSpark Wisp Raspberry Pi OS'
RELEASE='$RELEASE'
TARGET_HOSTNAME='nodespark-wisp'
DEPLOY_COMPRESSION='xz'
ENABLE_SSH=1
ENABLE_CLOUD_INIT=1
LOCALE_DEFAULT='en_US.UTF-8'
KEYBOARD_KEYMAP='us'
KEYBOARD_LAYOUT='English (US)'
TIMEZONE_DEFAULT='America/Chicago'
WPA_COUNTRY='US'
STAGE_LIST='stage0 stage1 stage2 stage-nodespark-wisp'
EXPORT_CONFIG_DIR='export-image'
EOF

echo "Building NodeSpark Wisp image with pi-gen in $WORKDIR"
echo "This can take a long time and needs Linux or Docker with privileged access."

cd "$WORKDIR"
if [ "${WISP_IMAGE_USE_DOCKER:-1}" = "1" ]; then
  ./build-docker.sh
else
  sudo ./build.sh
fi

echo
echo "Image build complete. Outputs:"
find "$WORKDIR/deploy" -maxdepth 1 -type f -name "${IMAGE_NAME}*" -print
