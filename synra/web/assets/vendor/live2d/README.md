# Live2D Browser Runtime Vendor Files

Synra's web UI can load a real Live2D Cubism model when these browser runtime
files are present:

```text
live2dcubismcore.min.js
pixi.min.js
pixi-live2d-display.min.js
```

`pixi.min.js` and `pixi-live2d-display.min.js` are normal browser libraries.
`live2dcubismcore.min.js` comes from the Live2D Cubism SDK for Web and is
covered by Live2D's SDK license. Keep that file local so the Jetson kiosk can
run without relying on a CDN.

Install them with:

```bash
cd synra
bash scripts/install_live2d_runtime_vendor.sh
```

The script downloads Cubism Core from Live2D's official web SDK endpoint and
downloads the PixiJS runtime files from jsDelivr/npm.
