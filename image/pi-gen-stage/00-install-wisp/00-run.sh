#!/bin/bash -e

install -d "${ROOTFS_DIR}/opt/nodespark-wisp"
tar -xzf files/nodespark-wisp.tar.gz -C "${ROOTFS_DIR}/opt/nodespark-wisp" --strip-components=1

install -d "${ROOTFS_DIR}/etc/nodespark-wisp"
sed 's|base_url = ".*"|base_url = ""|' \
  "${ROOTFS_DIR}/opt/nodespark-wisp/config.example.toml" \
  > "${ROOTFS_DIR}/etc/nodespark-wisp/config.toml"

install -d "${ROOTFS_DIR}/usr/local/sbin"
install -m 0755 files/nodespark-wisp-firstboot "${ROOTFS_DIR}/usr/local/sbin/nodespark-wisp-firstboot"

install -d "${ROOTFS_DIR}/etc/systemd/system"
install -m 0644 files/nodespark-wisp-firstboot.service "${ROOTFS_DIR}/etc/systemd/system/nodespark-wisp-firstboot.service"

sed \
  -e 's|__APP_ROOT__|/opt/nodespark-wisp|g' \
  -e 's|__APP_USER__|nodespark|g' \
  -e 's|__APP_HOME__|/var/lib/nodespark-wisp|g' \
  "${ROOTFS_DIR}/opt/nodespark-wisp/systemd/nodespark-wisp.service" \
  > "${ROOTFS_DIR}/etc/systemd/system/nodespark-wisp.service"

BOOT_DIR="${ROOTFS_DIR}/boot/firmware"
if [ ! -d "$BOOT_DIR" ]; then
  BOOT_DIR="${ROOTFS_DIR}/boot"
fi
install -d "$BOOT_DIR"
install -m 0644 files/nodespark-wisp.toml.example "$BOOT_DIR/nodespark-wisp.toml.example"
install -m 0644 files/BOOT_CONFIG_README.txt "$BOOT_DIR/NODESPARK_WISP_README.txt"

CONFIG_TXT="$BOOT_DIR/config.txt"
touch "$CONFIG_TXT"
append_boot_config() {
  local line="$1"
  if ! grep -Fxq "$line" "$CONFIG_TXT"; then
    printf '%s\n' "$line" >> "$CONFIG_TXT"
  fi
}

append_boot_config ""
append_boot_config "# NodeSpark Wisp Whisplay HAT"
append_boot_config "dtparam=spi=on"
append_boot_config "dtparam=i2c_arm=on"

on_chroot <<'EOF'
set -e

if ! id nodespark >/dev/null 2>&1; then
  useradd --system --create-home --home-dir /var/lib/nodespark-wisp --shell /usr/sbin/nologin nodespark
fi

for group in audio video gpio spi i2c; do
  if getent group "$group" >/dev/null 2>&1; then
    usermod -aG "$group" nodespark
  fi
done

chown -R nodespark:nodespark /var/lib/nodespark-wisp
chown -R nodespark:nodespark /opt/nodespark-wisp
chown root:nodespark /etc/nodespark-wisp/config.toml
chmod 0640 /etc/nodespark-wisp/config.toml

if [ ! -d /opt/Whisplay/.git ]; then
  git clone --depth 1 https://github.com/PiSugar/Whisplay.git /opt/Whisplay || true
fi

python3 -m venv --system-site-packages /opt/nodespark-wisp/.venv
/opt/nodespark-wisp/.venv/bin/pip install --upgrade pip wheel
/opt/nodespark-wisp/.venv/bin/pip install -r /opt/nodespark-wisp/requirements.txt
/opt/nodespark-wisp/.venv/bin/pip install -e '/opt/nodespark-wisp[ble]'
/opt/nodespark-wisp/.venv/bin/pip install pygame spidev gpiod || true

systemctl enable bluetooth.service || true
systemctl enable nodespark-wisp-firstboot.service
systemctl enable nodespark-wisp.service
EOF
