# halo for Windows

A native Windows companion app for the Teenage Engineering **EP-40 Riddim**: a to-scale 3D model of the device that reacts live to what the hardware sends over USB. It's a port of the macOS halo app (Swift, `00StillShining/halo`) to C++ with [JUCE](https://juce.com).

## Status

| Area | State |
|---|---|
| Live PLAY view: 3D EP-40, pad travel, velocity LEDs, replica LCD, halo ring | Working, tested with a real EP-40 on Windows 11 |
| MIDI: auto-detects the EP-40, follows plug and unplug, reports a port held by another app | Working |
| Status bar and rail: honest connection, feed, clock and transport state | Working |
| LOAD, EDIT, CAPTURE, RACK, BACKUPS | Not ported yet; each shows a "not ported yet" plate |
| Audio monitor, recorder, dub rack, GRAB, CHOP | Not ported yet (see `docs/plan.md`) |
| Sending samples to the EP-40 | Not possible yet anywhere: needs TE's private protocol (plan §5) |

## Build

You need **Visual Studio 2022 Build Tools** with the "Desktop development with C++" workload. CMake and Ninja come with it. The first build downloads JUCE 8.0.15 and doctest.

```bat
scripts\build.cmd build     :: Debug build
scripts\build.cmd test      :: build and run the unit tests
scripts\build.cmd run       :: build and launch the app
scripts\build.cmd build Release
```

The app is written to `build\<Config>\Halo_artefacts\<Config>\Halo.exe`.

### Running without the EP-40

Set `HALO_MOCK_MIDI=1`, or pass `--mock-midi`. A synthetic EP-40 then plays a pad pattern with a 120 BPM clock. The status bar always shows `SOURCE MOCK` in this mode.

Debug builds also read `HALO_RAIL=open` and `HALO_MODE=1..6`, so screenshots can be taken of each state. `tools\dev\screenshot.ps1` captures just the Halo window.

For the 3D stage, `HALO_STAGE_CAMERA=yaw,pitch,radius` (degrees, degrees, metres; the default is `28,42,0.7`) fixes the camera for before/after comparisons, and `HALO_STAGE_DEBUG=1..4` shows the lighting terms: ambient occlusion, key-light shadow, height-map contours, and the floor shadow's components.

## Layout

```
Source/Core     plain C++20, no JUCE: MIDI mapping, display reducer, pad holds,
                ring and lifecycle state. Ported from the Mac's pure-Swift logic.
Source/Device   EP-40 MIDI connection (JUCE / WinMM), lock-free hand-off to the UI
Source/Stage    OpenGL 3.2 renderer for the EP-40 model and its LCD texture
Source/UI       design tokens (palette A), look-and-feel, status bar, mode bar, rail
Source/App      app model, window, entry point
Tests           doctest unit tests (ported from the Swift XCTest suites)
assets          EP40.hmesh (baked model) and the LCD indicator atlas
tools/model     usdz_to_hmesh.py: bakes the Mac app's EP40.usdz for Windows
docs/plan.md    evaluation of the Mac app and the porting plan
```

### Regenerating the 3D model

The model comes from the Mac repo's Blender-authored `Halo/Resources/EP40.usdz`:

```bat
python -m venv tools\.venv
tools\.venv\Scripts\pip install usd-core
tools\.venv\Scripts\python tools\model\usdz_to_hmesh.py path\to\EP40.usdz assets\EP40.hmesh
```

The bake is more than a format change. It replaces Blender's flat-shaded bevels with area-weighted smooth normals, swaps the bevelled-cube chassis for rounded-corner slabs, adds the top row of pad lamps, and writes physically based colour, roughness and metallic values matched to TE's product photography (`.hmesh` version 2). The renderer still reads version 1 files.

## Honesty rules (carried over from the Mac app)

- Nothing is shown as hardware truth unless halo actually observed it. A USB connection alone isn't "live"; live starts with the first real MIDI event.
- The disconnected demo is always labelled PREVIEW, and the mock source is always labelled MOCK.
- Pads and LEDs are released on disconnect, or if the MIDI backlog ever overflows, so a lost Note Off can't leave a pad stuck down.
