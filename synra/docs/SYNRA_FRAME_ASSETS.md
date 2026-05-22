# Synra Frame Assets

These assets are the current production frames for Synra before the final
Live2D Cubism model exists.

## Full Sheets

```text
synra/web/assets/synra/sheets/expression-sheet.png
synra/web/assets/synra/sheets/rigging-poses-sheet.png
```

## Runtime Expression Frames

```text
neutral
listening
thinking
speaking
happy
wink
concerned
curious
```

Runtime path:

```text
synra/web/assets/synra/expressions/
```

## Runtime Rig Pose Frames

```text
forward-neutral
look-up
look-down
look-left
look-right
blink-closed
half-blink
smile
mouth-a
mouth-ei
mouth-ou
concerned
```

Runtime path:

```text
synra/web/assets/synra/rig-poses/
```

## App Behavior

The monitor UI uses these frames as the immediate Synra avatar layer:

- `idle` uses neutral and look-direction frames.
- `listening` uses the listening expression.
- `thinking` and `workflow_running` use the thinking expression.
- `speaking` uses the speaking expression and mouth-shape frames.
- `success` uses happy/wink.
- `warning` and `error` use concerned.
- `approval_needed` uses curious.
- Blink frames are injected during idle animation.

The Live2D layer still has priority. When a real Cubism model is installed and
loads successfully, the frame avatar fades out and the Live2D canvas takes over.
