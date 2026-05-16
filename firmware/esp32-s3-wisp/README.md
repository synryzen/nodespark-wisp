# NodeSpark Wisp ESP32-S3 Touch Build

This firmware turns an ESP32-S3 N16R8 development board into a NodeSpark Wisp
device with a 2.8-inch ILI9341 touch screen, MAX98357 I2S speaker amp, and
INMP441 I2S microphone.

It uses the same NodeSparkHub device protocol as the Raspberry Pi Wisp:

- Pair with NodeSparkHub using a Hub pairing code.
- Check in as a connected device.
- Poll Hub device commands.
- Show display cards, dashboards, notifications, QR/link screens, icon grids,
  pings, approval prompts, health checks, startup logo screens, and command
  status.
- Acknowledge completed, ignored, approved, rejected, or failed commands.
- Use the touchscreen for pairing, navigation, local demos, and approvals.
- Scan for Wi-Fi, enter the Wi-Fi password, and edit the Hub URL plus optional
  port on the touchscreen.
- Use `Ask AI` to send a prompt into NodeSparkHub's selected AI profile through
  the `Wisp Assistant` endpoint, with workflow fallback when direct AI is not
  configured.
- Select and save the active Hub workflow from NodeSparkHub commands.
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

For a visual wiring diagram, see [WIRING_DIAGRAM.md](WIRING_DIAGRAM.md).

### ILI9341 TFT + XPT2046 Touch

The display and touch controller share SPI clock, MOSI, and MISO.

| Display Pin | ESP32-S3 Pin | Notes |
| --- | ---: | --- |
| VCC | 3V3 | Use 3.3V unless your module explicitly needs 5V power |
| GND | GND | Common ground |
| CS / TFT_CS | GPIO10 | Display chip select |
| RESET / RST | 3V3 | Keep reset high during first bring-up |
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
   - `Adafruit GFX Library`
   - `Adafruit ILI9341`
   - `XPT2046_Touchscreen`
   - `ArduinoJson`
4. Optional: copy `nodespark_wisp_esp32/config.example.h` to
   `nodespark_wisp_esp32/config.h` to prefill Wi-Fi and Hub defaults.
   You can also configure Wi-Fi, Hub URL, and optional port directly from the Wisp
   touchscreen after flashing.
5. If you use `config.h`, edit it with your Wi-Fi SSID, password, and
   NodeSparkHub URL.
6. Compile for `ESP32S3 Dev Module` using a 16 MB flash / 3 MB app partition.
7. Upload over USB-C.

Example with `arduino-cli`:

```bash
arduino-cli compile \
  --fqbn esp32:esp32:esp32s3:FlashSize=16M,PSRAM=opi,PartitionScheme=app3M_fat9M_16MB \
  firmware/esp32-s3-wisp/nodespark_wisp_esp32

arduino-cli upload \
  -p /dev/cu.usbmodemXXXX \
  --fqbn esp32:esp32:esp32s3:FlashSize=16M,PSRAM=opi,PartitionScheme=app3M_fat9M_16MB \
  firmware/esp32-s3-wisp/nodespark_wisp_esp32
```

## Pairing

1. Open `Set` on the Wisp touchscreen.
2. Tap `Scan`, choose your Wi-Fi network, enter the password, then tap `Save`.
3. Tap the URL field and enter the NodeSparkHub server base URL.
   - For local Hub Server access, use something like `http://192.168.1.57`.
   - For Cloudflare or another remote tunnel/domain, use something like
     `https://your-domain.com`.
4. Tap the port field only when the URL needs an explicit port. Local Hub Server
   usually uses `8787`; Cloudflare HTTPS domains usually leave this field empty.
5. Tap `Connect`.
6. Start NodeSparkHub on the Mac.
7. Start Hub Server.
8. Open `Settings -> Hub Server -> Devices`.
9. Generate a pairing code.
10. On the ESP32 screen, open `Pair`.
11. Enter the code on the touchscreen keypad.
12. Tap `Pair`.

After pairing, the ESP32-S3 Wisp appears in NodeSparkHub's connected device
list as `ESP32-S3 / NodeSpark Wisp Touch`.

## Touch UI

- `Status`: Wi-Fi, Hub, device ID, pairing status.
- `Pair`: touchscreen pairing-code keypad.
- `Cmds`: last command and approval actions.
- `Demo`: ping, `Ask AI`, workflow trigger, and chime demo.
- `Mic`: INMP441 level test and voice workflow placeholder.
- `Set`: Wi-Fi scan, SSID/password entry, Hub URL, optional Hub port, Save, and Connect.

## Bluetooth Mobile Bridge

The ESP32-S3 firmware includes an experimental BLE bridge implementation using
the same GATT service as the Raspberry Pi Wisp, but it is disabled by default
because the Arduino-ESP32 BLE stack can reboot some ESP32-S3 boards when it is
combined with Wi-Fi, HTTPS, I2S, and TFT UI in one sketch.

For reliable demos today, use Bluetooth Mobile Bridge on the Raspberry Pi
Wisp. To experiment on ESP32-S3, set `WISP_ENABLE_BLE` to `1` in `config.h`
and build with the 16 MB / 3 MB app partition.

- Service: `4E530001-4E53-5749-5350-000000000001`
- Command characteristic: `4E530002-4E53-5749-5350-000000000001`
- Event characteristic: `4E530003-4E53-5749-5350-000000000001`
- State characteristic: `4E530004-4E53-5749-5350-000000000001`

When enabled, open NodeSpark on iPhone, go to `Settings -> Hub Pairing &
Control -> Wisp Mobile Bridge`, scan for `NodeSpark Wisp`, and connect. The
iPhone can send compact JSON commands such as `ping`, `card`, and `dashboard`
over BLE. The firmware processes BLE commands from the main loop so Bluetooth
writes do not interrupt display or network work.

## Current Limitations

- ESP32-S3 BLE bridge is experimental and disabled by default for stability.
  Use the Raspberry Pi Wisp for production Bluetooth Mobile Bridge demos until
  the ESP32 build moves to a lighter BLE stack.
- The MAX98357 path currently plays chimes. Full text-to-speech needs either a
  Hub audio endpoint, an onboard speech synthesis library, or iPhone bridge
  forwarding.
- The INMP441 path currently shows live level and can trigger a workflow event.
  Raw audio upload/transcription should be added after NodeSparkHub exposes a
  small-device audio upload endpoint.
- Touch calibration values may need adjustment for your exact 2.8-inch module.
