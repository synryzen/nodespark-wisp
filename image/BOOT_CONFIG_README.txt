NodeSpark Wisp boot configuration
=================================

Optional quick setup:

1. Copy nodespark-wisp.toml.example to nodespark-wisp.toml on this boot
   partition.
2. Edit hub.base_url to match the NodeSparkHub server URL.
3. Eject the card and boot the Raspberry Pi.

On boot, NodeSpark Wisp copies nodespark-wisp.toml into:

  /etc/nodespark-wisp/config.toml

You can also configure Wi-Fi, username/password, locale, and SSH from Raspberry
Pi Imager before writing the image.

