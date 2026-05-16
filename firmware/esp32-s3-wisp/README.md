# NodeSpark Wisp ESP32-S3 Touch Build

This firmware turns an ESP32-S3 N16R8 development board into a NodeSpark Wisp
device with a 2.8-inch ILI9341 touch screen, MAX98357 I2S speaker amp, and
INMP441 I2S microphone.

It uses the same NodeSparkHub device protocol as the Raspberry Pi Wisp:

- Pair with NodeSparkHub using a Hub pairing code.
- Check in as a connected device.
- Poll Hub device commands.
- Show display cards, dashboards, pings, approval prompts, and command status.
- Acknowledge completed, ignored, approved, rejected, or failed commands.
- Use the touchscreen for pairing, navigation, local demos, and approvals.
- Play I2S chimes through the MAX98357 amp.
- Show a live microphone level from the INMP441.
- Trigger a Hub workflow from the touchscreen.

## Required Parts

- ESP32-S3 Development Board N16R8 with USB-C
- ILI9341 2.8-inch SPI TFT LCD Display Touch Panel, 240x320
- MAX98357 I2S DAC Class D Amplifier Module
- Small speaker for the MAX98357 output
- INMP441 omnidirectional I2S MEMS microphone module
- Jumper wires and a breadboard or soldered prototype board
- A Mac running NodeSparkHub with Hub Server enabled

## Wiring

Use 3.3V logic for all signal pins. Power the ILI9341 module from `3V3` first.
Only use `5V` for the TFT VCC if your exact module requires it and its logic
pins are documented as 3.3V safe.

### ILI9341 TFT + XPT2046 Touch

The display and touch controller share SPI clock, MOSI, and MISO.

| Display Pin | ESP32-S3 Pin | Notes |
| --- | ---: | --- |
| VCC | 3V3 | Use 3.3V unless your module explicitly needs 5V power |
| GND | GND | Common ground |
| CS / TFT_CS | GPIO10 | Display chip select |
| RESET / RST | GPIO8 | Display reset |
| DC / RS | GPIO9 | Display data/command |
| SDI / MOSI | GPIO11 | SPI MOSI |
| SCK / CLK | GPIO12 | SPI clock |
| SDO / MISO | GPIO13 | SPI MISO |
| LED / BL | 3V3 | Backlight always on; add PWM transistor later if desired |
| T_CLK | GPIO12 | Touch SPI clock |
| T_DIN | GPIO11 | Touch MOSI |
| T_DO | GPIO13 | Touch MISO |
| T_CS | GPIO7 | Touch chip select |
| T_IRQ | GPIO6 | Touch interrupt |

### MAX98357 I2S Amplifier

| MAX98357 Pin | ESP32-S3 Pin | Notes |
| --- | ---: | --- |
| VIN | 5V or 3V3 | 5V gives louder speaker output |
| GND | GND | Common ground |
| BCLK | GPIO4 | I2S bit clock |
| LRC / LRCLK | GPIO5 | I2S word select |
| DIN | GPIO16 | I2S data from ESP32 |
| SD | 3V3 | Optional enable; can be left enabled |
| GAIN | Floating | Default gain |

### INMP441 I2S Microphone

| INMP441 Pin | ESP32-S3 Pin | Notes |
| --- | ---: | --- |
| VDD | 3V3 | Do not power from 5V |
| GND | GND | Common ground |
| SCK / BCLK | GPIO15 | I2S bit clock |
| WS / LRCL | GPIO17 | I2S word select |
| SD | GPIO18 | I2S data to ESP32 |
| L/R | GND | Select left channel |

## Build And Upload

1. Install Arduino IDE or use `arduino-cli`.
2. Install the ESP32 board package.
3. Install these libraries:
   - `TFT_eSPI`
   - `XPT2046_Touchscreen`
   - `ArduinoJson`
4. Copy `nodespark_wisp_esp32/config.example.h` to
   `nodespark_wisp_esp32/config.h`.
5. Edit `config.h` with your Wi-Fi SSID, password, and NodeSparkHub URL.
6. Compile for `ESP32S3 Dev Module`.
7. Upload over USB-C.

Example with `arduino-cli`:

```bash
arduino-cli compile \
  --fqbn esp32:esp32:esp32s3:FlashSize=16M,PSRAM=opi \
  firmware/esp32-s3-wisp/nodespark_wisp_esp32

arduino-cli upload \
  -p /dev/cu.usbmodemXXXX \
  --fqbn esp32:esp32:esp32s3:FlashSize=16M,PSRAM=opi \
  firmware/esp32-s3-wisp/nodespark_wisp_esp32
```

## Pairing

1. Start NodeSparkHub on the Mac.
2. Start Hub Server.
3. Open `Settings -> Hub Server -> Devices`.
4. Generate a pairing code.
5. On the ESP32 screen, open `Pair`.
6. Enter the code on the touchscreen keypad.
7. Tap `Pair`.

After pairing, the ESP32-S3 Wisp appears in NodeSparkHub's connected device
list as `ESP32-S3 / NodeSpark Wisp Touch`.

## Touch UI

- `Status`: Wi-Fi, Hub, device ID, pairing status.
- `Pair`: touchscreen pairing-code keypad.
- `Cmds`: last command and approval actions.
- `Demo`: local demo buttons and workflow trigger.
- `Mic`: INMP441 level test and voice workflow placeholder.

## Current Limitations

- The MAX98357 path currently plays chimes. Full text-to-speech needs either a
  Hub audio endpoint, an onboard speech synthesis library, or iPhone bridge
  forwarding.
- The INMP441 path currently shows live level and can trigger a workflow event.
  Raw audio upload/transcription should be added after NodeSparkHub exposes a
  small-device audio upload endpoint.
- Touch calibration values may need adjustment for your exact 2.8-inch module.

