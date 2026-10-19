# halo for Windows

A native Windows companion app for the Teenage Engineering **EP-40 Riddim**: a to-scale 3D model of the device that reacts live to what the hardware sends over USB. It's a port of the macOS halo app (Swift, `00StillShining/halo`) to C++ with [JUCE](https://juce.com).

## Status

| Area | State |
|---|---|
| Live PLAY view: 3D EP-40, pad travel, velocity LEDs, replica LCD, halo ring | Working, tested with a real EP-40 on Windows 11 |
| MIDI: auto-detects the EP-40, follows plug and unplug, reports a port held by another app | Working |
| Status bar and rail: honest connection, feed, clock and transport state | Working |
| Audio monitor: EP-40 USB input to any PC output, gain (default -12 dB), -1 dBFS safety limiter, meters with clip | Working (WASAPI shared mode). It refuses EP-40-to-EP-40 feedback and stops on unplug |
| Recorder: raw pre-monitor takes, 24-bit WAV, in `Music\Halo\Recordings` | Working; the file is fully finalised before it's read back |
| GRAB: the last 4/8/16 bars (clock + transport playing) or 5/10/30 s, zero-cross trimmed | Working; downbeats are exact audio frames from MIDI timestamps |
| CAPTURE: library of takes, grabs and imported files (WAV/AIFF/FLAC/OGG/MP3/M4A; drag-and-drop) | Working |
| EDIT: trim, equal-power fades, gain, normalise, mono/stereo, EP-40 quality presets, exact size, audition, export 16-bit WAV | Working |
| LOAD: the EP-40's own library over TE's protocol (storage, projects, pads, categories, every sound); audition, save to library, assign to pad, delete | Working on a real EP-40 (OS 2.5.0) |
| BACKUPS: full read-only backup (9 project TARs + every sound as a bit-exact WAV + metadata); unlocks writes | Working (477 sounds, ~20 min) |
| EDIT "SEND TO EP-40" and CHOP "SEND TO PADS": upload to empty USER slots, verify, assign pads | Working; round trip verified byte-identical |
| Dub RACK: TAPE ECHO (tempo-sync / tap), SPRING, SWEEP, LOW END; PRINT FX records post-rack | Working; bit-transparent when bypassed |
| CHOP: onset slicing (editable cuts), slice audition (click or 1-9, 0), pad plan, export slices + pads.txt | Working |
| Restoring a backup to the EP-40 | Not built yet |

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

### Debug-only hooks

Debug builds read these environment variables, so each state can be screenshotted and the audio path tested without clicking:

| Variable | Effect |
|---|---|
| `HALO_RAIL=open` | Opens the rail |
| `HALO_MODE=1..6` | Starts in that mode |
| `HALO_OUTPUT=<part of a device name>` | Picks the monitor output |
| `HALO_MONITOR_DB=-60` | Sets the monitor level (-60 is silent) |
| `HALO_AUTOMONITOR=1` | Starts the monitor |
| `HALO_AUTORECORD=<seconds>` | Records one take of that length |
| `HALO_AUTOGRAB=<seconds>[,bars]` | GRABs once after that long |
| `HALO_OPEN=<path>[\|chop]` | Opens a file in EDIT (or CHOP) without importing it |
| `HALO_AUTOEXPORT=edit\|slices` | Exports once the opened file has loaded |

Tools:
- `tools\dev\screenshot.ps1` captures just the Halo window.
- `tools\dev\inspect_take.py` prints a take's format and level.
- `tools\dev\make_test_loop.py` writes a 2-bar drum loop for testing EDIT and CHOP.

Keyboard shortcuts: Ctrl+1..6 switch modes, Ctrl+M toggles the monitor, Ctrl+R toggles recording, Ctrl+G grabs.

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
