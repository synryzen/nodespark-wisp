# Synra Live2D Model Pack

Drop the exported NodeSpark Synra Cubism runtime files in this folder.

Required entry point:

```text
synra.model3.json
```

Expected pack layout:

```text
synra.model3.json
synra.moc3
textures/
  texture_00.png
  texture_01.png
motions/
  idle.motion3.json
  listen.motion3.json
  think.motion3.json
  talk.motion3.json
  success.motion3.json
  concerned.motion3.json
  approval.motion3.json
expressions/
  neutral.exp3.json
  happy.exp3.json
  attentive.exp3.json
  focused.exp3.json
  curious.exp3.json
  concerned.exp3.json
  wink.exp3.json
physics3.json
pose3.json
```

The browser UI looks for `/assets/live2d/synra/synra.model3.json`. When it is
present and the runtime vendor files are installed, Synra switches from the
temporary PNG fallback to the real Live2D character automatically.
