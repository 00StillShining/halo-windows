# Redesign checkpoint: 26 October 2026

**Status:** the redesign is complete in mock testing.
- **Pages:** all seven are rebuilt and polished.
- **3D stage:** fully merged (`2851024`) and signed off at G3 against TE's photos. It has:
  - soft shadows and baked AO;
  - a dark-look floor reflection;
  - the live LCD with honest provenance;
  - pad springs and cue lights;
  - on-demand frames (0 frames at rest);
  - orbit inertia;
  - the Low and Still tiers.
- **Performance and tests:** RTX 5080 Release GPU p95 0.3-0.5 ms; a 30-minute soak with 0 GL errors and flat memory; HaloTests 176, HaloUiTests and the lint all green. Nothing is pushed.
- **Decisions:**
  - O3 LED echo: not built. The LEDs mark parameter selection on the device, so echoing pad hits would show false state.
  - The RECORD highlight gate is waived.
  - No mock marking in the UI.

## What is left (needs the owner)
1. Device checks with the real EP-40 on `build\Redesign\Halo_artefacts\Debug\Halo.exe`: sync, assign (armed, then the card), delete card, EDIT send and SEND TO PADS (progress and AUTO-TRIM), backup (cancel one, complete one), output menu, recording a take.
2. Optional: AMD iGPU timing. This needs Halo set to "Power saving" in Windows Graphics settings.
3. Merge `redesign/te-hardware` into `main` when the owner is happy.

---

# Redesign checkpoint: 23 October 2026

**Status:** every page is rebuilt in the OP-1 x OP-XY language (R0 to R13 plus a polish pass). The 3D Riddim stage is merged into `redesign/te-hardware` at `d81b485`. It has:
- true proportions;
- TE's own artwork;
- the D5 screen;
- the port wall, speaker and fader;
- cached shadow maps.

All tests pass: HaloTests 168, plus HaloUiTests and the write-site lint. Nothing is pushed.

## Owner decisions recorded
- No mock marking anywhere in the UI (`plan.md` section 0.3).
- The RECORD highlight gate is waived because it matches TE's photo (riddim plan, Appendix B).
- Smart App Control was turned off by the owner, so branch builds can run.

## What is left
- **Owner device checks with the real EP-40** (gate U4), on the branch build `build\Redesign\Halo_artefacts\Debug\Halo.exe`:
  - sync;
  - assign: armed, then the confirm card;
  - delete card;
  - EDIT send and SEND TO PADS: progress and AUTO-TRIM;
  - a backup: cancel one, complete one;
  - output menu;
  - recording a take.
- **3D:** the side-wall battery icon; M8 (baked AO, grain, floor reflection); M9 (live LCD, bloom); M10 (motion); M11 (tiers and hardening).
- **Then:** merge `redesign/te-hardware` into `main` once the device checks pass.

---

## Earlier checkpoint (19 October 2026)

# Redesign checkpoint: 19 October 2026 (end of day)

Paused at the owner's request. Everything is committed locally; nothing is pushed.

## Where things are

| What | Where |
|---|---|
| Stable Halo (daily use) | branch `main` at `ec02dec`; built in `build\Debug` |
| Legacy archive of Halo 1 | `Documents\Halo Legacy\Halo 1 (main ec02dec, 2026-10-19)\` and git tag `legacy-v1` |
| App redesign (OP-1 x OP-XY screen language) | branch `redesign/te-hardware`, head `036c63b` |
| 3D Riddim stage upgrade | branch `worktree-agent-af51e98e63b4074d8` (worktree `.claude\worktrees\agent-af51e98e63b4074d8`), head `25bdd2b`. **Not merged into the redesign branch yet.** |
| Plans | `docs/redesign/plan.md` (app), `docs/redesign/riddim-3d-plan.md` (3D) |
| Mockups | `docs/redesign/mockups/` (SVG) and `mockups/png/` (renders); `hybrid-*` is the chosen direction |
| Redesign branch build | `set HALO_BUILD_DIR=...\build\Redesign` then `scripts\build.cmd build` |

## App redesign: done R0 to R4b; R5 is a WIP commit

- **R0:** `HALO_DATA_ROOT` sandbox, backup cache, HonestState, deterministic mock, and **ASSIGN now asks first**.
- **R1:** colour tokens, 5 embedded OFL font families (licences included), theme plumbing, tooltips.
- **R2a/R2b:** primitives and a Debug gallery (`HALO_UI_GALLERY=1..5`), the encoder system core, and WaveCache.
- **R3:** WriteGate and ConfirmCard; every device write goes through one gate and names its backup.
- **R4a/R4b:** the shell (black glass, status strip, graphite deck with pictogram keys, CHOP on its own key Ctrl+7, caution band in MOCK) and the four colour encoders (LEVEL, GRAB and ECHO MIX are live; ORBIT stays honestly dead until the 3D work adds camera setters).
- **R5 (WIP `036c63b`):** the meter poll, clock position, take and grab records, rail layout and `PlayPage.h`.
- **Next:**
  1. Write `PlayPage.cpp` (last pad, monitor, grab tape lane, rack chain, events, spine, clock lane).
  2. Wire it into MainComponent.
  3. Delete `PlayRail.*` and `MonitorPanel.*`.
  4. Gate U2 review against `mockups/png/hybrid-play.png`.
  5. Then R6, CHOP.
- **Tests:** HaloTests 126/126, HaloUiTests 15/15, write-site lint green.

## 3D stage: done M0 to M3 plus the sheen fix; M4 is a WIP commit

- **M1:** TE palette and legends (BPM / METRONOME), grounded device, fitted camera, ring as a floor light pool, `StageLook` paper/dark.
- **M2:** HDR, 4x MSAA, Neutral tone map, exact sRGB, Low tier.
- **M3:** studio IBL, GGX, perspective floor. The screen glare is now a soft sheen, so the LCD stays readable.
- **M4 (WIP `25bdd2b`):** the `.hmesh` v3 format, parser, tests, converter and asset are done. The renderer still draws the v2 asset. Next:
  1. Single vertex/index buffer upload.
  2. Slot transforms in the shaders.
  3. Material table texture.
  4. Remove the v2 asset.
  5. Shot comparison.
- **Then:** M5a and M5b (stepped chassis, real keycap fillets and proportions: the biggest remaining gap), M6, M7.
- **Tests:** HaloTests 128/128; Python 6/6.

## Merge plan (first thing next session)

1. Merge `worktree-agent-af51e98e63b4074d8` into `redesign/te-hardware`. Expect small conflicts in `CMakeLists.txt` and `Source/App/Main.cpp`.
2. Wire `Theme::stageLook()` to `setLook(dark)` so the stage turns black inside the frame, and add the camera setters so ORBIT goes live.
3. Build `build\Redesign`, run both test suites, and shoot PLAY.

## Owner checks that need the real EP-40 (later)

- ASSIGN, SEND, SEND TO PADS and DELETE each open the confirm card naming the backup; unplugging with a card open makes its confirm dead.
- Returning to PLAY shows the first stage frame within 150 ms.
- A bare Alt tap no longer swallows the next key.
- AMD iGPU stage timing: set Halo to "Power saving" in Windows Graphics settings, then run `stage_shots.ps1 -Shots hero -Stats -HoldSeconds 12 -Exe <Release exe>`.
