# NodeSpark Wisp M5Stack Core2 Build

This firmware turns an M5Stack Core2 into a premium NodeSpark Wisp device. It
uses the Core2 built-in 2-inch capacitive touch screen, virtual front buttons,
speaker, microphone, vibration motor, battery monitor, IMU, RTC, and microSD
slot.

The Core2 build uses the same NodeSparkHub device protocol as the Raspberry Pi
Whisplay and ESP32-S3 builds:

- Pair with NodeSparkHub using a pairing code.
- Check in as a connected device.
- Poll Hub commands.
- Acknowledge command results.
- Run a selected Hub workflow.
- Ask NodeSparkHub's Wisp Assistant endpoint, with workflow fallback.

## Hardware Needed

- M5Stack Core2 main unit
- USB-C cable
- microSD card, optional, up to 16 GB recommended by M5Stack
- Wi-Fi network reachable by the Mac running NodeSparkHub

No display, touch, speaker, mic, battery, or amplifier wiring is required. Those
parts are built into the Core2.

## Build

Install Arduino CLI, the ESP32 board package, and libraries:

```bash
arduino-cli core install esp32:esp32
arduino-cli lib install M5Unified ArduinoJson
```

Create the config file:

```bash
cp firmware/m5stack-core2-wisp/nodespark_wisp_core2/config.example.h \
  firmware/m5stack-core2-wisp/nodespark_wisp_core2/config.h
```

Edit `config.h` with your Wi-Fi name, Wi-Fi password, and NodeSparkHub URL.
Then compile:

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

If your Core2 appears as `/dev/cu.wchusbserial...`, use that port instead.

## On-Device Controls

- Bottom left virtual button: previous screen.
- Bottom middle virtual button: main action on the current screen.
- Bottom right virtual button: next screen.
- Touch the bottom navigation tabs to jump between screens.

Screens:

- `Status`: Wi-Fi, Hub health, pairing, battery, IP, and device ID.
- `Pair`: enter the one-time NodeSparkHub pairing code.
- `Hub`: run a workflow, ask AI, ping Hub, and view command status.
- `Sensors`: microphone level, IMU tilt, haptic test, and SD card status.
- `Setup`: volume, Wi-Fi reconnect, token reset, and saved workflow.

## Pair With NodeSparkHub

1. Start NodeSparkHub and turn on the Hub Server.
2. Open `Settings -> Hub Server -> Devices`.
3. Generate a pairing code.
4. On the Core2, open `Pair`.
5. Enter the code and tap `Pair`.
6. The Core2 should appear as `M5Stack Core2 / NodeSpark Wisp`.

## Current Capabilities

- Branded NodeSpark mascot startup screen.
- Touch-first Core2 UI with five screens.
- Hub pairing, check-ins, command polling, and command acknowledgements.
- Rich display commands, notifications, dashboards, QR/link text, and approvals.
- Workflow launch from the Core2.
- Ask AI through NodeSparkHub.
- Speaker chimes with volume control.
- Microphone level visualization.
- Battery and charging display.
- IMU tilt readout.
- Vibration/haptic feedback.
- microSD card check and local log file.

## Notes

M5Unified shares audio resources between the internal speaker and microphone.
The firmware briefly stops the microphone before playing a chime, then restarts
mic capture afterward. This keeps audio more stable on Core2 than trying to use
speaker and mic at the same instant.

## References

- M5Stack Core2 documentation: https://docs.m5stack.com/en/core/Core2
- M5Unified library: https://github.com/m5stack/M5Unified
