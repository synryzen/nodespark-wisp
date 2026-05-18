# Install NodeSpark Wisp

NodeSpark Wisp runs on a Raspberry Pi Zero 2 W with the PiSugar Whisplay HAT.
The repo also includes ESP32-S3 and M5Stack Core2 touch-screen firmware builds.

## Requirements

- Raspberry Pi Zero 2 W
- PiSugar Whisplay HAT
- PiSugar battery
- Raspberry Pi OS with Wi-Fi enabled
- NodeSparkHub running on your Mac, with the Hub Server started
- NodeSparkHub 3.2 or newer for Intelligence Center and Wisp Assistant
- NodeSpark iOS 3.2 or newer for Wisp Mobile Bridge intelligence settings
- Active NodeSparkHub All Access subscription for live Wisp operation

NodeSpark Wisp is not feature-tiered on the device. The subscription activates
the NodeSparkHub runtime used for pairing, secure commands, workflow execution,
automation history, and iPhone Mobile Bridge forwarding.

## Option A: Flash The NodeSpark Wisp Raspberry Pi Image

The customer-friendly install path is the custom NodeSpark Wisp Raspberry Pi OS
image. It boots with Wisp already installed and enabled, including the optional
Bluetooth LE dependency used by Wisp Mobile Bridge.

To use it:

1. Download the latest NodeSpark Wisp `.img.xz` image from GitHub Releases or a
   build artifact.
2. Open Raspberry Pi Imager.
3. Choose **Use custom** and select the Wisp image.
4. In Imager settings, set Wi-Fi, username/password, locale, and optional SSH.
5. Flash the microSD card.
6. Optional: after flashing, open the boot partition, copy
   `nodespark-wisp.toml.example` to `nodespark-wisp.toml`, and set the Hub URL.
7. Boot the Pi with the Whisplay HAT attached.
8. Pair with NodeSparkHub.

The image intentionally does not ship with a default username/password. Use
Raspberry Pi Imager to set credentials before flashing.

Build recipe for maintainers:

```bash
bash scripts/build_pi_image.sh
```

Details are in:

```text
image/README.md
```

## Option B: Manual Install On The Pi

```bash
sudo apt-get update
sudo apt-get install -y git
git clone https://github.com/synryzen/nodespark-wisp.git
cd nodespark-wisp
bash scripts/install_pi.sh
```

The installer copies the app to `/opt/nodespark-wisp`, creates a Python virtual environment, installs the systemd service, and creates `/etc/nodespark-wisp/config.toml`.

## Configure Hub URL

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

## Pair With NodeSparkHub

In NodeSparkHub, first open `Settings -> Intelligence` and confirm `Wisp
Assistant` is enabled. Then generate a pairing code and run:

```bash
/opt/nodespark-wisp/scripts/pair_device.sh 123456
```

Open `Settings -> Hub Server -> Devices` in NodeSparkHub. You should see `NodeSpark Wisp`.

To test direct AI assistant replies from the device, use a Wisp build with
speaker support and make sure NodeSparkHub has a default AI profile configured.
The device sends assistant requests to NodeSparkHub through `/wisp/assistant`,
so normal Hub pairing and device tokens are still used.

## Start The Service

```bash
sudo systemctl enable --now nodespark-wisp
journalctl -u nodespark-wisp -f
```

## Test Commands

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

To let NodeSpark on iPhone connect to the Wisp over Bluetooth LE, enable bridge
mode. The custom Raspberry Pi image and `install_pi.sh` already include the BLE
dependency.

```bash
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

For iPhone-side intelligence controls, open:

```text
Settings -> Intelligence
```

Keep `Wisp Mobile Assist` enabled if you want NodeSpark iOS to forward Wisp
assistant and workflow events through the current Hub connection.

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

## ESP32-S3 Touch Build

The ESP32-S3 build uses:

- ESP32-S3 N16R8 USB-C development board
- ILI9341 2.8-inch SPI TFT LCD touch display
- MAX98357 I2S amplifier and speaker
- INMP441 I2S microphone

Read the wiring guide:

```text
firmware/esp32-s3-wisp/README.md
```

Create the firmware config:

```bash
cp firmware/esp32-s3-wisp/nodespark_wisp_esp32/config.example.h \
  firmware/esp32-s3-wisp/nodespark_wisp_esp32/config.h
```

Edit `config.h` with Wi-Fi and the NodeSparkHub LAN URL, then compile:

```bash
bash scripts/build_esp32_s3.sh
```

Upload with Arduino IDE or:

```bash
arduino-cli upload \
  -p /dev/cu.usbmodemXXXX \
  --fqbn esp32:esp32:esp32s3:FlashSize=16M,PSRAM=opi,PartitionScheme=app3M_fat9M_16MB \
  firmware/esp32-s3-wisp/nodespark_wisp_esp32
```

Pair from the ESP32 touch screen by opening the `Pair` tab, entering the code
from NodeSparkHub `Settings -> Hub Server -> Devices`, and tapping `Pair`.

The ESP32-S3 build uses the 16 MB / 3 MB app partition because Bluetooth
Mobile Bridge, HTTPS Hub access, touchscreen UI, and display graphics no longer
fit in the default 1.2 MB app partition.

## M5Stack Core2 Build

The M5Stack Core2 build uses the hardware already built into the Core2:

- 2-inch 320x240 touch display
- Built-in speaker amplifier
- Built-in microphone
- Built-in vibration motor
- Built-in battery and power management
- Built-in IMU, RTC, and SD card slot

No display, touch, mic, amp, battery, or SD wiring is required.

Create the firmware config:

```bash
cp firmware/m5stack-core2-wisp/nodespark_wisp_core2/config.example.h \
  firmware/m5stack-core2-wisp/nodespark_wisp_core2/config.h
```

Edit `config.h` with Wi-Fi and the NodeSparkHub URL. A local LAN Hub URL looks
like `http://192.168.1.241:8787`; a remote Cloudflare URL can look like
`https://nodespark.msidragon.com`.

Compile:

```bash
bash scripts/build_m5stack_core2.sh
```

Upload with Arduino CLI:

```bash
arduino-cli upload \
  -p /dev/cu.usbserial-XXXX \
  --fqbn esp32:esp32:m5stack-core2:PartitionScheme=default,PSRAM=enabled \
  firmware/m5stack-core2-wisp/nodespark_wisp_core2
```

After upload, open `Pair` on the Core2 touch screen, enter the device code from
NodeSparkHub `Settings -> Hub Server -> Devices`, and tap `Pair`.
