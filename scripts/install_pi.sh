#!/usr/bin/env bash
set -euo pipefail

APP_USER="${APP_USER:-$USER}"
APP_HOME="$(getent passwd "$APP_USER" | cut -d: -f6)"
APP_ROOT="${APP_ROOT:-/opt/nodespark-wisp}"
WHISPLAY_ROOT="${WHISPLAY_ROOT:-/opt/Whisplay}"
REPO_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"

sudo apt-get update
GPIO_RUNTIME_PACKAGE="libgpiod2"
if ! apt-cache show "$GPIO_RUNTIME_PACKAGE" >/dev/null 2>&1; then
  GPIO_RUNTIME_PACKAGE="libgpiod3"
fi
sudo apt-get install -y \
  python3 python3-venv python3-pip git alsa-utils bluetooth bluez espeak-ng \
  "$GPIO_RUNTIME_PACKAGE" libgpiod-dev python3-dev python3-pil python3-pygame
if apt-cache show libbluetooth-dev >/dev/null 2>&1; then
  sudo apt-get install -y libbluetooth-dev
fi

if [ ! -d "$WHISPLAY_ROOT/.git" ]; then
  sudo git clone --depth 1 https://github.com/PiSugar/Whisplay.git "$WHISPLAY_ROOT"
else
  sudo git -C "$WHISPLAY_ROOT" pull --ff-only
fi

sudo mkdir -p "$APP_ROOT"
sudo rsync -a --delete "$REPO_DIR/" "$APP_ROOT/"
sudo chown -R "$APP_USER":"$APP_USER" "$APP_ROOT"

python3 -m venv --system-site-packages "$APP_ROOT/.venv"
"$APP_ROOT/.venv/bin/pip" install --upgrade pip wheel
"$APP_ROOT/.venv/bin/pip" install -r "$APP_ROOT/requirements.txt"
"$APP_ROOT/.venv/bin/pip" install -e "$APP_ROOT[ble]"
"$APP_ROOT/.venv/bin/pip" install pygame spidev gpiod || {
  echo "Hardware Python packages did not all install. The service can still run, but the Wisp display/button may be unavailable until spidev/gpiod are installed." >&2
}

sudo mkdir -p /etc/nodespark-wisp
if [ ! -f /etc/nodespark-wisp/config.toml ]; then
  sudo cp "$APP_ROOT/config.example.toml" /etc/nodespark-wisp/config.toml
  sudo chown root:"$APP_USER" /etc/nodespark-wisp/config.toml
  sudo chmod 640 /etc/nodespark-wisp/config.toml
fi

sudo cp "$APP_ROOT/systemd/nodespark-wisp.service" /etc/systemd/system/nodespark-wisp.service
sudo sed -i "s|__APP_ROOT__|$APP_ROOT|g; s|__APP_USER__|$APP_USER|g; s|__APP_HOME__|$APP_HOME|g" /etc/systemd/system/nodespark-wisp.service
sudo systemctl daemon-reload
sudo systemctl enable bluetooth.service >/dev/null 2>&1 || true

cat <<EOF

Installed NodeSpark Wisp.

Next:
  1. Edit Hub settings:
     sudo nano /etc/nodespark-wisp/config.toml
  2. Pair with the code from NodeSparkHub:
     $APP_ROOT/scripts/pair_device.sh 123456
  3. Start service:
     sudo systemctl enable --now nodespark-wisp
  4. Watch logs:
     journalctl -u nodespark-wisp -f

Optional Whisplay audio driver:
  sudo bash $WHISPLAY_ROOT/Driver/install_wm8960_drive.sh
  sudo reboot
EOF
