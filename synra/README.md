# NodeSpark Synra

NodeSpark Synra is the Jetson-powered monitor companion for NodeSparkHub.
It turns Hub workflows into a living on-screen AI presence: an adult original
anime-style assistant who listens, speaks, reacts, runs workflows, and shows
approvals, alerts, and workflow state on a dedicated display.

This project is the first runnable prototype. It intentionally starts with the
same device pattern as NodeSpark Wisp: pair with NodeSparkHub, check in, poll
commands, acknowledge results, and expose capabilities. Synra adds an avatar
state machine and a full-screen browser UI that can run on the Jetson's HDMI
display.

## First Prototype

Current capabilities:

- Full-screen Synra monitor UI.
- Local control API for avatar state, expression, message, and workflow cards.
- Wisp-compatible Hub client for pairing, check-in, command polling, workflow
  runs, assistant calls, and command acknowledgements.
- Command handlers for `speak`, `setExpression`, `setState`, `showCard`,
  `approval`, `assistant`, and `runWorkflow`.
- Browser speech output when Synra receives a `speak` command.
- Browser microphone loop for push-to-talk requests through NodeSparkHub
  Assistant.
- Live2D-ready browser stage for a real Cubism character model, with PNG
  fallback while the rigged Synra model pack is being authored.
- Jetson-friendly Python daemon with no browser framework dependency.

## Install On Existing Jetson OS

Synra is an app for the normal Jetson Ubuntu desktop. It does not replace the
operating system, reflash the board, or take over the machine.

On the Jetson:

```bash
cd nodespark-wisp/synra
bash scripts/install_jetson_app.sh
```

Then edit:

```text
/etc/nodespark-synra/config.toml
```

Set `hub.base_url` to the Mac or server running NodeSparkHub. The daemon runs as
a user systemd service:

```bash
systemctl --user status nodespark-synra
systemctl --user restart nodespark-synra
```

Open the monitor UI:

```text
http://127.0.0.1:8788
```

To launch Synra full-screen whenever the Jetson desktop user logs in:

```bash
bash /opt/nodespark-synra/scripts/install_desktop_autostart.sh
```

That installs a normal desktop autostart entry for Chromium/Chrome kiosk mode.

## Run Locally

```bash
cd synra
python3 -m venv .venv
. .venv/bin/activate
pip install -e .
cp config.example.toml config.toml
nodespark-synra --config config.toml
```

Open:

```text
http://localhost:8788
```

On the Jetson, run Chromium in kiosk mode against the same URL.

## Local Control API

Set avatar state:

```bash
curl -X POST http://localhost:8788/api/state \
  -H "Content-Type: application/json" \
  -d '{"mode":"thinking","expression":"focused","message":"Building your workflow map..."}'
```

Make Synra speak visually:

```bash
curl -X POST http://localhost:8788/api/command \
  -H "Content-Type: application/json" \
  -d '{"type":"speak","text":"NodeSparkHub is online. I am ready."}'
```

Show an approval:

```bash
curl -X POST http://localhost:8788/api/command \
  -H "Content-Type: application/json" \
  -d '{"type":"approval","title":"Approval Needed","text":"Run the client follow-up workflow?"}'
```

Ask from the monitor microphone:

```text
Open http://127.0.0.1:8788
Click Talk
Allow microphone access in Chromium
Speak naturally
```

The browser captures a transcript, sends it to the local Synra daemon, the
daemon forwards it to NodeSparkHub's `/wisp/assistant` flow, and Synra speaks
the returned reply.

## Live2D Character Model

Synra's final visual layer is Live2D Cubism, not a moving PNG. The web app looks
for a rigged model pack here:

```text
synra/web/assets/live2d/synra/synra.model3.json
```

Check whether the runtime and model are present:

```bash
cd synra
bash scripts/check_live2d_assets.sh
```

Install the local browser runtime files:

```bash
cd synra
bash scripts/install_live2d_runtime_vendor.sh
```

Install a finished model delivery zip or folder:

```bash
cd synra
bash scripts/install_live2d_model_pack.sh /path/to/nodespark-synra-live2d-delivery.zip
```

Install an optional official Live2D sample model for renderer smoke testing:

```bash
cd synra
bash scripts/install_live2d_smoke_test_model.sh
```

Check runtime/model status from the running daemon:

```bash
curl http://localhost:8788/api/live2d
```

See `docs/LIVE2D_PIPELINE.md` for the layer list, expression names, motion
groups, and export structure.
See `docs/LIVE2D_LOCAL_SETUP.md` for the local Mac and Jetson setup status.
See `docs/SYNRA_FRAME_ASSETS.md` for the current AI-drawn expression and pose
frames and generated video loops used by the monitor UI.

The production handoff package for artists and riggers is in:

```text
synra/live2d-production/
```

Build a sendable package:

```bash
cd synra
bash scripts/build_live2d_commission_pack.sh
```

## Design North Star

Synra should feel like the face and soul of NodeSparkHub, not a generic chatbot.
She is smart first, visually memorable second, and useful always.

Target states:

```text
idle
listening
thinking
speaking
workflow_running
success
warning
error
approval_needed
sleep
```
