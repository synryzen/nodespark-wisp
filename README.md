# NodeSpark Wisp

NodeSpark Wisp is a pocket-sized Raspberry Pi companion device for
NodeSparkHub. It gives NodeSparkHub a real physical presence: a tiny display,
speaker, microphone, RGB light, and hardware button that can run workflows,
show live workflow results, speak responses, ask for approvals, and display
branded dashboard screens.

It is built to demonstrate what NodeSparkHub can do beyond an iPhone. With
Wisp, NodeSparkHub can connect software automation to something people can hold
in their hand.

Repository:

```text
https://github.com/synryzen/nodespark-wisp
```

## What It Is For

NodeSpark Wisp is for showing, testing, and building physical workflow
experiences powered by NodeSparkHub.

Use it to:

- Demo NodeSparkHub at a desk, booth, store, classroom, or client meeting.
- Trigger Hub workflows from a real button.
- Send live workflow output from NodeSparkHub to a Raspberry Pi display.
- Speak Hub responses through the device speaker.
- Capture short voice commands and send them into Hub workflows.
- Show branded cards, metrics, alerts, approvals, QR codes, and status screens.
- Prove that NodeSparkHub can connect Mac, iPhone, APIs, schedules, automations,
  and physical hardware into one workflow system.

The big idea: NodeSparkHub is not just an app screen. It can become a control
center for real-world devices.

## How It Works With NodeSparkHub

NodeSpark Wisp connects to the Hub server built into NodeSparkHub over HTTP.
After pairing, it checks in with the Hub, appears in Hub Server device settings,
and polls for commands from NodeSparkHub.

The device uses these Hub API flows:

- `POST /pair`
- `POST /devices/checkin`
- `GET /workflows`
- `POST /workflows/<name>/run`
- `GET /runs/<id>/status`
- `POST /devices/<deviceId>/commands`
- `GET /devices/<deviceId>/commands/poll`
- `POST /devices/<deviceId>/commands/<commandId>/ack`

NodeSparkHub can send commands to the Wisp from workflow templates, local demo
scripts, or direct API calls. The Wisp acknowledges each command so Hub can know
whether it displayed, spoke, ran, approved, rejected, or failed the request.

## What The Device Can Do

Current Wisp capabilities:

- Pair with NodeSparkHub using a one-time pairing code.
- Appear inside NodeSparkHub's connected device list.
- Check in automatically so Hub can show connection status.
- Show the NodeSpark mascot as a startup logo.
- Show animated listening, thinking, running, success, and error screens.
- Show text messages from NodeSparkHub.
- Show rich branded cards with icons, subtitles, accent colors, and progress.
- Show approval prompts where short press approves and hold rejects.
- Show a notification center with recent Hub alerts.
- Show compact dashboards with metrics and list items.
- Show icon/graphics grids for visual demos.
- Show QR codes for pairing, identity, links, or workflow data.
- Speak text from Hub through the onboard speaker when `espeak-ng` is installed.
- Play startup, listening, success, and error chimes when audio is available.
- Set the RGB LED color from Hub commands.
- Run a selected Hub workflow from the physical button.
- Cycle favorite workflows with a short button press.
- Record a short voice command with a button hold.
- Transcribe with OpenAI, offline Vosk, or skip transcription for text-only use.
- Send workflow payloads back to Hub with device identity and transcript text.
- Run a full sales-showcase sequence from one Hub command.
- Print local status including IP/Wi-Fi/battery/temperature when available.
- Optionally advertise a Bluetooth LE Wisp Mobile Bridge for NodeSpark on iPhone.

## Hardware Needed

This software targets the same hardware stack used by OpenClaw-style DIY
assistants:

- Raspberry Pi Zero 2 W
- MicroSD card, 16 GB or larger recommended
- PiSugar Whisplay HAT with:
  - 1.69-inch LCD
  - WM8960 speaker/microphone audio
  - RGB LED
  - onboard button
- PiSugar battery pack compatible with the Whisplay HAT
- Small speaker connected through the Whisplay/WM8960 audio path
- USB power cable for charging/setup
- Wi-Fi network reachable by the Mac running NodeSparkHub
- Optional case/enclosure for demo-ready builds

Recommended software/runtime:

- Raspberry Pi OS Lite or Desktop, 64-bit recommended
- Python 3.11 or newer
- NodeSparkHub 3 or newer running on the Mac
- Hub Server enabled in NodeSparkHub
- Optional: OpenAI API key for cloud transcription
- Optional: Vosk model for offline speech transcription

Important: this project uses the same physical hardware style as OpenClaw, but
it is NodeSparkHub companion software. OpenClaw is not required.

## Quick Install

On the Raspberry Pi:

```bash
git clone https://github.com/synryzen/nodespark-wisp.git
cd nodespark-wisp
bash scripts/install_pi.sh
```

Edit the Hub URL and favorite workflows:

```bash
sudo nano /etc/nodespark-wisp/config.toml
```

In NodeSparkHub on your Mac:

1. Start NodeSparkHub.
2. Start the Hub server.
3. Open `Settings -> Hub Server -> Devices`.
4. Generate a pairing code.

Then pair the Pi:

```bash
/opt/nodespark-wisp/scripts/pair_device.sh 123456
```

Start the background service:

```bash
sudo systemctl enable --now nodespark-wisp
journalctl -u nodespark-wisp -f
```

For the full setup guide, see [docs/INSTALL.md](docs/INSTALL.md).

## First Demo

After the Wisp appears in NodeSparkHub's device list, copy its device ID and run
these from the Mac or from a terminal that can reach the Hub:

```bash
curl http://127.0.0.1:8787/devices
```

Show a display message:

```bash
bash scripts/send_demo_command.sh <device-id> display "A Stripe order arrived. NodeSparkHub routed it to this device."
```

Make the device speak:

```bash
bash scripts/send_demo_command.sh <device-id> speak "NodeSparkHub just controlled a Raspberry Pi display from a workflow."
```

Show a rich card:

```bash
bash scripts/send_demo_command.sh <device-id> card "The workflow finished and the physical device updated instantly."
```

Run the full showcase:

```bash
bash scripts/send_demo_command.sh <device-id> demo "Physical workflows are live."
```

That is the moment people understand it: NodeSparkHub can receive an event, run
automation logic, then command a real device as an output surface.

## Hub-To-Device Commands

Supported Wisp command types:

- `display`: show a title/body text screen.
- `card`: show a branded rich card with style, icon, subtitle, footer, and progress.
- `approval`: show an approve/reject prompt; short press approves, hold rejects.
- `notification` or `notify`: add an alert to the device notification stack.
- `dashboard`: show a compact metric dashboard with list items.
- `graphics` or `icons`: show a visual icon grid.
- `speak`: speak text through the Wisp speaker.
- `led`: set the RGB LED color.
- `ping`: show a live connectivity ping.
- `logo`, `splash`, or `startup`: show the branded NodeSpark mascot screen.
- `qr`: show custom QR data, or the pairing/device-identity QR screen.
- `demo`: run the sales-showcase sequence.
- `runWorkflow`: ask the device to start another Hub workflow.
- `selectWorkflow`: change the selected favorite workflow.

Raw rich-card example:

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

## Voice And Button Controls

Default button behavior:

- Short press: cycle through favorite Hub workflows.
- Hold: record a short voice command and run the selected workflow.
- During approval: short press approves, hold rejects.

The simplest first voice test is text-only:

```bash
/opt/nodespark-wisp/.venv/bin/nodespark-wisp run --workflow "Wisp Assistant" --text "hello from the pi"
```

For OpenAI transcription, set:

```toml
[audio]
transcription_provider = "openai"
openai_model = "gpt-4o-mini-transcribe"
```

Then export `OPENAI_API_KEY` for the service, or put it in the config file.
For a product build, prefer an environment variable or secret manager over
storing the key directly in TOML.

For offline transcription, install `vosk`, download a small model, and set:

```toml
[audio]
transcription_provider = "vosk"
vosk_model_path = "/opt/nodespark-wisp/models/vosk"
```

## Wisp Mobile Bridge

Wisp Mobile Bridge is an optional Bluetooth LE mode for travel demos and
on-the-go control. It lets NodeSpark on iPhone connect directly to the Wisp,
send display/speech/dashboard commands, and forward Wisp events into
NodeSparkHub when the iPhone has a Hub connection.

The standard Wi-Fi/Hub connection remains the best full-time setup. Bluetooth
bridge mode is for mobile use when the Wisp is near the iPhone.

Enable it on the Pi:

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

Restart:

```bash
sudo systemctl restart nodespark-wisp
```

In NodeSpark on iPhone, open:

```text
Settings -> Hub Pairing & Control -> Wisp Mobile Bridge
```

Then scan, connect, and try Ping or Demo Card.

BLE protocol:

- Service: `4E530001-4E53-5749-5350-000000000001`
- Command characteristic, write: `4E530002-4E53-5749-5350-000000000001`
- Event characteristic, notify/read: `4E530003-4E53-5749-5350-000000000001`
- State characteristic, notify/read: `4E530004-4E53-5749-5350-000000000001`

Commands are compact JSON objects using the same command shapes as the Hub
device command channel, such as `display`, `card`, `dashboard`, `speak`, `led`,
`ping`, and `demo`.

## Startup Logo

The device uses the bundled NodeSpark mascot at startup. To swap in a different
PNG later, copy it onto the Pi and set:

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

If the Pi install is a Git checkout, the update command pulls first. If it was
copied with `install_pi.sh`, re-copy the folder from your Mac and run
`update_pi.sh`.

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
7. Explain the bigger idea: NodeSparkHub connects iPhone, Mac, bots, APIs,
   schedules, and physical devices into one workflow system.

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

In your workflow nodes, use `{{input.text}}`, `{{input}}`, or payload fields
depending on the NodeSparkHub node.

## Critical Notes

- NodeSparkHub must be running and reachable on the same network unless you
  expose the Hub another way.
- Pairing creates the device token used for Hub check-ins and command polling.
- Treat device tokens like credentials.
- Keep the Hub server URL updated if the Mac IP address changes.
- The Whisplay/WM8960 audio driver may require a reboot after installation.
- Voice features are optional. Display, button, workflow, QR, and command
  features work without cloud transcription.
- The legacy `nodespark-whisplay` command is still available as a compatibility
  alias, but new installs should use `nodespark-wisp`.

## Whisplay Driver

The installer clones PiSugar's official Whisplay repo into `/opt/Whisplay`. If
audio is not visible in `arecord -l` and `aplay -l`, install the WM8960 driver
and reboot:

```bash
sudo bash /opt/Whisplay/Driver/install_wm8960_drive.sh
sudo reboot
```

## Development From Your Mac

You can run the non-hardware parts locally:

```bash
cp config.example.toml config.toml
# edit config.toml to point at your Hub
bash scripts/run_dev.sh health
bash scripts/run_dev.sh workflows
bash scripts/run_dev.sh run --workflow "Wisp Assistant" --text "test"
```

## License

MIT License. See [LICENSE](LICENSE).
