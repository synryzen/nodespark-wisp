# NodeSpark Synra M5Stack Core2 Voice Remote

This firmware turns an M5Stack Core2 into a voice-only Synra remote for
NodeSparkHub. The Core2 has no camera and does not provide text chat. It uses
the built-in microphone, speaker, touch screen, battery monitor, haptics, and
Wi-Fi to talk to the Hub the same way Synra does on other linked devices.

## What It Does

- Connects to Wi-Fi from an on-device setup portal.
- Pairs with NodeSparkHub using the Hub pairing code.
- Checks in as a connected Synra voice remote.
- Records short push-to-talk voice turns.
- Sends audio to the Hub transcription endpoint.
- Sends the transcript to `/synra/assistant`.
- Requests WAV speech from the Hub so Core2 can play the Hub-selected Synra
  voice, including ElevenLabs voices configured on the Hub.
- Lets the user choose between `Synra`, `Synra Modern`, and `Synra Battle`.
- Polls safe Hub device commands for display/speech/status updates.
- Refuses direct workflow execution from the device; workflow actions stay
  confirm-gated on NodeSparkHub.

## Hardware Needed

- M5Stack Core2 main unit
- USB-C cable
- Wi-Fi network reachable by the Mac running NodeSparkHub
- Optional microSD card

## Build

Install Arduino CLI, the ESP32 board package, and libraries:

```bash
arduino-cli core install esp32:esp32
arduino-cli lib install M5Unified ArduinoJson
```

Create the config file if needed:

```bash
cp firmware/m5stack-core2-synra/nodespark_synra_core2/config.example.h \
  firmware/m5stack-core2-synra/nodespark_synra_core2/config.h
```

Compile:

```bash
bash scripts/build_m5stack_core2.sh
```

Upload:

```bash
arduino-cli upload \
  -p /dev/cu.usbserial-XXXX \
  --fqbn esp32:esp32:m5stack-core2:PartitionScheme=default,PSRAM=enabled \
  firmware/m5stack-core2-synra/nodespark_synra_core2
```

## On-Device Setup

1. Open `Settings` on the Core2.
2. Tap `Wi-Fi / Hub Setup`.
3. Connect a phone or computer to the `NodeSpark-Core2-xxxx` access point.
4. Open `http://192.168.4.1`.
5. Select Wi-Fi SSID, enter Wi-Fi password, enter Hub URL, optionally enter a
   Hub pairing code, and choose a Synra character.
6. Save and restart.

The pairing code can also be entered directly on the Core2 `Pair` screen.

## Screens

- `Home`: Wi-Fi, Hub, pairing, selected character, and status.
- `AI`: push-to-talk Synra voice remote.
- `Pair`: numeric Hub pairing code entry.
- `Test`: mic level, speaker test, SD status.
- `Set`: Wi-Fi setup portal, volume, restart, character selector, pair shortcut.

## Safety

- The device never receives raw ElevenLabs API keys or other Hub credentials.
- ElevenLabs and other Synra voice credentials remain in the Hub Keychain.
- The device requests WAV output from the Hub because Core2 playback is PCM WAV.
- The device is least-privilege by default and does not run workflows directly.
- Any live workflow or app-control action must still pass through Hub
  confirmation gates.
