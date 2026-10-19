# halo — evaluation & improvement plan

Repo: `00StillShining/halo` (25 commits, last activity 2026-07-14). Reviewed 2026-10-19 by static reading only. Nothing was built, because this machine runs Windows.

> **Update 2026-10-19: decisions and progress**
>
> - **Stack changed to native C++ / JUCE 8.** The owner wants a native Windows app, not a web one, so the Tauri/browser recommendation in §2 and §4 is superseded. The Track B milestones still apply; read "Rust/cpal/midir" there as JUCE's own audio and MIDI classes.
> - **B0 (hardware check): done.** See §6.
> - **B1 + B2 (shell + live Play view): done in this repo.**
>   - The real Blender-built EP-40 model, baked from the USDZ.
>   - Pad travel and velocity LEDs, the replica LCD, and the halo ring.
>   - Status bar, mode bar and Play rail.
>   - EP-40 auto-detect with hot-plug.
>   - A MOCK source for working without hardware.
>   - 32 unit tests.
>
>   It's verified against the real EP-40 on Windows: the app connected and went LIVE from the device's MIDI clock.
> - **Mac bugs fixed in the port:**
>   - Stop clears the clock, and the clock has a 0.5 s freshness timeout (A1.5).
>   - A MIDI overflow releases all pads instead of dropping a Note Off (A1.9).
> - **B3 (monitor + capture): first part done.**
>   - WASAPI route from the EP-40 USB input to any output, with gain, the -1 dBFS limiter, meters and clip flags.
>   - Raw 24-bit WAV takes.
>   - Fixed Mac bugs: the file is closed before it's read back (A1.1); dropped audio and low disk are reported (A1.2); the header uses the route's real rate (A1.3).
>   - Also fixed: the Mac meter applied per-sample ballistics once per block, so release was hundreds of times too slow.
>   - Verified: the route opens on the real EP-40 at 44.1 kHz, 480 frames, ~33 ms, and a take is a valid WAV.
>   - **Owner listening test (2026-10-19): passed.** EP-40 audio is heard clearly through a PC output, and the meters move.
>   - Windows' default output on this PC *is* the EP-40, so halo asks you to pick another output rather than feed back.
>   - Still to do in B3: drift correction. JUCE's single WASAPI device runs input and output on separate clocks with no varispeed.
> - **GRAB: done.**
>   - An always-on 60 s+ history; the last 4/8/16 bars (fresh clock, transport playing) or 5/10/30 s, zero-cross trimmed, written off the UI thread.
>   - Downbeats are exact audio frames converted from MIDI timestamps. The Mac stamped them on UI arrival, one tick early (tick 96, not 97).
>   - Stop keeps the bar phase, so Continue resumes in phase.
>   - Verified: 4 bars at 120 BPM gives an 8.000 s WAV.
>   - **Not yet felt on hardware:** bar-locked GRAB with the EP-40 playing.
> - **B4 (dub RACK): done.**
>   - TAPE ECHO (clock sync, divisions, tap), SPRING, SWEEP, LOW END; ENGAGE; PRINT FX.
>   - Bit-transparent bypass, with 8 DSP tests.
>   - Fixed a Mac bug: SWEEP and LOW END controls took ~10 s instead of 20 ms to glide.
>   - **Not yet felt on hardware:** how the rack sounds on the EP-40.
> - **B5 (LOAD / EDIT / CHOP, local): done**, re-scoped to real work instead of mock device data.
>   - CAPTURE is the library: takes, grabs and imported files.
>   - EDIT does non-destructive prep, EP-40 quality presets, exact size estimates, audition at the treatment rate, and 16-bit WAV export.
>   - CHOP finds onsets and slices, lets you edit cuts, plays slices back, plans pads (bytes and overflow), and exports slices with a pads.txt.
>   - LOAD shows the honest state, plus the working path: export, then TE's EP Sample Tool.
>   - New on Windows: a band-limited Kaiser-sinc resampler, so LO-FI exports don't alias.
>   - The Mac's mock device library and slot numbers were dropped; they need the protocol.
> - **CHOP improvement over the Mac:**
>   - Log-compressed spectral flux. On a 16-hit drum loop the Mac's linear flux found 20 cuts at every sensitivity, so the slider did nothing; this finds the 16 hits.
>   - Cuts closer than the minimum slice length to either end are dropped.
> - **More Mac bugs found while porting (all fixed here):**
>   - Meter ballistics ran once per block with per-sample coefficients.
>   - Rack control smoothing had the same problem.
>   - GRAB downbeats came one tick early.
> - **Owner hardware check (2026-10-19):** the owner pressed pads in all four groups on the real EP-40, and the correct keys moved and lit in the Windows app.
> - **Clock tempo: resolved.**
>   - The EP-40 was set to 180 BPM, and halo first read 179.2.
>   - Cause: Windows delivers MIDI clock in bursts, and the Mac's per-tick smoothing threw away the in-burst gaps.
>   - Tempo now comes from the span across a one-bar window and reads **180.0** on the real device. This also applies to the Mac app (new §3 item).

---

## 1. Verdict

**halo does not run on Windows, and it can't be made to with small fixes.** It's a native macOS app in every layer:

| Layer | Technology | Windows equivalent? |
|---|---|---|
| UI (~5,950 lines) | SwiftUI + AppKit | None. Swift on Windows has no SwiftUI. |
| 3D EP-40 model (~2,400 lines) | RealityKit + USDZ | None |
| Audio monitor / recorder (~990 lines) | CoreAudio HAL, dual AUHAL | None (WASAPI/ASIO are different APIs) |
| Sample decode / audition (~1,430 lines) | AVFoundation | None |
| MIDI (~350 lines) | CoreMIDI | None (WinMM / Windows MIDI Services) |
| FFT (~490 lines) | Accelerate / vDSP | Needs any FFT library |
| **Pure logic (~4,040 lines)** | Foundation only | **Portable.** This covers the DSP rack, limiter, ring buffers, meters, GRAB math, MIDI mapping, display state, backups/journal, capability model. |

The build also targets **macOS 26, Apple silicon only, Xcode 26**, so it won't run on Intel Macs or older macOS either.

**Completeness:** everything that works without the EP-40's private protocol is built and unit-tested. That's about 16.7k lines of app code and 286 tests across six modes: Play, Load, Edit, Capture/GRAB, Chop and Rack, plus Backups and Diagnostics. Two things are still missing:
1. **Anything that writes to the device**: sending samples, assigning pads, restoring backups, reading the device's sample list. These all depend on Teenage Engineering's undocumented SysEx protocol ("Phase 0B"). Five buttons are permanently disabled, and Load, Edit and Chop run on *mock* device data.
2. **Hardware verification** of the audio path. The live monitor route, latency, drift and recorder have never been confirmed with a real EP-40 signal.

The code itself is well structured and honest. There are no TODO markers, no `try!`, no `fatalError`, and the audio render callbacks are real-time safe. The weak spots are listed in §3.

---

## 2. Recommendation: two tracks

**Track A: stabilise the Mac version as the reference.** This needs a Mac, and it's cheap. The Mac app is the most complete statement of what halo *is*, and fixing its bugs keeps it a trustworthy spec.

**Track B: build a cross-platform version that runs on Windows.** Recommended stack:

| Concern | Choice | Why |
|---|---|---|
| App shell | **Tauri 2** (Rust backend + web UI) | Small native installer for Windows, macOS and Linux, with real file system access |
| UI | TypeScript + React (or Svelte) | Fast to build; the design tokens in `HaloColors/Metrics/Typography` translate directly to CSS variables |
| 3D model | **Three.js + glTF** | Export glTF from the existing `tools/blender/build_ep40.py` (Blender exports glTF natively) and keep the same entity-name contract (DD-006) |
| MIDI | Rust `midir` (WinMM/WASAPI on Windows, CoreMIDI on Mac) | Also covers SysEx for the future protocol work |
| Audio monitor / record | Rust `cpal` (WASAPI, optional ASIO) + `hound` (WAV) | Low-latency, real-time-safe callback, like the AUHAL design |
| DSP / onset detection | Rust port of the pure-Swift files + `rustfft` | Port the 286 Swift tests alongside as the spec |

A plain browser app (WebMIDI + Web Audio) would be simpler. But low-latency input monitoring and the dub rack suffer on Windows browsers, and file and backup handling are awkward there. A browser app is fine for a quick spike of the Play view; Tauri is better for the real thing.

**Windows gotchas to plan for:**
- **Exclusive MIDI ports.** Classic Windows MIDI lets only *one* app open a port at a time. If a DAW or TE's sample tool holds the EP-40, halo can't connect. Detect this and say so, the way halo already does with its honest-state vocabulary. Newer Windows MIDI Services removes the limit where it's installed.
- **USB audio.** It's still UNKNOWN whether the EP-40 exposes a USB audio device at all (device-capabilities.md). Check this in Device Manager and Sound settings before building anything audio-related on Windows.
- **Per-device sample rates.** WASAPI shared mode resamples to the Windows mixer rate. Record the device's real rate in the WAV header (the same bug class as Mac risk #3 below).

---

## 3. Track A: fixes to the Mac app (priority order)

Sizes: **S** < ½ day, **M** 1–2 days, **L** longer.

### A1. Correctness bugs (do first)
1. **Recorded takes may be read before the WAV header is written** (S). `WAVFileWriter` relies on `AVAudioFile` being released to finalize the header. The drain thread still holds a reference, though, and `SessionRecorder.stop()` calls `takes.ingest` straight after. Fix: add an explicit `close()` to `WAVFileWriter`, call it in `RecordingDrain.finish()`, and add a test that ingests immediately after stop.
2. **Disk-full errors are silently swallowed** (S). `try? writer.write` (SessionRecorder.swift:202) truncates takes without a word. Surface an error state.
3. **Takes can get the wrong sample rate in their header** (M). `activeRouteSampleRate` reads the *currently selected* output device, not the rate the route was built with. Changing the picker or the device rate mid-route makes takes and GRABs play at the wrong pitch. Fix: store the route's rate when it starts. The reconciler should also restart or flag the route when the rate changes, not only when a device disappears.
4. **Ordering between the two audio threads is too weak** (S). The `armed`/`postFX` flags use `.relaxed` atomics across the input and output callbacks. Use `.releasing` stores and `.acquiring` loads so only one thread ever writes the tap ring.
5. **Stale MIDI clock** (S). Stop doesn't clear the tempo/clock state, and there's no freshness timeout, so GRAB can use an old downbeat and the rack keeps a stale BPM. Add a ~500 ms timeout and clear on `.stop`. Then check on the device whether the downbeat should land on clock 96 or 97.
6. **Input sample-rate mismatch** (M, needs device). The input AUHAL is given the *output* device's rate, but AUHAL doesn't resample input. Either insert an AudioConverter or match rates. Combine this with item 9.
7. **Unchecked frame counts in the render path** (S). Clamp `inNumberFrames` to `maxFrames` in the input callback, and check the return codes of the `MaximumFramesPerSlice`/`BufferFrameSize` setters.
8. **Main-thread stalls** (M). `RecordingDrain.finish()` blocks the main actor on a semaphore, and `GrabController.grab` does about 60 s of mixdown plus a WAV write synchronously on main. Move both to a background task.
9. **Stuck pads** (S). When the MIDI mailbox fills it drops the *oldest* event, which can lose a Note Off and leave a pad stuck. Never drop Note Offs, or reset held pads on overflow.

### A2. Unfinished but buildable without the protocol
10. **Wire up the drift corrector** (L, needs device). `DriftController`/`DriftCompensatingConverter` are built and tested but never instantiated. First add xrun/underrun counters and ring-fill telemetry to Diagnostics, so drift can be seen at all.
11. **Sync the capability catalogue with the docs** (S). The July device session confirmed pad transmit, pad order and the silent X/Y knobs, but `DeviceCapabilities.catalogue` still says UNKNOWN/DOCUMENTED. Update the catalogue, add the missing rows, and replace `testNoCapabilityIsObserved` with an allow-list of observed IDs. Also fix the self-contradicting text in `device-capabilities.md`: its header says "no device session yet", and its checklist still calls pad order an assumption.
12. **Write to the operation journal** (S). `OperationJournal` is read by the Backups rail but never written to.
13. **Remove palette B** (S). The gate decided on A, but B is still in `HaloColors.swift` and `capture_gate.sh`.
14. **Re-test the device settings** (S, needs device). Retest with velocity on (`301`), MIDI clock out (`102`) and loop-pad behaviour, as already listed in the docs.
15. **Feedback guard** (S). Ask for confirmation before ⌘M starts monitoring when a feedback loop is likely, instead of only warning.

### A3. Code health
16. **Split `HaloAppModel.swift` (947 lines, ~20 subsystems)** (M). Pull out:
    - the MIDI→display reducer (lines ~759–875)
    - the clock/tempo tracker (~877–922)
    - a diagnostics presenter
    - typed enums in place of the string statuses (`"NO DEVICE"`)

    The reducer and the tracker are pure, so they become the first files ported in Track B.
17. **Remove dead code** (S): the unused `isConfirmed`, the `ProtocolTrace` with no producer, the ignored `sampleRate` parameter, and the constant `rackAvailable`.
18. **Fix force unwraps** (S): `baseAddress!` in AudioDeviceDiscovery.swift:194 and 285, and the ones in OnsetDetector.

### A4. Tooling
19. **Add CI** (S). A GitHub Actions `macos-latest` job running `scripts/build.sh test`. Note that this needs an Xcode 26 runner image.
20. **Fix the build scripts** (S). Use `-derivedDataPath build/` instead of `find ~/Library/... | head -1`, which can launch a stale build. Add `*.xcresult` to `.gitignore`, and remove the hardcoded `/Users/stillshining` paths from the docs.
21. **Large files** (optional). Consider Git LFS for `EP40.usdz` and the gate PNGs, or regenerate the USDZ from the Blender script at build time.

---

## 4. Track B: Windows / cross-platform build (milestones)

Each milestone is usable on its own.

| # | Milestone | Content | Size |
|---|---|---|---|
| B0 | **Hardware check on Windows** | Plug in the EP-40. Does it appear as a MIDI device? As a USB audio device? List both in a 50-line Rust or WebMIDI probe. This decides the scope of B3. | S |
| B1 | **Skeleton + design system** | Tauri app, CSS tokens from HaloColors/Metrics/Typography (palette A), mode bar, status bar, honest-state plates | M |
| B2 | **Live Play view** | `midir` input, then ported `EP40MIDIMapping` + the display reducer + `PadHoldRegistry`, then the Three.js EP-40 (glTF from Blender) with pad travel, LED velocity and the halo ring | L |
| B3 | **Monitor + Capture** | `cpal` input→output with a ring buffer, ported `SafetyLimiter`/`MonitorGain`/meters, a session recorder to WAV, and GRAB | L |
| B4 | **Rack** | Port `DubRack` (TAPE ECHO / SPRING / SWEEP / LOW END) with its tests, plus MIDI-clock tempo sync | M |
| B5 | **Load / Edit / Chop (local)** | Sample import and decode (`symphonia`), waveform cache, non-destructive prep, an `OnsetDetector` port, slice audition | L |
| B6 | **Backups + Diagnostics** | Manifests, journal, capability matrix (one shared JSON catalogue for both apps) | M |

Port the Swift unit tests *first* for each module. They're the spec, and they keep both versions behaving the same.

---

## 5. Shared track: the device protocol (Phase 0B)

This one blocks the app's "send to EP-40" features on both platforms. It's the highest-value work and the highest-risk.

1. **Read-only first.** Capture the SysEx that TE's official EP Sample Tool exchanges with the EP-40, for example via a MIDI monitor, or by logging WebMIDI traffic if the tool runs in a browser. Record the device identity byte, framing, and whether the byte-5 conflict in `docs/research/05-protocol-evidence.md` resolves to "constant" or "direction".
2. **Implement listing and download only.** Verify against the data the official tool shows. This alone unlocks real data in Load and Edit (replacing `MockDeviceLibrary`) and waveforms for device samples.
3. **Guarded upload to one empty slot**, verified by reading it back, with a backup snapshot first. Only then enable SEND + ASSIGN, SEND TO PADS, SEND CHANGES and the transfer-progress ring.
4. Keep the existing honesty rule: a feature unlocks only when its capability row is OBSERVED.

> ⚠ Writes are destructive to the device. Back up the EP-40 with TE's official tool before any write experiment.

---

## 6. Decision (2026-10-19): Windows first

The Mac is still available, but **Windows is the focus for now**. Track A is paused. The Mac app stays as the reference spec, and its bugs (§3) are fixed only when a Track B port touches the same logic. For example, port the WAV writer *with* an explicit close, and the clock tracker *with* a freshness timeout.

**Toolchain on this PC:** Node 24, npm, git and WebView2 are installed. Rust and the MSVC C++ build tools are **not**, and Tauri needs both. So the order is:

1. **Browser first.** Build B0–B2 as a Vite + TypeScript + Three.js web app, using WebMIDI in Chrome or Edge. This needs nothing new installed, and Edge already supports WebMIDI, including SysEx after a permission prompt.
2. **Add Tauri at B3.** Low-latency monitoring is the first thing a browser can't do well. Install Rust (`rustup`) and Visual Studio Build Tools then, and wrap the *same* frontend in Tauri, with `cpal`/`midir` in Rust.

### B0 result (2026-10-19): passed

Windows 11 recognises the EP-40 as a class-compliant USB device (`VID_2367 PID_8520`) with **no driver install**:
- **MIDI:** an `EP-40` input port and an `EP-40` output port (WinMM). Windows MIDI Services isn't installed, so the port is single-client.
- **Audio:** render endpoint "Speakers (EP-40)" and capture endpoint "Digital Audio Interface (EP-40)", both stereo. The Windows default format is 44.1 kHz / 16-bit. Note that halo's Mac code assumes 48 kHz in places, such as the GRAB ring size.
- **Browser:** the owner ran `probe/index.html` in Edge and reported that all checks passed: MIDI connect, pads, identity, and the audio input meters. The detailed findings JSON wasn't captured, so the identity-reply bytes, velocity values and clock BPM are still to be recorded.
- The Claude desktop app's built-in browser refuses WebMIDI permission, so use Edge or Chrome for development.

### Order
1. ~~**B0 hardware check.**~~ Done (see above).
2. **B1 + B2:** a live Play view in the browser, with pads lighting from real MIDI.
3. **Protocol read-only work (§5.1–5.2)** alongside B3 and B4 in Tauri.
4. **Later:** B5 and B6, protocol writes, drift correction, the XY-pad → rack mapping.

## 7. Open questions

- Should the new version also run on Mac eventually? The browser/Tauri route keeps that open at no extra cost.
- Which features matter most on Windows: the live Play view, monitoring/recording, or sample management?
