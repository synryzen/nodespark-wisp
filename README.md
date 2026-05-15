# NodeSpark Wisp

NodeSpark Wisp is a pocket-sized physical companion for NodeSparkHub. It turns workflows into a real display, speaker, microphone, approval button, status screen, and sales-demo device.

It runs on the same hardware stack used by OpenClaw-style DIY assistants:

- Raspberry Pi Zero 2 W
- PiSugar Whisplay HAT, 1.69-inch LCD, WM8960 speaker/microphone, RGB LED, button
- PiSugar battery

This device connects to NodeSparkHub over the Hub HTTP API that already exists in your apps:

- `POST /pair`
- `POST /devices/checkin`
- `GET /workflows`
- `POST /workflows/<name>/run`
- `GET /runs/<id>/status`

## What It Does

- Pairs with NodeSparkHub using the same one-time pairing code flow as the iOS app.
- Checks in every minute so it appears in Hub connected devices.
- Uses the Wisp button:
  - short press cycles favorite workflows
  - hold records a voice command and runs the selected workflow
- Shows connection, listening, transcript, run, and result states on the Wisp screen.
- Shows the bundled NodeSpark mascot as a branded startup logo.
- Shows animated listening/thinking/running screens during live demos.
- Shows an optional Wi-Fi/IP/battery status footer on device screens.
- Shows a QR pairing screen when the device is not paired.
- Plays simple startup, success, listening, and error chimes when audio is available.
- Speaks Hub workflow output through `espeak-ng` when installed.
- Supports OpenAI transcription, offline Vosk transcription, or no transcription.
- Polls a secure Hub command inbox so NodeSparkHub can control the physical device.

## Hub-to-Device Commands

NodeSparkHub now exposes a paired-device command channel:

- `POST /devices/<deviceId>/commands`
- `GET /devices/<deviceId>/commands/poll`
- `POST /devices/<deviceId>/commands/<commandId>/ack`

Supported Wisp command types:

- `display`: show a title/body on the LCD
- `card`: show a branded rich card with style, icon, subtitle, footer, and progress
- `approval`: show an approve/reject prompt; short press approves, hold rejects
- `notification` or `notify`: add an alert to the device notification stack
- `dashboard`: show a compact metric dashboard with list items
- `graphics` or `icons`: show a graphic/icon grid
- `speak`: speak text through the Wisp speaker
- `led`: set the RGB LED color
- `ping`: show a live connectivity ping
- `logo`, `splash`, or `startup`: show the branded NodeSpark mascot screen
- `qr`: show custom QR data, or the pairing/device-identity QR screen
- `demo`: run the full sales-showcase sequence
- `runWorkflow`: ask the device to start another Hub workflow
- `selectWorkflow`: change the selected favorite workflow

From the Mac running NodeSparkHub, localhost calls are allowed for demo control:

```bash
curl http://127.0.0.1:8787/devices
```

Then send a screen command:

```bash
bash scripts/send_demo_command.sh <device-id> display "A Stripe order arrived. NodeSparkHub routed it to this device."
```

Or make it speak:

```bash
bash scripts/send_demo_command.sh <device-id> speak "NodeSparkHub just controlled a Raspberry Pi display from a workflow."
```

Or show the branded startup screen during a demo:

```bash
bash scripts/send_demo_command.sh <device-id> logo "NodeSparkHub turns workflows into physical experiences."
```

Or show the pairing/device QR:

```bash
bash scripts/send_demo_command.sh <device-id> qr
```

Raw JSON example:

```bash
curl -X POST "http://127.0.0.1:8787/devices/<device-id>/commands" \
  -H "Content-Type: application/json" \
  -d '{"type":"card","style":"ai","icon":"ai","title":"AI Result","subtitle":"NodeSparkHub","body":"The workflow finished and the physical device updated instantly.","progress":0.82,"rgb":[0,190,255]}'
```

Approval example:

```bash
curl -X POST "http://127.0.0.1:8787/devices/<device-id>/commands" \
  -H "Content-Type: application/json" \
  -d '{"type":"approval","title":"Approval Needed","body":"Approve the next workflow step?","choices":["Approve","Reject"],"rgb":[255,180,50]}'
```

Dashboard example:

```bash
curl -X POST "http://127.0.0.1:8787/devices/<device-id>/commands" \
  -H "Content-Type: application/json" \
  -d '{"type":"dashboard","title":"Workflow Monitor","metricLabel":"Hub","metricValue":"Live","items":["Server online","Watchers active","Device paired"],"rgb":[45,160,255]}'
```

That is the demo magic: NodeSparkHub can receive an event, run automation logic, then drive a real-world device as an output surface.

## Install On The Pi

On the Raspberry Pi, clone the public repo and run the installer:

```bash
git clone https://github.com/synryzen/nodespark-wisp.git
cd nodespark-wisp
bash scripts/install_pi.sh
```

If you copied this folder manually instead of cloning it, run the same installer from inside the copied folder.

For a more detailed walkthrough, see [docs/INSTALL.md](docs/INSTALL.md).

Edit the Hub URL and workflow names:

```bash
sudo nano /etc/nodespark-wisp/config.toml
```

In NodeSparkHub on your Mac, start the Hub server and generate a pairing code. Then pair the Pi:

```bash
/opt/nodespark-wisp/scripts/pair_device.sh 123456
```

The pairing command immediately performs a device check-in. In NodeSparkHub, open:

```text
Settings -> Hub Server -> Devices
```

You should see `NodeSpark Wisp` with its device UUID. Use `Copy Device ID` there when a Wisp workflow template asks for a target device.

Start the service:

```bash
sudo systemctl enable --now nodespark-wisp
journalctl -u nodespark-wisp -f
```

## Startup Logo

The device uses the bundled NodeSpark mascot at startup. To swap in a different PNG later, copy it onto the Pi and set:

```toml
[display]
startup_logo_enabled = true
startup_logo_path = "/home/pi/my-logo.png"
```

Leave `startup_logo_path` blank to use the built-in mascot.

## Status, QR, And Updates

Print device status:

```bash
/opt/nodespark-wisp/.venv/bin/nodespark-wisp status
```

Show the pairing QR on the LCD:

```bash
/opt/nodespark-wisp/.venv/bin/nodespark-wisp qr
```

Refresh the installed package/dependencies and restart the service:

```bash
/opt/nodespark-wisp/scripts/update_pi.sh
```

If the Pi install is a Git checkout, the update command pulls first. If it was copied with `install_pi.sh`, re-copy the folder from your Mac and run `update_pi.sh`.

## Sales Demo Flow

1. Start NodeSparkHub on the Mac and start the Hub server.
2. Pair the Wisp device.
3. Show the device appearing in connected devices.
4. Run a workflow from the physical button.
5. Send a Hub-to-device command that changes the screen and speaks.
6. Open the Template Library and use one of the Wisp templates:
   - `Wisp Device Showcase`
   - `Wisp AI Voice Reply`
   - `Physical Approval Ping`
7. Explain the bigger idea: NodeSparkHub connects iPhone, Mac, bots, APIs, schedules, and physical devices into one workflow system.

## Whisplay Driver

The installer clones PiSugar's official Whisplay repo into `/opt/Whisplay`. If audio is not visible in `arecord -l` and `aplay -l`, install the WM8960 driver and reboot:

```bash
sudo bash /opt/Whisplay/Driver/install_wm8960_drive.sh
sudo reboot
```

## Voice Setup

The simplest first test is text-only:

```bash
/opt/nodespark-wisp/.venv/bin/nodespark-wisp run --workflow "Wisp Assistant" --text "hello from the pi"
```

For OpenAI transcription, set:

```toml
[audio]
transcription_provider = "openai"
openai_model = "gpt-4o-mini-transcribe"
```

Then export `OPENAI_API_KEY` for the service, or put it in the config file. For a product build, prefer the environment variable or a secret manager over keeping the key in the TOML file.

For offline transcription, install `vosk`, download a small model, and set:

```toml
[audio]
transcription_provider = "vosk"
vosk_model_path = "/opt/nodespark-wisp/models/vosk"
```

## Workflow Payload

Button-triggered runs send this payload to NodeSparkHub:

```json
{
  "source": "wisp",
  "deviceId": "...",
  "deviceName": "NodeSpark Wisp",
  "text": "transcribed voice command",
  "input": "transcribed voice command",
  "utterance": "transcribed voice command",
  "timestamp": "..."
}
```

In your workflow nodes, use `{{input.text}}`, `{{input}}`, or payload fields depending on the NodeSparkHub node.

## Development From Your Mac

You can run the non-hardware parts locally:

```bash
cd "NodeSpark Wisp"
cp config.example.toml config.toml
# edit config.toml to point at your Hub
bash scripts/run_dev.sh health
bash scripts/run_dev.sh workflows
bash scripts/run_dev.sh run --workflow "Wisp Assistant" --text "test"
```
