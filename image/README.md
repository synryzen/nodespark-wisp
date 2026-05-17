# NodeSpark Wisp Raspberry Pi OS Image

This folder contains the build recipe for a downloadable NodeSpark Wisp factory
image. The default image is based on Raspberry Pi OS Lite 32-bit and is intended for
Raspberry Pi Zero 2 W plus the PiSugar Whisplay HAT.

## What the image includes

- NodeSpark Wisp installed at `/opt/nodespark-wisp`
- A dedicated `nodespark` service user
- `nodespark-wisp.service` enabled on boot
- Whisplay display, button, speaker, microphone, QR, workflow, and Hub command
  software
- Python runtime dependencies installed in `/opt/nodespark-wisp/.venv`
- SPI and I2C enabled in the Raspberry Pi boot config
- A first-boot helper that can copy `nodespark-wisp.toml` from the boot
  partition into `/etc/nodespark-wisp/config.toml`

The image does not ship with a default login password. Users should set the
username, password, Wi-Fi, locale, and SSH options in Raspberry Pi Imager before
writing the SD card.

## Build locally

The builder uses the official `pi-gen` flow. Raspberry Pi documents `pi-gen` as
the tool used to create Raspberry Pi OS images and supports custom stages. It
must run on Linux or through Docker with privileged access.

```bash
bash scripts/build_pi_image.sh
```

The output appears under:

```text
~/.cache/nodespark-wisp/pi-gen/deploy/
```

Useful overrides:

```bash
WISP_PI_GEN_BRANCH=arm64 bash scripts/build_pi_image.sh   # optional 64-bit local build
WISP_IMAGE_WORKDIR=/fast/linux/disk/nodespark-wisp-image bash scripts/build_pi_image.sh
```

## Build on GitHub

Use the manual **Build Raspberry Pi Image** GitHub Actions workflow. It creates
an `.img.xz` artifact when the build succeeds.

The build can take a long time and may be too large for routine CI, so it only
runs from `workflow_dispatch`.

## User setup flow

1. Download the latest NodeSpark Wisp `.img.xz` image.
2. Open Raspberry Pi Imager.
3. Select **Use custom** and choose the Wisp image.
4. In Imager settings, set Wi-Fi, username/password, locale, and optional SSH.
5. Flash the microSD card.
6. Optional: mount the boot partition and copy `nodespark-wisp.toml.example` to
   `nodespark-wisp.toml`, then edit the Hub URL.
7. Boot the Raspberry Pi with the Whisplay HAT attached.
8. Open NodeSparkHub, start Hub Server, generate a pairing code, then pair the
   Wisp.

If no boot config is supplied, the device still boots into Wisp mode and shows
the startup/pairing UI. Configure `/etc/nodespark-wisp/config.toml` over SSH
after the first boot.
