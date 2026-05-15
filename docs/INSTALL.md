# Install NodeSpark Wisp

NodeSpark Wisp runs on a Raspberry Pi Zero 2 W with the PiSugar Whisplay HAT.

## Requirements

- Raspberry Pi Zero 2 W
- PiSugar Whisplay HAT
- PiSugar battery
- Raspberry Pi OS with Wi-Fi enabled
- NodeSparkHub running on your Mac, with the Hub Server started

## 1. Install On The Pi

```bash
sudo apt-get update
sudo apt-get install -y git
git clone https://github.com/synryzen/nodespark-wisp.git
cd nodespark-wisp
bash scripts/install_pi.sh
```

The installer copies the app to `/opt/nodespark-wisp`, creates a Python virtual environment, installs the systemd service, and creates `/etc/nodespark-wisp/config.toml`.

## 2. Configure Hub URL

In NodeSparkHub on the Mac, open `Settings -> Hub Server` and copy the LAN URL. On the Pi:

```bash
sudo nano /etc/nodespark-wisp/config.toml
```

Set:

```toml
[hub]
base_url = "http://YOUR-MAC-IP:8787"
default_workflow = "Wisp Assistant"
favorite_workflows = ["Wisp Assistant", "Quick Note", "Home Status"]
```

## 3. Pair With NodeSparkHub

In NodeSparkHub, generate a pairing code. Then run:

```bash
/opt/nodespark-wisp/scripts/pair_device.sh 123456
```

Open `Settings -> Hub Server -> Devices` in NodeSparkHub. You should see `NodeSpark Wisp`.

## 4. Start The Service

```bash
sudo systemctl enable --now nodespark-wisp
journalctl -u nodespark-wisp -f
```

## 5. Test Commands

From the Mac running NodeSparkHub:

```bash
curl http://127.0.0.1:8787/devices
```

Copy the Wisp device ID, then:

```bash
bash scripts/send_demo_command.sh <device-id> card "NodeSparkHub is now controlling NodeSpark Wisp."
bash scripts/send_demo_command.sh <device-id> dashboard
bash scripts/send_demo_command.sh <device-id> demo
```

## Optional Wisp Mobile Bridge

To let NodeSpark on iPhone connect to the Wisp over Bluetooth LE, install the optional BLE dependency and enable bridge mode:

```bash
/opt/nodespark-wisp/.venv/bin/pip install 'nodespark-wisp[ble]'
sudo nano /etc/nodespark-wisp/config.toml
```

Set:

```toml
[bluetooth]
enabled = true
device_name = "NodeSpark Wisp"
```

Restart the service:

```bash
sudo systemctl restart nodespark-wisp
```

On iPhone, open NodeSpark, then:

```text
Settings -> Hub Pairing & Control -> Wisp Mobile Bridge
```

Scan for the Wisp and connect. The iPhone bridge can send direct commands to the Wisp and can forward Wisp events to NodeSparkHub when the iPhone is paired with a reachable Hub.

## Optional Audio Driver

If the speaker or microphone does not appear in `arecord -l` and `aplay -l`:

```bash
sudo bash /opt/Whisplay/Driver/install_wm8960_drive.sh
sudo reboot
```

## Update Later

```bash
cd /opt/nodespark-wisp
git pull --ff-only
/opt/nodespark-wisp/scripts/update_pi.sh
```
