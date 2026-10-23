# EP-40 Riddim 3D stage: upgrade plan

**File:** `docs/redesign/riddim-3d-plan.md` · **Branch:** `redesign/te-hardware` · **Status:** final, ready for M0

**Scope.** This plan covers only the 3D EP-40 Riddim on the main stage:
- `Source/Stage/*`
- the model pipeline in `tools/model/*`
- the stage assets
- the stage's tests and dev tools

The app-wide OP-1 / OP-XY redesign is a separate effort. The two meet at two points only:
- the stage-owned look tokens (`StageLook`, §3.1);
- the overlay rule in §3.9: nothing JUCE draws can sit on top of the stage.

**Sources merged here:**
- the renderer audit
- the asset audit
- the EP-40 reference brief, taken from TE's to-scale vector drawings
- the asset-first and renderer-first candidate plans
- the critique of the first draft. Every substantive point is resolved below; Appendix A maps each one to where it was resolved.

**Rules for disagreements.**
- **Geometry:** TE's scale drawings decide. A value counts only once it has been **measured from SVG path coordinates**; pixel readings are provisional.
- **Look:** the reference brief's material notes decide.
- **Truthfulness:** `RingState`, `FeedMode` and `DisplayState` provenance decide. The stage may only show state that Halo has actually observed.

**Order of work:**
1. Cheap fixes to the current asset that are visible straight away.
2. The lighting core, which makes any geometry look premium and has to exist before materials can be judged.
3. New geometry and artwork, which make it *recognisably* an EP-40.
4. The live display, motion and hardening.

The owner reviews the look three times along the way, not just once at the end.

---

## 0. Decisions settled by this plan

| # | Topic | Decision |
|---|---|---|
| D1 | Geometry source of truth | A Python **model compiler** (`tools/model/ep40/`) builds every part from a measured spec. The USDZ and `build_ep40.py` are retired as geometry sources for Windows; the USDZ is kept only for a contract-name cross-check. Blender is not required. The Mac app keeps its USDZ and is out of scope. Dependencies are chosen in M0 by checking for Python 3.14 wheels (§4.1). |
| D2 | Tone mapper | **Khronos PBR Neutral by default.** AgX is selectable. ACES Hill is built only for comparison, because it pushes the riddim orange toward yellow and dulls the green. M2 settles the choice with a swatch ΔE check. |
| D3 | Pad-strike feedback | The cap is **never tinted**, and today's whole-pad `uGlow` wash is removed. A strike shows as: <ul><li>a **fixed** travel. The default is 2.6 mm, owner-tunable from 2.4 to 3.2 mm (O5). It does not scale with velocity, because a real key bottoms out whatever the pressure, and a 3.2 mm drop leaves the cap flush with the deck so it seems to vanish.</li><li>a **Halo cue** whose intensity follows velocity: a very subtle aperture underglow, recorded in DD-006g as a Halo-owned cue in the same family as the ring pool. The owner picks this or the LED echo at gate G3, from side-by-side shots (O3).</li></ul> |
| D4 | Truthful lighting | An emissive may show only state Halo has observed. The authority is `RingState`, `FeedMode` and `DisplayState` provenance (preview is "always labelled PREVIEW"). <ul><li>The 16 + 2 LEDs are modelled, bound and able to emit, but they light only from a mapped, observed state field. Today's notes-only MIDI (`Ep40MidiMapping.h`) maps to none of them, so they stay dark.</li><li>The real LEDs mark parameter selection (LEVEL, PITCH…), so echoing pad hits on them would mimic a device indicator showing false state. That is why the underglow, not the LED echo, is the default Halo cue.</li><li>Preview frames must look different from live frames (§6, M9).</li></ul> |
| D5 | Screen | Real geometry. The glass window (`display_glass`) is **1.0 mm thick**: top at reference z 16.0, underside at z 15.0. The icon plane (`display_surface`) sits at **z 14.4**, under a real 0.6 mm air gap, so it shows parallax and cannot z-fight. Lint requires glass underside − icon plane ≥ 0.3 mm. A dedicated LCD shader draws the icons from an icon SDF atlas and a small state texture. The window stays backlit emerald acrylic, as on the device. |
| D6 | Halo ring | No longer geometry. It becomes an emissive **pool of light on the floor** that follows the chassis footprint. It covers every `RingState` (linked, monitoring, recording, transfer, error, discovering) and is hidden when disconnected. It arrives in **M1** inside the existing ground shader, so the cue never disappears. `halo_ring` stays in the asset as a zero-triangle anchor to keep the DD-006 name. |
| D7 | Look ownership | The stage owns its look through `StageLook` tokens, held **by value** and copied under the existing SpinLock: backdrop, floor, floor texture, shadow tint, one ring colour per `RingState`, exposure, tone mapper and light intensities. <ul><li>Two presets ship from M1: **Paper**, the default (graph paper, DD-GATE Palette A), and **Dark** (OP-XY near-black, `#060607` / `#38383F`).</li><li>**Device plastic colours are not tokens.** They are facts of the hardware and live in the model spec.</li><li>The stage stops reading `HaloColours::graphPaper` (`StageComponent.cpp:451,486-487,611`).</li></ul> |
| D8 | Mesh format | `.hmesh` **v3**: chunked, with a material table, per-part kinds, flags and slots, a model-space rest pose, 24-byte vertices and baked AO. <ul><li>Slots carry **translation plus rotation only**: no scale, no per-draw normal matrix, normals rotated by the slot quaternion in the vertex shader.</li><li>The whole device draws in **one opaque draw plus one glass draw**.</li><li>The parser lives in `HaloCore`, has no JUCE dependency, is unit-tested, and reads v1, v2 and v3.</li></ul> |
| D9 | Artwork and data textures | <ul><li>**Artwork:** the front SVG is rasterised **per ink colour, using each path's own fill rule** (`nonzero` or `evenodd`) across all subpaths. Atlas regions are then cut out by target rectangle. Stroked paths, the masked glare group and illustration outlines (key borders, knob rings, screw circles) are dropped. Each legend has an explicit include rectangle in `spec.py`, and `--check` fails on any ink outside a listed rectangle.</li><li>**Data textures** (SDF atlases, grain, deck AO, environment) are stored as raw chunked binary (`.htex`, `.henv`), **never PNG**, because JUCE's PNG loader premultiplies alpha (`juce_PNGLoader.cpp:428`) and keeps pixels as BGRA in memory. They are uploaded with an explicit channel order.</li><li>PNG is used only for opaque colour images (the Still-tier poster).</li></ul> |
| D10 | Ambient occlusion | **Baked offline in the compiler:** per-vertex AO plus an R8 deck AO texture mapped by model XY, in the same space as the face atlas. <ul><li>The deck is triangulated with a constrained Delaunay mesh refined to a 2 mm grid, so vertex terms interpolate cleanly with no slivers.</li><li>Contact darkening around moving keys is **analytic**: a rounded-rectangle SDF per aperture plus the slot's current travel, passed as uniforms. No texture is needed.</li><li>The per-frame height-map AO is deleted, and there is no runtime bake, cache or first-run hitch.</li></ul> |
| D11 | Artwork licence | The owner says copyright is not a concern for this personal project. This is recorded as **DD-006g**: "the Windows stage may use TE's own vector artwork; the aperture underglow and floor pool are Halo-owned cues". It supersedes the DD-006b/e boundary for Windows only. `--artwork neutral` (OFL fonts, generic icons) restores that boundary with a rebuild. |
| D12 | Hardware and tiers | <ul><li>**Development and budget targets:** this machine's **NVIDIA RTX 5080** (typical) and its **AMD Radeon iGPU** (the floor, forced through Windows Graphics settings).</li><li>No Intel heuristics and no adaptive tiering.</li><li>Three tiers: **High**, **Low** (today's forward path, cleaned up) and **Still** (2D poster).</li><li>`GL_RENDERER`, `GL_VERSION` and the granted `GL_SAMPLES` are logged at every start, because "Let Windows decide" can put Halo on the iGPU.</li></ul> |
| D13 | Repaint policy | The stage renders only when something changed (§7). <ul><li>**Idle** means *connected, transport stopped, no input*, and costs **0 stage frames**.</li><li>When disconnected, the stage follows the `DisplayState::previewFrame` demo (`AppModel.cpp:436`, about 6 fps). The discover sweep advances only on those frames, so it adds none of its own.</li><li>Idle is measured as stage frames and render-thread time, not whole-process CPU. `AppModel`'s own 60 Hz timer is outside this plan.</li></ul> |

### Owner inputs (none of them block M0 to M5a)

- **O1. Optional caliper numbers from the owner's EP-40.** No photo, since DD-006b records that the owner cannot photograph the unit. The numbers:
  - total thickness at the screen section
  - thickness at the key deck
  - keycap height above the deck
  - how far the feet stand proud

  Every height can already be measured from TE's side and top-edge SVGs, so these numbers only tighten tolerances.
- **O2. Floor treatment.** The default is a graph-paper floor texture that fades to the floor token (DD-GATE Palette A). The redesign may override it with a plain sweep, because it is only a token value.
- **O3. Pad cue.** Subtle aperture underglow (default) or row-LED echo. Chosen at G3 from side-by-side shots.
- **O4. Showcase drift.** A slow ±8° yaw turntable at a 30 fps cap. Built but off by default; the redesign decides whether it gets a toggle.
- **O5. Key travel.** 2.6 mm by default, tunable from 2.4 to 3.2 mm. Chosen at G2.

### Owner review gates

| Gate | After | Reviews |
|---|---|---|
| **G1** | M3 | Lighting and materials on the current geometry: `hero`, `low` and `keys` in both presets, beside TE photos |
| **G2** | M5b | Proportions, stepped body and keys with numerals; travel (O5) |
| **G3** | M11 | Full sheet; pad cue (O3); sign-off "recognisably the real device, trophy-worthy" |

---

## 1. Target look

The goal is a Teenage Engineering catalogue photograph that the user can turn with the mouse.

**Device and setting**
- A sharp, nearly rectangular cream slab (#DCD2BF) rests on a warm floor that recedes in perspective. The floor shows graph paper in the Paper preset and near-black in the Dark preset, and at the stage edges it blends exactly into the surrounding UI colour.
- A dark-green stripe about 3.8 mm tall runs around every side: the bottom shell.
- The feet sit flush or almost flush, so the slab touches the floor. Under it, a tight, almost black contact shadow opens into a broad soft penumbra toward the player. There is no gap and nothing floats.
- **Dark preset:** the multiply shadow has nothing to darken, so contact is carried by a faint floor reflection and a rim light on the shell. The slab must never look as if it hovers.

**Body shape.** The upper third (port strip, label plate, speaker, screen) stands about 3 mm above the key deck. Its front step wall is **edge-lit emerald acrylic**: the window's front edge glows green and casts a faint green spill onto the first row of keys.

**Side walls.** The +X wall, which the hero camera sees, shows the dark port circles near the top edge and near the front. The −X wall carries the embossed battery "×4" icon.

**Lights**
- A large soft key light from the upper front-left.
- A long, thin strip light behind and to the right. It draws a bright line along the top fillet of the chassis and the back edge of every keycap. This is the cue that makes the caps read as moulded solids.
- A dim fill light keeps the shadow side cream rather than grey.

**Materials**
- **Plastics** are PA66 satin. Highlights are broad and dim and roll across the deck as you orbit.
- **Orange (#FA5A16) and green (#0A4A36)** stay saturated in shadow and never turn brown.
- **Nothing is metallic.**
- **The screen window is the only truly glossy surface.** A long diagonal streak crosses it, as in TE's own drawing.

**Screen**
- The icons sit 0.6 mm below the glass and visibly shift against the frame as the camera moves.
- Unlit icons are dark green-black ghosts. Lit icons glow in TE's palette, with a soft halo inside the acrylic and a faint bloom.
- The three alphanumeric tempo digits shine pale mint in their darker box.

**Keys**
- Crisp, slightly tapered 16 mm squares standing 3.2 mm proud.
- Flat tops, with soft 0.7 mm edges that catch a line of light.
- Each sits in a near-black hairline aperture.

**Legends.** Every legend is a laser-engraved decal: flush, sharp at any zoom, and slightly rougher than the plastic around it. That covers:
- TE's chunky rounded numerals;
- the real drum, bass-headstock, keys and ball illustrations on the beige group pads;
- the RIDDIM wordmark;
- the full deck silkscreen.

**Knobs.** Small towers: a coloured disc, a frosted stem that glows faintly where it is backlit, and a flat coloured cap. VOLUME is white, X is orange and Y is dark green. There is no pointer.

**On a pad strike**
- The key drops its fixed travel in about 20 ms and settles with a barely perceptible overshoot on release.
- A Halo cue (D3) flares and fades over about 150 ms; its brightness follows velocity.

**Speaker.** Rounded cream ribs over a dark cavity with a grey cone.

**Stability and cost.** Nothing shimmers while orbiting and no gradient bands. When nothing changes, the stage renders no frames.

**Fit test.** Viewed straight down in unlit albedo, the model registers to TE's front SVG to within 0.5 mm.

---

## 2. Reference facts that matter

**Sources.** TE's EP-40 guide drawings, vendored to `reference/te-ep40/` (§4.1):

| Drawing | Scale and use |
|---|---|
| Front `69033ff84b343523886b5edb.svg` | **0.6091 mm per unit.** Outline 289.0 × 394.0 units; 394/289 = 1.3633, matching 240/176. 70 `<path>` elements and no `<text>`, `<use>` or `<clipPath>`. Compound paths of up to 4,133 subpaths, 53 stroked outlines, one luminance `<mask>`, one `evenodd` fill. |
| Sides `6903405a189dd72cc91154ef.svg` (left), `6903405ad2b3ad2c2f5de575.svg` (right) | **0.600 mm per unit** |
| Top edge `6903449b7bfa382ccab2ae95.svg` | Every Z height on the port face |
| Screen layout `69021d87c95e942b7d172721.svg` | `mm = 12.2 + (svgX − 31.4) × 0.184` |
| TE top-down render, photos | **Human review sheet only.** A lit marketing image with an unknown lens is not a measurement source. |

**Provenance rule.** Every number in `spec.py` carries a tag:
- `svg-path`: measured from path coordinates with `svgelements`. Required for every height before M5a closes.
- `svg-px`: read from a rasterised drawing. Provisional only.
- `owner`: from O1.
- `estimate`.

`lint.py` reports any `svg-px` or `estimate` value that is still left on a height.

### 2.1 Frames

| Frame | Convention |
|---|---|
| Reference (drawings) | x = mm from the left edge; y = mm from the port edge toward the player; z = mm above the bottom face, feet excluded |
| Model (hmesh) | Z up, metres, origin at the footprint centre, ports at +Y. The same frame as today. |
| Mapping | `X = x − 88.0`, `Y = 120.0 − y`, `Z = z − 8.0` (mm) |

**Heights.** Provisional values until M5a replaces them with `svg-path` measurements:

| Surface | Reference z (mm) | Model Z (mm) | Evidence today |
|---|---|---|---|
| Bottom face | 0 | −8.0 | — |
| Feet contact | −0.3 | −8.3 | Side SVGs show a straight green bottom edge, so the feet are flush or at most 0.3 mm proud |
| Bottom shell top (parting groove) | 3.8 | −4.2 | Stripe 3.6–4.0 in the side and top-edge views |
| Key deck top | 12.9 | +4.9 | Step 2.9–3.2 in the side view; provisional 3.1 |
| Upper section and glass top | 16.0 | +8.0 | TE's 16 mm envelope |
| Glass underside | 15.0 | +7.0 | D5 |
| Icon plane (`display_surface`) | 14.4 | +6.4 | D5 |
| Keycap tops | deck + 3.2 = 16.1 | +8.1 | Protrusion 3.2; official travel 3.20 mm |
| Knob tops | deck + 11.45 = 24.35 | +16.35 | 11.9 (top-edge view) and 11.0 (side view); spread recorded |

**`groundZ`** becomes `boundsMin.z` computed **only from parts flagged `castsShadow`**. Anchors and the ring are excluded, because `halo_ring` reaches −9.4 mm today and would make the device float. The same bounds drive the camera fit, with the knobs included.

### 2.2 Chassis and zones

**Footprint and corners.** 240 × 176 mm, portrait, ports on the short top edge. In plan the corners are nearly sharp: about 1.0 mm radius on the vertical edges and about 0.5 mm on the top edges.

**Stepped profile.** The single most important shape fact, and today's model does not have it.

| Layer | Extent |
|---|---|
| Bottom shell | Dark green, z 0 to about 3.8, all round, with a 0.2 mm V parting groove at its top |
| Upper section | y 0 to 104.4, top at z 16.0 |
| Key deck | y 104.4 to 240, top at z about 12.9, with a vertical step wall at y 104.4 |
| Keycaps | Top at deck + 3.2 |

**Top-face zones**

| Zone | x | y | Notes |
|---|---|---|---|
| Port strip | 0–176 | 0–8.6 | Coloured tab inserts that wrap over the edge |
| Label plate | 0–119.8 | 8.6–56.5 | 4 faint screw rings, 4.9 mm diameter, at (3.8, 12.4), (115.6, 12.4), (3.8, 52.3), (115.6, 52.3) |
| Speaker block | 119.6–176 | 8.6–56.5 | Cavity 40 mm diameter at (147.6, 32.5); about 15 half-round ribs at about 3.2 mm pitch across the full block (count and pitch from `svg-path` in M6); cone behind |
| Mic | — | — | 1.2 mm pinhole at (132.4, 52.0) |
| Screen band | 0–176 | 56.5–104.4 | Window x 2.4–173.6, y 58.0–102.4, inside a near-black green frame. Screws 2 mm at (88, 58.7) and (88, 100.5) |
| Key deck | 0–176 | 104.4–240 | — |

**Side walls (`chassis_edgeBand` details).** The default yaw of 28° shows the +X wall, so these are in M5a's acceptance:
- 3 + 2 dark circles on the cream wall, near the ports and near the front, on both walls. Positions and diameters from `svg-path`.
- An embossed battery "×4" icon with an arrow, on the −X wall: a 0.15 mm emboss plus a decal.
- A raised dark bump about 1 mm proud at y ≈ 19–44 mm in the **left** view only. M5a checks the back and top-edge SVGs. It is built on −X if confirmed there, and dropped with a note in `spec.py` if the views conflict.

**Back.** Four orange feet, 4.5 mm diameter, in square bosses about 13 mm across, centred about 9.5 mm in from each corner. Flush to 0.3 mm proud. No other underside detail is built (§10).

### 2.3 Key grid

Cells are **16.0 × 16.0 mm at a 23.94 mm pitch**.

| | col0 | col1 | col2 | col3 | col4 | col5 | col6 |
|---|---|---|---|---|---|---|---|
| cell left x | 7.9 | 31.7 | 55.7 | 79.7 | 103.6 | 127.6 | 151.5 |
| model centre X | −72.1 | −48.3 | −24.3 | −0.3 | 23.6 | 47.6 | 71.5 |

| | row0 | row1 | row2 | row3 | row4 |
|---|---|---|---|---|---|
| cell top y | 112.2 | 136.3 | 160.3 | 184.2 | 208.2 |
| model centre Y | −0.2 | −24.3 | −48.3 | −72.2 | −96.2 |

| | col0 | col1 | col2 | col3 | col4 | col5 | col6 |
|---|---|---|---|---|---|---|---|
| row0 | VOLUME knob | SOUND / EDIT | MAIN / COMMIT | TEMPO / LOOP | – | X knob | Y knob |
| row1 | KEYS (small) | DRUM (`group_a`) | 7 | 8 | 9 | SAMPLE / CHOP | TIMING / CORRECT |
| row2 | FADER (small) | BASS (`group_b`) | 4 | 5 | 6 | FX / OUTPUT | ERASE / SYSTEM |
| row3 | fader | MELODIC (`group_c`) | 1 | 2 | 3 | − | + |
| row4 | SHIFT (small) | SFX (`group_d`) | . | 0 | ENTER | RECORD | PLAY |

**Full keys**
- Base 16.0 mm, tapering to about 15.1 mm at the top.
- 3.2 mm above the deck.
- Plan corner radius about 1.5 mm; top fillet about 0.7 mm; flat top.
- Each sits in a **0.6 mm near-black gap** (#1C1B19).

**Function cells.** These are not two keys. Each 16 × 16 aperture holds:
- an upper **short cap**, about 15 × 7 mm and 3.2 mm tall, carrying the main label;
- below it, a **flat printed plate**, about 16 × 8 mm at deck + 0.2 mm, carrying the shift label.

Cell colours (COMMIT and CHOP orange, LOOP and CORRECT green, and so on) live in `spec.py` and are checked against `c_keys.png`.

**Small keys.** 15.6 × 7.9 mm at x 8.1–23.7:
- KEYS at y 136.6–144.5
- FADER at y 152.4–160.3
- SHIFT at y 216.4–224.3

**Knobs (all three built the same way)**
- Base disc 16 mm × about 2.4 mm.
- Frosted stem 8.1–8.4 mm diameter (front view against top-edge view; `svg-path` decides in M6), from 1.8 mm to about 9.4 mm above the deck.
- Coloured cap up to about 11.45 mm above the deck (spread 11.0–11.9), flat with a soft edge, **no pointer**.
- Centres: (15.8, 120.4), (135.6, 120.4), (159.6, 120.4).
- A dark collar at the foot of the stem on X.

**Fader**
- Slot about 3 mm wide at x ≈ 15.7, y 168.6–208.2.
- Cap: a **vertical** white cylinder (axis +Z), 7.9 mm diameter, standing about 9 mm above the deck. It **travels along model ±Y**.
- Printed centre tick, 13.7 mm wide, at y ≈ 188.4.

**LEDs**
- 1.8 mm, flush.
- Rows at y 132.0 / 156.0 / 179.9 / 204.1; columns at x 35.9 / 59.9 / 84.0 / 108.0.
- Two extra LEDs at (132.2, 204.1) and (140.1, 204.1).
- The procedural top-row lamps (`status_lamp_top_*`) do not exist on the device and are removed.

### 2.4 Ports (top edge, left to right)

| Port | x | Detail |
|---|---|---|
| OUTPUT | tab 7.9–23.9, jack at 16.2 | White tab |
| INPUT | tab 39.8–55.8, jack at 48.2 | Orange tab |
| SYNC / MIDI | **one** tab 63.7–103.7 | Dark green; 4 × 3.5 mm jacks at 72.2 / 80.2 / 88.0 / 96.0; I/O ticks |
| USB-C | tab 127.6–143.6, port at 136 | Cream tab; stadium opening 8.9 × 2.6 mm |
| POWER | printed label 143–175 | **No tab.** A stadium recess about 10 mm wide at x ≈ 155–165 on the edge face holds an orange slider about 4.6 mm wide at x ≈ 160–165. In the front view the slider stands about 4–5 mm proud of the edge. |

Jack rims are 6.5 mm across with 3.6 mm bores. Every jack-centre height and the slider's proud distance come from `svg-path` on `6903449b…` in M6.

### 2.5 Colours and materials

All are albedo values in sRGB. The compiler stores them as linear. Metallic is 0 everywhere.

| Part | Hex | Roughness | Notes |
|---|---|---|---|
| Chassis, deck, port strip | **#DCD2BF** | 0.62 | PA66 MT grain, strength 1.0 |
| Label plate | **#ECEBE6** | 0.40 | Grain 0.3 |
| Off-white keys and plates | **#E9E9E5** | 0.45 (tops 0.42, sides 0.50) | Grain 0.6 |
| Group pad face | **#DDD3C2** | 0.45 | |
| Dark green plastic | **#0A4A36** | 0.45 | Shell, PLAY, LOOP, CORRECT, TIMING, Y knob, SYNC tab |
| Orange plastic | **#FA5A16** | 0.45 | INPUT, COMMIT, SAMPLE/CHOP, RECORD, X knob, slider, feet |
| Brand ink (print) | #00503A | +0.08 over host | Engraved |
| Orange ink | #FF681D | +0.08 | SUPERTONE, X, GAIN, ENTER |
| Aperture gap and liners | #1C1B19 | 0.70 | |
| Knob stems | #EDEBE3 | 0.30 | Frosted; back-transmission |
| Screen window | **#149A78** | 0.05 | Clearcoat; F0 0.04 (acrylic, n = 1.49) |
| Screen frame | #0B2E24 | 0.15 | Glossy dark |
| Icons off / digit box | #174A3A / #2F7D63 | — | LCD branch |
| LEDs | off #3E3A35, on #F0461E | 0.35 | Emissive when on |
| Speaker cavity / cone | #1E211E / #4A4A46 | 0.8 / 0.6 | |

**Screen palette.** Icon lit colours are red #DB3C27, orange #D85B29, blue #5394B5, green #30895E, beige #E2C09F and grey #CCC4C0. The digits use mint #E8FFF4. Off digit segments are #194824.

**Digit segments.** The ghost digits in `c_screen.png` look 16-segment (split top and bottom bars). M9 counts the segment paths in `69021d87…svg` before freezing the `DISP` layout, which then has 3 × N segments + 3 decimal points.

### 2.6 Legends

All small legends are uppercase light geometric sans, about 2.0–2.3 mm cap height, widely spaced, laser-engraved.

| Legend | Position (reference mm) | Ink |
|---|---|---|
| RIDDIM | x 8.5–45.1, y 16.2–28.2 | Green; from the SVG |
| SUPERTONE | x 7.9–35.5, cap height about 4.1 mm | Orange |
| ORIGINAL LAYERING MACHINE | x 7.9–76.3 | Charcoal |
| VOLUME | centred at (16.2, 108.0) | Green |
| LIVE bracket | x 40 to 64.4 at y 108, with drop ticks | Green |
| BPM ──── METRONOME | line from TEMPO (x 88) to BPM (x 136.4), continuing to METRONOME (x 160.2) | Green |
| GAIN | vertical, reading upward, x 124.2, y 125–133, bracket to SAMPLE | Orange |
| SWING | vertical, x 172.3, bracket to TIMING | Green |
| X / Y | about 5.3 mm tall, y ≈ 132.9 | X orange, Y green |
| LED labels | above 7/8/9: LEVEL PITCH TIME; 4/5/6: LPF HPF →FX; 1/2/3: ATK REL PAN; ./0/ENTER: TUNE VEL MOD | Green |
| Port labels | OUTPUT, INPUT, SYNC, MIDI, USB, POWER, plus I/O marks | Per tab |

**Pad faces**
- **Digits:** a chunky rounded custom face, about 60% of key height. The counters in 8 and 0 depend on the `evenodd` fill rule.
- **"." pad:** a 4 mm orange rounded square at top-left.
- **ENTER:** orange text.
- **RECORD and PLAY:** white text.
- **− and +:** green symbols.
- **Group pads:** the conga, the bass headstock, a 2×4 grid of piano-key bars, and an orange ball with two white dots.

**Asset errors today.** `legend_26` reads "DUB" where the device says "BPM". The legends float 1.65 mm above the plate and the lamps 1.3 mm. M1 fixes all three.

---

## 3. Renderer upgrades

**Target API: OpenGL 3.2 core / GLSL 150.** Two features come from extensions, both guarded: anisotropic filtering and timer queries.

### 3.1 Stage-owned look (`Source/Stage/StageLook.h`, new)

```cpp
struct StageLook {
    juce::Colour backdrop, floor, shadowTint;
    std::array<juce::Colour, kRingStateCount> ring;    // one per RingState value
    float exposure = 1.0f; ToneMapper toneMapper = ToneMapper::neutral;
    float keyIntensity = 1.0f, rimIntensity = 1.0f, fillIntensity = 1.0f;
    float floorReflection = 0.0f;                       // 0 = off (Paper), ~0.15 (Dark)
    juce::Image floorTexture;                           // by value; graph paper by default (O2)
    static StageLook paper();                           // default, today's values
    static StageLook dark();                            // OP-XY near-black
};
void StageComponent::setLook (const StageLook&);       // copies under the SpinLock; GL thread uploads; one repaint
```

- `paper()` reproduces today's graph-paper values, so with default tokens M1 changes nothing visually apart from the intended fixes.
- The redesign calls `setLook()` from its theme.
- Device colours are not part of `StageLook`.

### 3.2 Frame graph (final state)

```
Cached, re-rendered only on frames where a slot moved:
   key ShadowMap 2048² D24; deck shadow cache R8 (model XY, 8 px/mm)
On display change:  LcdState texture via glTexSubImage2D (no reallocation)

Per drawn frame:
 1. Depth prepass                          opaque range, 1 draw
 2. Main HDR pass                          RGBA16F MSAA 4×: premultiplied device radiance, α = device coverage
                                           + RGBA16F MSAA floor target: rgb = reflection, a = shadow
                                           opaque draw → glass draw → [optional mirrored floor draw, M8]
 3. Custom resolve                         per-sample 1/(1+luma) weighting (bright edges stay anti-aliased)
 4. Bloom (M9)                             half-res, 5 levels, 13-tap down (Karis on mip 0) / 3×3 tent up, no threshold, mix ≈ 0.04
 5. Composite
    device:  unpremultiply (α > 0) → × exposure → tone map → re-premultiply
    floor:   base = linear(floor token or floor texture) × mix(1, shadowTint, shadow)
             light = pool(SDF, RingState) + reflection + bloom
             floor = base + (1 − base) · T(light)       // T = active tone curve; light = 0 leaves base exact
    out = device over floor (bloom over the device uses the same screen blend)
    → exact piecewise sRGB OETF
 6. FXAA                                   only when the granted GL_SAMPLES < 4, or on the Low tier
 7. Dither                                 ±0.5 LSB blue noise, always last
```

**Backdrop compositing.** The floor is a real perspective plane: it writes shadow and reflection terms in pass 2, and the pool is computed analytically in composite. Where no light falls, its colour is exactly the token, so the stage border equals the UI colour.

**MSAA ownership.**
- The JUCE default framebuffer stops requesting MSAA (`StageComponent.cpp:153-156`); the stage owns its MSAA.
- `GL_SAMPLES` is read back from the stage FBO and logged, along with `GL_RENDERER` and `GL_VERSION`.

**Resizing.**
- Render targets are **grow-only**, and the frame renders into a viewport sub-rect.
- A larger target is reallocated only once a resize has settled for 200 ms. Until then, the frame renders at reduced scale into the existing targets.

### 3.3 Shading

This replaces `skyGradient`, `softbox` and the analytic env-BRDF (`StageShaders.h:181-236`).

**Direct light.** Cook-Torrance GGX with height-correlated Smith visibility and Schlick Fresnel, for the key softbox and the rim strip.
- Each light is treated as an area light by widening roughness: `α' = clamp(α + size/(2·dist))`.
- Diffuse is Lambert × (1 − F).

**Image-based lighting** from `assets/stage/studio.henv`:
- prefiltered GGX cubemap sampled with `textureLod(env, R, rough × maxMip)`, with seamless filtering;
- a 64² RG16F split-sum lookup table generated once at startup;
- multi-scatter energy compensation;
- SH9 irradiance for diffuse;
- a runtime SH9 floor-bounce term built from the floor token on every `setLook` (§3.4);
- Lagarde specular occlusion, so the IBL does not light up key gaps.

**Specular anti-aliasing.** Kaplanyan-Hoffman: α² is widened by the screen-space normal variance from `dFdx`/`dFdy(n)`, capped at 0.18.

**Per-material extras,** read from the material texture (§5):
- Glass: clearcoat lobe at roughness 0.04, blended `GL_ONE, GL_SRC_ALPHA`.
- Frosted stems: wrapped diffuse plus thickness transmission (0.35).
- Orange and green caps: back-transmission 0.15, active only while their aperture cue is lit.

**Normals.** Rotated by the slot quaternion in the vertex shader (D8). There is no `uNormalMatrix` and no squash.

**Ambient occlusion.**
- Baked vertex AO, or the deck AO texture on faces with `n.z > 0.5` that belong to the deck.
- Multiplied by the analytic contact term (D10): `1 − k·exp(−sdfAperture / 1.5 mm)·travel01` for apertures within 25 mm of a moving slot. At most 4 such apertures are passed as uniforms.

**Removed:** per-fragment tone mapping (`StageShaders.h:287,308,323`). The LCD and ring paths move into HDR.

**Tone curves.**
- The curve constants live in one shared file, `Source/Core/ToneCurveConstants.inc`, written with `HALO_TC(name, value)` macros.
- C++ includes it as `constexpr`. The shader loader expands the same file into GLSL `const float` lines, so both sides use one set of numbers.
- `ToneCurvesTests` tests the C++ only.
- A Debug-only self-test renders a 64-px ramp through the GLSL curve, reads it back and compares it with the C++ within 1e-3.

### 3.4 Studio environment (`tools/model/ep40/env_bake.py`)

Generated with numpy only, deterministically, using a Hammersley sequence. The bake is **neutral**: no floor bounce is baked in, because the floor colour is a runtime token.

**Scene:**

| Element | Placement | Radiance |
|---|---|---|
| Dark warm room | — | 0.03 |
| Key softbox | 0.9 × 0.6 m, upper front-left | 6 |
| Rim strip | 1.0 × 0.12 m, behind-right at about 20° elevation | 10 |
| Fill card | front-left | 0.8 |

Every source has a soft gradient falloff at its edges. The rim strip is what produces TE's diagonal streak on the glass.

**Output: `.henv` v1:**
- a 128² cubemap with 6 GGX-prefiltered mips (1024 samples per texel), stored as RGB9E5;
- SH9 irradiance;
- descriptions of the key and rim lights, so the analytic lights and the shadow line up with what the reflections show;
- a CRC.

Size: about 0.5 MB.

**Runtime floor bounce.** A lower-hemisphere SH9 lobe of `linear(floor token) × 0.18` is added to the irradiance on every `setLook`.

### 3.5 Shadows and floor

- **Key shadow:** 2048², **cached**, and re-rendered only on frames where a slot moved.
  - Sampling is PCSS-lite: an 8-tap blocker search, then a 16-tap Vogel disc rotated by a 64² blue-noise texture.
  - The shadow is crisp at contact and soft further away.
- **Deck shadow cache:** the key shadow resolved once into a model-XY R8 texture, so upward-facing deck and plate fragments pay 1 tap instead of 24.
- **Floor:**
  - the analytic footprint SDF (kept, it works well);
  - the cached shadow;
  - the ring pool: `ring[state] × level × exp(−max(sdf − 2 mm, 0) / 18 mm)`, with a dashed angular mask while discovering. The mask advances only on frames that are already drawn (D13).
  - The pool is hidden when disconnected.
- **Floor reflection (M8, optional, on in the Dark preset):**
  - a second draw of the opaque range, mirrored about `groundZ`;
  - written to the floor target's rgb, faded by distance from contact (e-fold 20 mm) and by the floor's roughness;
  - `StageLook::floorReflection` scales it.

### 3.6 Camera (`Source/Core/StageCamera.{h,cpp}`, pure)

- **Fit to bounds:**
  - Project the 8 corners of the `castsShadow` bounding box, knobs included.
  - Binary-search the radius to fit both FOV axes with a 6% margin, then re-centre the look-at target.
  - This fixes the crop in `shot-user-rack.png` and the under-fill in `shot-merged.png` (about 45% today).
- **Clamps:** radius 0.55–1.6 × the fitted value; pitch 12–85°.
- **Feel:**
  - orbit inertia, with velocity decaying at τ 120 ms;
  - double-click eases back to the hero view (yaw 28°, pitch 38°) over 400 ms;
  - presets `hero`, `top` (orthographic plan), `low`, `keys`, `edge`;
  - showcase drift (O4).
- **No depth of field.**

### 3.7 Textures

- **Anisotropic filtering** at 8× on every decal and AO texture (`GL_EXT_texture_filter_anisotropic`, guarded).
- **Mipmaps** on every atlas. SDF atlases get mips built by the compiler, not by `glGenerateMipmap`.
- **Loader:** `.htex` files go through `Source/Core/HtexFormat.{h,cpp}`. The format holds explicit channel semantics, CRC and mips, and is uploaded with no premultiply and no BGRA swizzle.

### 3.8 Grain

Grain is distance-gated so it never sparkles and never costs anything at hero distance:
- **Below 8 screen px per mm**, including hero distance at about 4 px/mm, each material uses a constant roughness that already includes the grain's average widening. There are no grain taps.
- **From 8 to 16 px/mm**, the triplanar grain normal and its roughness delta fade in.
- The grain mips use Toksvig widening, `r' = sqrt(r² + (1−|n̄|)/|n̄|)`, built by the compiler.

### 3.9 Coexisting with JUCE

The stage uses `setComponentPaintingEnabled(false)` (`StageComponent.cpp:159`) inside a native GL child window, so **no JUCE component can draw over the stage**.
- Redesign overlays, including the `stageClip` treatment in `docs/redesign/mockups/hybrid-play.svg` and the `opxy-*` mockups, must either sit beside the stage or be drawn in GL by the stage.
- This rule is recorded in the `StageLook.h` header comment and handed to the redesign effort.
- Turning component painting on would cost a full texture composite every frame and is rejected.

---

## 4. Asset upgrades: the model compiler

### 4.1 Layout and dependencies

```
tools/model/
  build_ep40.py            python -I tools/model/build_ep40.py --out assets/stage [--artwork te|neutral] [--check]
  requirements.txt         pinned from tools/.venv (pip freeze)
  usdz_to_hmesh.py         M1 fixes; from M4 writes v3; later only --contract-check
  hmesh_io.py, htex_io.py  readers/writers shared by the compiler and the tests
  ep40/
    spec.py                every measured number and colour, each with a provenance tag
    frames.py              reference mm <-> model metres
    svg_measure.py         path-coordinate measurement helpers (svgelements)
    geom/   sagitta.py prims.py poly.py extrude.py normals.py tri.py
    parts/  chassis.py sidewalls.py keys.py knobs.py fader.py speaker.py ports.py display.py leds.py feet.py anchors.py
    artwork/ svg_import.py raster.py inks.py sdf.py atlas.py neutral.py
    materials.py ao.py grain.py env_bake.py lint.py compare_reference.py
  tests/                   python -I -m unittest discover tools/model/tests
reference/te-ep40/         SVGs + the crops tests read + README (URLs, scales); photos not in git
```

**Dependencies.** M0 runs `pip download --only-binary=:all:` against the Python 3.14.8 venv for the packages below and pins what resolves.

| Package | Used for | If no wheel |
|---|---|---|
| numpy | Everything | Required |
| svgelements | SVG paths and measurement | Pure Python |
| Pillow | Poster PNG and review sheets | Not installed today; M0 adds it |
| scipy | `distance_transform_edt` for SDFs | Felzenszwalb in numpy |
| shapely ≥ 2.1 | Polygon offset for fillets and holes, `constrained_delaunay_triangles` | numpy ear clipping plus grid refinement |
| fontTools | Neutral-mode glyphs | Neutral mode disabled |
| usd-core | Contract check | Already installed |

**Rasteriser.** A numpy scanline rasteriser with 4×4 supersampling. It honours `nonzero` and `evenodd` across every subpath of a compound path.

**Reference vendoring.** The scratchpad references total about 51 MB.
- **Committed:** the 8 TE SVGs (3.8 MB) and the PNG crops the tests read (`c_keys.png`, `c_knobs.png`, `c_screen.png`, `c_top.png`).
- **Kept out of git:** the 2048 px photos and the remaining crops, in the git-ignored `reference/te-ep40/local/`. The README lists every source URL so they can be fetched again.

**Generated assets are committed.** The CMake target `ep40_model` is optional and not part of the default build.

### 4.2 Geometry rules

- **Segment counts** follow the sagitta rule, `n = ceil(angle / acos(1 − e/r))` with e = 0.02 mm. That gives about 10 segments per quarter on a 1.5 mm corner and about 64 around a stem.
- **Normals** are analytic from each generator. Every edge gets at least a 0.2 mm fillet. Creases are split only where flagged.
- **Holes without CSG:**
  - The deck top is one polygon with holes: key and small-key apertures, knob seats, the fader slot and 18 LED holes.
  - It is triangulated with constrained Delaunay plus a 2 mm interior grid (D10) and has a filleted outer edge.
  - Each aperture has a liner, reusing the `*_well` names: 4 dark walls plus a floor, 3.5 mm deep.
  - Jack, USB-C, power-recess and mic pockets are built the same way: rim, bore, dark floor.
- **Nothing coplanar and nothing floating:**
  - LEDs sit 0.05 mm below the deck inside their holes.
  - The icon plane sits 0.6 mm under the glass.
  - All text is decals.
  - Lint enforces both rules (§4.6).

### 4.3 Part map

**The contract** is the 43 part names plus `ep40_root` in `reference/halo-mac/Halo/Reality/EP40EntityNames.swift`. It is ported verbatim to `Source/Core/Ep40EntityNames.h`. Extra parts may be added or renamed freely (DD-006e).

| Name | New geometry |
|---|---|
| `chassis_base` | Green shell, z 0 to about 3.8; vertical corners r 1.0; bottom fillet 0.5; 4 square foot bosses |
| `foot_0..3` *(new)* | Orange, 4.5 mm diameter, flush to 0.3 mm proud |
| `chassis_edgeBand` | Cream walls from the shell top to z 16.0 (y < 104.4) and to the deck (y ≥ 104.4); V parting groove at the shell top; 0.5 mm top fillet; side-wall details from §2.2 |
| `chassis_topPlate` | Port-strip top plus key deck with all holes |
| `brand_panel` | Label plate, 0.15 mm proud in a 0.25 mm groove; 4 embossed screw rings. All lettering moves to decals, which removes 13.5k triangles. |
| `speaker_panel`, `speaker_grille` | Half-round ribs across the block (crests z 16.0, grooves about 1 mm deep), clipped open over the 40 mm cavity |
| `speaker_cone` *(new)* | Cone, surround (torus) and dust cap |
| `mic_port` | 1.2 mm pinhole pocket, 2 mm deep |
| `display_bezel` | Frame #0B2E24, top at z 16.0, running down both side walls |
| `display_glass` *(new)* | Window insets 2.4 / 1.5 / 2.4 / 2.0 mm; **1.0 mm thick** (z 15.0–16.0) with visible inner walls |
| `display_edge` *(new)* | The step wall at y 104.4, with an emissive green gradient |
| `display_screw_0/1` *(new)* | 2 mm dark heads |
| `display_surface` | Icon plane at **z 14.4**; UV 0..1 spans the window |
| `pad_*` (12), `group_a..d`, `button_minus/plus/record/play` | Full keycap: 16.0 → 15.1 taper, 3.2 mm tall, plan corner r 1.5, 0.7 mm top fillet (8 segments). Pivot at the base centre, axis −Z, travel `kKeyTravelMm` (O5). |
| `button_sound/main/tempo/sample/timing/fx/erase` | Short cap, 15 × 7 × 3.2 mm, in the upper part of a 16 × 16 aperture |
| `button_*_secondary` (7, kept) | Flat printed plate, 16 × 8 mm, at deck + 0.2 mm, in the same aperture. One well per cell, not two. |
| `button_keys`, `fader_mode_button`, `button_shift` | Small keys, 15.6 × 7.9 mm |
| `knob_volume/x/y` | Disc, frosted stem, flat cap; dark collar on X |
| `fader_track`, `fader_cap` | 3 × 40 mm slot liner; vertical 7.9 mm cylinder about 9 mm tall; slot axis +Y |
| `ports_strip`, `port_*`, `ports_usb`, `port_power_switch` | Tabs per §2.4 with wrap-over; jack, USB-C and mic pockets; SYNC/MIDI as one block; no POWER tab; power recess plus slider |
| `led_<pad>` ×16, `led_aux_0/1` | Replace `status_lamp_*`. Flush 1.8 mm discs. `ledFor` holds the pad index. All unlit in the asset. |
| `*_well` | Aperture liners, #1C1B19, with a 0.6 mm visible gap; **not raised** |
| `legend_25..29` | Deleted in M1; their text is printed by `FacePrint` until M7 |
| `halo_ring` | Anchor only: zero triangles, not `castsShadow`. Its bounds and corner radius describe the footprint. |

### 4.4 Triangle plan (target about 90k, cap 150k)

| Group | Triangles |
|---|---|
| Deck and top surfaces with holes (constrained Delaunay plus 2 mm grid) | 20k |
| Sides, shell, fillets, groove, feet, side-wall details | 12k |
| Keycaps: 20 × 650, 7 × 550, 3 × 550 | 18.5k |
| Liners | 3k |
| Knobs (3 × 3k) | 9k |
| Fader | 1.5k |
| Speaker ribs, cavity and cone | 12k |
| Ports and power recess | 6.5k |
| Display: frame, glass, edge, screws | 3k |
| LEDs | 1k |

### 4.5 Decals, AO and LCD atlas

**Artwork import (D9)**
1. `svg_import.py` flattens every path into reference mm, applying transforms. It **discards** every stroked path, the masked glare group, and outline rings and borders (identified by stroke, or by the "ring" and "border" lists in `spec.py`).
2. `inks.py` groups the filled paths by nearest palette ink. `raster.py` rasterises each ink layer as a whole, using each path's fill rule, at 32 px/mm.
3. `atlas.py` cuts regions using `spec.LEGENDS`. This is an explicit list of `{legend, ink, reference rect, target part, atlas}` entries. Ink outside every listed rectangle fails `--check` and is written to an overlay PNG for review.
4. `sdf.py` builds signed distance fields from the coverage, with a range of ±8 texels at atlas resolution.

**Four-ink SDF texture, `.htex` RGBA8, no premultiply.** Each channel is a signed distance: 0.5 = edge.

| Channel | Ink |
|---|---|
| R | Green #00503A |
| G | Orange #FF681D |
| B | White #F4F2EC |
| A | Charcoal #2B2B28 |

Inks are layered in a fixed order. Coverage is `clamp(d/fwidth(d) + 0.5, 0, 1)`.

**Atlases**

| Asset | Size | Density | Covers | Arrives |
|---|---|---|---|---|
| `assets/stage/ep40_keys_sdf.htex` | 2048² | about 16 px/mm | Cap tops, secondary plates, tab inserts | M5b |
| `assets/stage/ep40_face_sdf.htex` | 2048 × 2816 | about 11.7 px/mm | Deck silkscreen, label plate, port strip, fader tick, side-wall battery icon | M7; replaces `FacePrint.h` |
| `assets/stage/ep40_deck_ao.htex` | 1024 × 1408 R8 | about 5.9 px/mm | Model-XY AO for the deck and plate | M8 |
| `assets/stage/display_icons_sdf.htex` | 2048 × 512 | — | Icon SDFs (R) and icon or segment IDs (G, sampled nearest) | M9 |

**Decal UVs** are written into vertices by the compiler:
- cap tops: planar projection normalised to the cap's own top rectangle;
- deck and plate: model XY mapped onto the face atlas;
- only faces with `n.z > 0.5` sample a decal, except the side-wall icon, which uses its own planar projection.

**Engraving response:**
- roughness +0.08 where ink is present;
- a slight normal bend from the SDF gradient within 0.05 mm of an ink edge, enabled only above 8 px/mm.

**AO bake (`ao.py`, D10).**
- 256 hemisphere rays per vertex against the rest-pose mesh, using a numpy BVH or shapely-accelerated ray casting. The result goes into the `u8 ao` byte of each vertex.
- The deck AO texture is ray-cast per texel.
- Moving parts bake at rest; their contact term is analytic.

**PA66 grain** (`assets/stage/pa66_grain.htex`, 512² RGBA8, with Toksvig mips):

| Channel | Content |
|---|---|
| RG | Surface-gradient derivatives (no tangents needed) |
| B | Roughness delta, ±0.04 |
| A | Cavity |

The bumps are Gaussian, 40–90 µm, tiling every 6 mm.

**Plan poster** (`assets/stage/ep40_plan.png`, opaque RGB). Captured in-app by `stage_shots.ps1 -Poster` from the `top` orthographic preset, using the High tier with AO. It replaces `plan_view.py`, and the Still tier uses it.

### 4.6 Lint and reference comparison (`--check`; a non-zero exit fails)

**`lint.py`**
- All 43 contract names are present.
- **Dimensions:**
  - footprint 176.0 × 240.0 ± 0.2 mm;
  - each height within ±0.1 mm of its `spec.py` value;
  - every height tagged `svg-path` or `owner` from M5a on.
- **Floaters:** every part's lowest vertices are within 0.03 mm of a supporting surface (downward ray cast).
- **Interference:** at the maximum tunable travel (3.2 mm), no keycap crosses its liner.
- **Z-fighting:** no overlapping coplanar faces with different materials within 0.02 mm. Glass underside − icon plane ≥ 0.3 mm.
- **Bounds:** `boundsMin.z` is computed from `castsShadow` parts only, and anchors carry zero triangles.
- **Mesh hygiene:** no triangle under 1e-4 mm², outward normals, no deck triangle with an aspect ratio above 12.
- **Decals and textures:** every decal UV inside its atlas rectangle; every `spec.LEGENDS` entry non-empty; no ink outside the listed rectangles.
- **Budgets:** per-group triangle budgets met.
- **LEDs:** `led_*` `ledFor` values are valid.

**`compare_reference.py`**
- The reference is the **front SVG**, rasterised at a known scale, and is registered by the four chassis corners. This can be exact because the SVG is vector and to scale.
- Geometry inputs:
  - the compiler's own top-down orthographic albedo raster of the spec;
  - the in-app capture of `HALO_STAGE_DEBUG=7` (unlit albedo, `top` preset).
- It reports:
  - aperture-edge offsets (mean and maximum);
  - CIEDE2000 colour difference per zone, albedo against SVG fill;
  - a heat map at `build/model/compare.png`.
- TE's lit render and the photos appear only on the human review sheet.

---

## 5. Format: `.hmesh` v3

**Container.** Little-endian, 4-byte aligned, chunked. Unknown chunks are skipped.

```
Header 48 B: "HMSH" u32 version=3 u32 flags u32 chunkCount f32[3] boundsMin f32[3] boundsMax
             u32 crc32(all chunk bytes) u32 reserved     // bounds over castsShadow parts; boundsMin.z = ground contact
Chunk table: chunkCount × { char[4] id; u32 offset; u32 size }
'MATL' n × 64 B: char[16] name; f32[3] baseLinear; f32 rough; f32 metal; f32 clearcoat;
       f32 clearcoatRough; f32 transmission; f32 thicknessMm; f32[3] emissiveLinear;
       f32 grain; u32 flags (glass, lcd, led, apertureEmitter, decalTarget, edgeEmitter)
'TEXR' n × { u8 role; u8 format; u16 w; u16 h; u16 nameLen; char name[] }   // HaloAssets resource names (.htex)
'PART' n × { u16 nameLen; char name[]; u8 kind; u8 flags; u8 slot; u8 material;
       f32[3] pivot; f32[3] axis; f32 travelM; f32[6] bounds;
       u32 firstIndex, indexCount, baseVertex, vertexCount; u16 ledFor; u16 reserved }
       kind:  static pad group button knob fader led display glass anchor speakerCone plate
       flags: castsShadow moving emissive transparent decal
'VERT' 24 B: f32[3] position (model space, rest pose); snorm16[2] normal (octahedral);
       unorm16[2] uv; u8 material; u8 slot; u8 mask (decal|grain|glass|emissive); u8 ao (baked, 255 = none)
'INDX' u32, per-part ranges, drawn with glDrawElementsBaseVertex
'DISP' icon table + digit layout      'META' UTF-8 JSON (compiler git hash, spec version, artwork mode, tri counts)
```

**Vertex attributes.** `material`, `slot`, `mask` and `ao` are fed with `glVertexAttribPointer(..., GL_UNSIGNED_BYTE, GL_FALSE, ...)` as floats and decoded in the shader with `int(x + 0.5)`. There are no integer attributes.

**Material table.** Uploaded at load into a small RGBA32F texture (4 texels per material) and read with `texelFetch`. This keeps uniform space free as materials grow.

**One VBO, one draw.**
- Per-slot transforms are plain uniform arrays in the vertex shader (no UBOs): `uSlotOffset[48]` (xyz), `uSlotRot[48]` (quaternion) and `uSlotPivot[48]`, 144 vec4 in all.
- The vertex shader rotates both position and normal about the pivot.
- Parts are sorted so the glass is a separate trailing range.

**Size.** About 2.2 MB at 90k triangles, against 3.8 MB for v2 today.

**Parser** (`Source/Core/HmeshFormat.{h,cpp}`, in HaloCore, no JUCE):
- Input is `std::span<const std::byte>`; output is a result type.
- It reads v1, v2 and v3. Legacy versions load with neutral defaults:
  - one material per unique colour;
  - slots for `pad_*` and `group_*` assigned by name;
  - bounds computed while excluding `halo_ring`;
  - AO set to 255.
- It rejects:
  - a bad magic, version or CRC;
  - a chunk or count that overruns the buffer;
  - an index ≥ the part's vertexCount;
  - out-of-range material, slot, ledFor or decal values;
  - non-finite floats or inverted bounds.

Today's parser trusts its counts and never range-checks indices (`HaloMesh.cpp:39-56`). `HaloMesh.cpp` becomes a thin wrapper: parse, then upload one VBO, one EBO and one VAO.

**`.htex` and `.henv`** follow the same container, CRC and rejection rules, each with its own parser and tests in HaloCore.

---

## 6. Live-MIDI animation upgrades

**Pure logic lives in `HaloCore`**, so `HaloTests` covers it without GL. That suite links only HaloCore and doctest (`CMakeLists.txt:115-127`).

| Element | Behaviour | Source of truth |
|---|---|---|
| **Pads (12)** | `Core/PadSpring.h`: semi-implicit spring, ω = 2π·26 Hz. Press is ζ 1.0 toward the fixed `−kKeyTravelMm` (2.6 mm by default, O5), whatever the velocity. Release is ζ 0.72, with one tiny overshoot. No squash. Replaces the exponential 1.1–1.3 mm travel (`StageComponent.cpp:16-17,389-413`). | Note on/off |
| **Pad cue** | Default aperture underglow (D3, DD-006g): HDR orange × (0.4 + 0.6·velocity) on the struck pad's liner rim, 8 ms attack and 150 ms decay. Up to 4 diffuse-only point lights, radius 30 mm at aperture height, warm the cap's lower sides and its neighbours very slightly. The alternative (O3) is a row-LED echo. The cap albedo never changes. | Note on/off, velocity |
| **Group pads (4)** | Travel like the pads when observed. The active group shows on the LCD group icon. The flat green tint (`StageComponent.cpp:520-522`) is removed. | Group from note range |
| **Other keys** | Same spring, but only for keys whose presses Halo can observe. Today that is none, since the mapping is notes only. Hooks exist in the slot table. | Confirm what the EP-40 sends first |
| **Knobs / fader** | Not animated. The knobs have no pointer, so rotation would be invisible. `fader_cap` has a slot on +Y, driven only if a CC is confirmed. | Out of scope until CC is confirmed |
| **LEDs (16 + 2)** | Emissive #F0461E × 12 HDR when lit, #3E3A35 when off; 8 ms attack, 120 ms decay. Bound by kind and `ledFor`. Lit only from a mapped, observed field (D4); the pad echo applies only if O3 picks it. | `Core/StageState` LED map |
| **Screen** | `Core/LcdState.{h,cpp}`: `toLcdState(const DisplayState&)` returns per-icon intensities, digit segments and backlight. Details below. | `DisplayState`, `FeedMode` |
| **Edge glow** | `display_edge` emits a green vertical gradient while connected. Deck fragments within 6 mm of y 104.4 receive `green × 0.25 × exp(−d / 2 mm)`. | Connection state |
| **Ring pool** | One colour and level per `RingState`. Steady when linked or monitoring; pulses follow `RingState`'s own recording, transfer and error cadence. Discovering shows a dashed sweep that steps only on preview frames (D13). Off when disconnected. | `RingState` |
| **Speaker cone** | Optional slot of ±0.3 mm, enabled only if the meter source is proven to be the device's output. | Meter provenance |

**Screen mapping.** It lights only what `DisplayRenderer` lights today, translated to TE icons:
- mode, play, clock pulse, active group and meters map to their TE icons;
- **active pad** lights the NUMPAD icon, and the pad itself shows travel;
- today's 12-cell pad grid (`DisplayRenderer.cpp:150-166`) is not carried over.

**Feed provenance** replaces the dot beacon (`DisplayRenderer.cpp:190-201`) with a defined treatment:

| Feed | Backlight | Digits |
|---|---|---|
| **Live** | 0.35 | Tempo |
| **Waiting** | 0.25 | "---" |
| **Preview** | 0.15 | Steady "PRV" |

- The digits are alphanumeric segments, so they can spell "PRV". Preview frames therefore always carry a label on the device itself, and Halo's own PREVIEW label beside the stage stays as it is.
- Lit icons emit in their palette colour, with in-glass halation (coarse-mip coverage × 0.25).
- Off icons are #174A3A at 85% coverage.
- A display change costs one small texture upload.

**The message-thread path shrinks.** The image renders and Gaussian blurs (`DisplayRenderer.cpp:90-107`) and the digit drawing (`:109-148`) leave the hot path. `DisplayRenderer` stays only for the Still tier, and it stops reallocating its highlight image on every call.

---

## 7. Performance budget and repaint

**Hardware.** The RTX 5080 is typical. The AMD Radeon iGPU is the floor: the owner sets Halo.exe to "Power saving" in Windows Graphics settings for those runs. Assumed stage size is about 1.5 MP physical.

**Timing.** All timings come from **Release** builds with `HALO_STAGE_STATS=1`, which reports `GL_TIME_ELAPSED` p50 and p95. M0 records today's renderer on both GPUs as the baseline.

| Item | AMD iGPU | RTX 5080 | How |
|---|---|---|---|
| Shadow + deck cache | ≤ 0.9 ms, only on frames where a slot moved | ≤ 0.15 | Cached; orbiting costs nothing here |
| Depth prepass | ≤ 0.4 ms | ≤ 0.1 | One draw |
| Main HDR pass, 4× MSAA | ≤ 2.8 ms | ≤ 0.6 | About 1 shadow tap on decks, 24 elsewhere; AO is a vertex or texture lookup |
| Resolve + bloom + composite | ≤ 1.5 ms | ≤ 0.4 | Half-resolution bloom |
| **GPU total while animating** | **≤ 6 ms p95** | **≤ 1.5 ms p95** | Stats |
| Render-thread CPU | ≤ 0.8 ms per frame, ≤ 3 geometry draws (+1 with floor reflection), ≤ 16 full-screen passes, no allocations | | `GlProgram` caches uniform locations; today about 770 `glGetUniformLocation` calls per frame |
| Message-thread display update | < 0.1 ms | | `LcdState` POD copy |
| **Idle** (D13) | **0 stage frames** | | `setContinuousRepainting(false)`; one `requestFrame(reason)` funnel |
| Disconnected | ≤ 6 stage frames/s | | Preview demo only |
| VRAM | ≤ 100 MB | | Grow-only targets |
| Embedded asset bytes | ≤ 8 MB | | |
| Startup | parse ≤ 15 ms, upload ≤ 40 ms | | No runtime bake |

**What triggers a repaint** (each one calls `requestFrame(reason)`):
- MIDI pad or clock events
- display changes, including preview frames
- camera drag or inertia
- unsettled springs. A spring counts as settled once |x| < 1e-5 m and |v| < 1e-4 m/s.
- LED, cue and glow decay tails
- `RingState` pulses
- resize or a `setLook` call

### Tiers (`Source/Stage/StageTier.{h,cpp}`)

| Tier | Selected when | Pipeline |
|---|---|---|
| **High** | Default: GL ≥ 3.2 core, all programs link, RGBA16F MSAA FBO complete | Everything |
| **Low** | Float or MSAA FBO incomplete, an HDR program fails to link, or `HALO_STAGE_TIER=low` | LDR forward (today's path, cleaned up): Neutral tone map in the fragment shader, exact sRGB, FXAA, SH-only IBL plus the GGX key, no bloom, LEDs as additive sprites |
| **Still** | `GL_VERSION` < 3.2, `GDI Generic` or the Microsoft Basic Render Driver, a required `juce::gl` entry point is null, no program links, or no completed frame within 3 s | `StagePlanFallback`: JUCE 2D of `ep40_plan.png`, with pad and LED radial glows at known rectangles and the `DisplayRenderer` image in the window rectangle |

**Still-tier guard.**
- In `newOpenGLContextCreated`, before the first shader call, the stage checks `GL_VERSION` and that every GL entry point it uses is non-null.
- To demote, it posts to the message thread, calls `context.detach()` so the native GL child window goes away, and then shows `StagePlanFallback`.
- The stage is never blank. Today a failed `buildProgram` leaves it blank (`StageComponent.cpp:122-145`).

**Logging.** Every demotion is logged to the stage log. Its path is `HALO_STAGE_LOG`, or `%TEMP%\halo-stage-<pid>.log` by default, and it replaces `halo-stage-shader.log`. The PID default keeps a daily instance and a MOCK instance (commit e55a60c) from clashing.

---

## 8. Milestones

### Common harness (built in M0, used by every milestone)

**Build:** `set HALO_BUILD_DIR=build\Redesign` then `scripts\build.cmd`. Tests:
- `ctest --test-dir build\Redesign -C Debug`
- `tools\.venv\Scripts\python -I -m unittest discover tools/model/tests`

**Dev hooks.** `HALO_STAGE_CAMERA` and `HALO_STAGE_DEBUG` are kept.

| Hook | Build | Values / purpose |
|---|---|---|
| `HALO_STAGE_STATS` | Debug + Release | `=1`: fps, GPU p50/p95, tier, frame reasons, every 2 s |
| `HALO_STAGE_LOG` | Debug + Release | Log path; the default is per-PID |
| `HALO_STAGE_TIER` | Debug + Release | Force `high`, `low` or `still` |
| `HALO_STAGE_PRESET` | Debug | `hero`, `top`, `low`, `keys`, `edge` |
| `HALO_STAGE_POSE` | Debug | e.g. `"pad7=1.0,pad5=0.8,group=1,lcd=demo,ring=steady,feed=live"`. **Overrides live and mock pad, display and ring input**, and freezes springs and clock. |
| `HALO_STAGE_LOOK` | Debug | `paper` or `dark` |
| `HALO_WINDOW_SIZE` | Debug | `w,h` |
| `HALO_STAGE_TONEMAP` | Debug | Choose the tone mapper |
| `HALO_STAGE_DEBUG` | Debug | Existing modes, plus 5 = material swatch card, 6 = silhouette mask, 7 = unlit albedo, 8 = emissive only |

**Swatch card (`DEBUG=5`).** Each material's albedo is drawn **unlit**, × exposure 1.0, through the active tone mapper and the OETF. All ΔE targets refer to this card.

**Shots with `tools/dev/stage_shots.ps1`.** For each shot it:
1. sets `HALO_MOCK_MIDI=1`, the hooks and a unique `HALO_STAGE_LOG`;
2. starts the built exe with `-PassThru`;
3. waits for `HALO_STAGE_READY` in *that* log;
4. runs `tools\dev\screenshot.ps1 -ProcessId $p.Id`.

Always pass `-ProcessId`, because a daily instance may be running alongside the mock. Visual shots use Debug builds; timing runs use Release.

**Canonical shot set**, in both looks (`paper`, `dark`):
- `hero` at 1456×939
- `rack` (`HALO_MODE=5`) at 1294×1447
- `small` at 900×640
- `top`, `low`, `keys`, `edge`
- `hit` (hero with pose `pad7=1.0,group=1`)
- `card`, `mask`, `albedo`

Output goes to `build/shots/<milestone>/`.

**Comparison with `tools/dev/compare_shots.py` (Pillow):**
- per-shot diff against the previous milestone
- swatch ΔE2000 from `card`
- device-box margins from `mask`
- a review sheet with TE's render and photo crops alongside

### M0: Harness, references, contract, baseline (S). No visual change.

**Files**
- `Source/Stage/StageComponent.cpp`:
  - dev hooks;
  - startup log of `GL_RENDERER`, `GL_VERSION` and `GL_SAMPLES`;
  - `GL_TIME_ELAPSED` stats (guarded, Release-available);
  - per-PID log.
- `tools/dev/stage_shots.ps1`, `tools/dev/compare_shots.py` (new).
- `reference/te-ep40/`: SVGs, the tested crops and the README; `.gitignore` covers `local/`.
- `tools/model/requirements.txt` after the wheel check (§4.1); add Pillow and svgelements to the venv.
- `Source/Core/Ep40EntityNames.h` (43 names plus root), `Tests/Ep40EntityNamesTests.cpp`.
- `CMakeLists.txt`: test sources.

**Accept**
- The baseline shot set exists in `build/shots/m0/`.
- Two runs of the same pose differ by at most 1/255 per pixel outside the LCD.
- The names test passes: 43 unique entries matching the Swift file.
- Baseline GPU p50/p95 is recorded for `hero` on the RTX 5080 and on the AMD iGPU, in Release.
- `requirements.txt` records which optional wheels resolved.

### M1: Quick wins on the current asset (S)

**Files**
- `tools/model/usdz_to_hmesh.py`:
  - cream palette from §2.5 in `MATERIALS`;
  - the 4 baked "lit" lamps (`0_1`, `1_0`, `1_3`, `2_2`) unbaked;
  - `status_lamp_top_*` removed;
  - remaining lamps grounded at plate top + 0.02 mm;
  - **`legend_25..29` deleted**;
  - `halo_ring` written with no `castsShadow` meaning, so bounds exclude it.
- `assets/EP40.hmesh` regenerated.
- `Source/Stage/FacePrint.h`: adds VOLUME, **BPM**, METRONOME, X (orange) and Y (green).
- `Source/Stage/StageLook.h` (new), with the `paper()` and `dark()` presets.
- `StageComponent.{h,cpp}`:
  - `setLook`;
  - `groundZ` from shadow-casting bounds;
  - `halo_ring` no longer drawn;
  - **floor ring pool inside the existing ground shader** (`drawGround`, `uKind 3`), using the footprint SDF and `RingState` colours;
  - camera fit through `Core/StageCamera.{h,cpp}` (fit only).
- `Tests/StageCameraTests.cpp`.
- Anisotropic filtering on the face print (guarded).

**Accept**
- `StageCameraTests`: for aspects 0.6–2.5, the projected bounding box lies inside the viewport with at least 5% margin.
- `mask` at `rack`, `small` and `hero`: the device is fully inside the stage with at least 5% margin and fills at least 60% of the limiting dimension.
- `low`: no light gap between the chassis and its shadow.
- No brown ring in any shot. `ring=steady`, `ring=discover` and every other `RingState` pose show the pool; `ring=off` shows none.
- No lit lamps in the `rest` pose. The plate reads BPM, not DUB.
- `card`: deck swatch within ΔE 5 of #DCD2BF.
- With `paper()`, the backdrop pixels are unchanged from M0. The `dark()` shots exist and the device is visibly grounded in them.

### M2: HDR core and tone mapping (M)

**Files**
- `Source/Stage/RenderTargets.{h,cpp}` (grow-only, settle-to-reallocate) and `GlProgram.{h,cpp}` (new).
- `Source/Stage/Shaders/*.glsl` (`common`, `part.vert/frag`, `depth`, `resolve`, `composite`, `fxaa`), embedded through a new `HaloShaders` binary-data target, with `HALO_STAGE_SHADER_DIR` hot reload in Debug.
- `Source/Core/ToneCurveConstants.inc`, `ToneCurves.h`, `Tests/ToneCurvesTests.cpp`, and the Debug ramp self-test.
- `StageShaders.h` removed; its passes are split into the GLSL files.
- The JUCE pixel format stops requesting MSAA.
- The current forward path is kept as the **Low** tier.

**Accept**
- `card` with Neutral: orange #FA5A16, green #0A4A36, cream #DCD2BF and off-white #E9E9E5 are within **ΔE2000 ≤ 3** of target. AgX must be ≤ 4. ACES is reported only.
- The stage border equals the token colour exactly in both looks.
- Silhouette edges show no dark or bright fringe: on a `hero` crop, edge pixels lie between the device and floor values in each channel.
- The backdrop histogram has no empty bins between its minimum and maximum (no banding). Dither runs after FXAA.
- The log shows `GL_SAMPLES=4` on the HDR FBO, or FXAA engaged.
- `ToneCurvesTests`: C++ curves match their reference formulas, and the sRGB OETF matches the piecewise definition. The Debug ramp self-test passes on both GPUs.
- `glGetUniformLocation` calls per frame: 0 after warm-up (stats).
- Dragging the window edge for 5 s reallocates targets no more than once after it settles.
- `HALO_STAGE_TIER=low` renders a complete frame.

### M3: Studio IBL, GGX, perspective floor (M), then owner gate G1

**Files**
- `tools/model/ep40/env_bake.py` producing `assets/stage/studio.henv` (neutral).
- `Source/Core/HenvFormat.{h,cpp}`, `HtexFormat.{h,cpp}`, plus `Tests/HenvFormatTests.cpp` and `Tests/HtexFormatTests.cpp`.
- `Source/Stage/Environment.{h,cpp}`: cubemap, SH9 plus the runtime floor bounce, BRDF lookup table.
- `part.frag`: GGX, IBL, specular occlusion, specular AA.
- `composite.frag`: perspective floor plane terms; the ring pool moves here from the M1 ground shader.
- `CMakeLists.txt`: `HaloAssets` adds `assets/stage/*`.

**Accept**
- `hero`: the screen window shows one continuous diagonal reflection. RECORD and PLAY show a broad highlight, with peak luminance at least 1.25 × the cap's median.
- Contrast: deck mean L* is 78–88; aperture gaps L* ≤ 20.
- Under orbit the floor recedes with the camera; it is no longer screen-space.
- Switching between `paper()` and `dark()` changes the diffuse floor bounce on the shell without any rebake.
- `HenvFormatTests` and `HtexFormatTests`: header, CRC, mip sizes, SH9 count, RGB9E5 decode of known vectors, one known RGBA texel decoded unpremultiplied in RGBA order, truncations rejected.
- `env_bake.py` produces the same SHA-256 on two runs.
- `hero` GPU p95 ≤ 6 ms on the AMD iGPU (Release).
- **G1:** the owner signs off on lighting and materials from the review sheet.

### M4: hmesh v3 and single draw (M). No intended visual change.

**Files**
- `Source/Core/HmeshFormat.{h,cpp}` (v1, v2, v3).
- `tools/model/hmesh_io.py`, `htex_io.py`. `usdz_to_hmesh.py` writes v3 by default; `--format v2` is kept.
- `assets/stage/EP40.hmesh` (v3, converted from the current geometry). The old `assets/EP40.hmesh` is removed.
- `Source/Stage/HaloMesh.{h,cpp}`: single VBO and EBO, slot uniforms, material texture, binding by kind.
- `Tests/HmeshFormatTests.cpp`, `Tests/Ep40AssetTests.cpp`, with `target_compile_definitions(HaloTests PRIVATE HALO_ASSET_DIR=…)`.
- `tools/model/tests/test_hmesh_io.py`.

**Accept**
- `HmeshFormatTests`:
  - v3 round trip of a hand-built buffer;
  - every rejection case in §5;
  - unknown chunks skipped;
  - a v2 fixture keeps its roughness, and v1 gets 0.5;
  - v2 bounds exclude `halo_ring`;
  - 2,000 random truncations and byte flips never crash and never yield an out-of-range index.
- `Ep40AssetTests`: all contract names present; pads and groups have unique slots; anchors have zero triangles.
- Python: round trip and byte-deterministic output.
- `m4` shots against `m3`: mean ΔE < 1.0, no region above 3.
- Main-pass geometry draws go from about 123 to 2 (stats). Pad travel still works in the `hit` pose.

### M5a: Compiler I: kernel, stepped chassis, side walls (L)

**Files**
- `tools/model/build_ep40.py`.
- `tools/model/ep40/{spec,frames,svg_measure,materials,lint,compare_reference}.py`.
- `geom/*`.
- `parts/{chassis,sidewalls,feet,anchors}.py`.
- `tools/model/tests/`: primitives watertight, triangulation area and aspect, sagitta counts, frame round trip.

**Carried-over parts.** Every part not yet rebuilt is carried over from the M4 asset through an explicit table, `spec.CARRY_OVER`. Each row gives a part name and a Z offset; deck parts and upper-section parts each get the offset from the old plate top to the new surface they sit on. Every row is listed, so nothing relies on a default.

**Measurement.** All heights in §2.1, plus the side-wall details and the bump question, are measured from `svg-path` and recorded with provenance. O1 numbers are applied if they have arrived.

**Accept**
- `lint.py` passes, and no height is still `svg-px` or `estimate`.
- `compare_reference` against the front SVG: zone boundaries at 8.6 / 56.5 / 104.4 within 0.5 mm; chassis outline within 0.2 mm.
- `low` and `edge`:
  - show the step, the green shell stripe and the grounded feet;
  - overlaid on the side SVGs, the profile matches within 0.3 mm;
  - the +X wall shows its port circles.
- `Ep40AssetTests` tightened: footprint 176 × 240 ± 0.3 mm; body 16 ± 0.3 mm; `boundsMin.z` equals the spec's feet contact (≈ −8.3) ± 0.3 mm; triangles ≤ 150k.

### M5b: Compiler II: keys, wells, function cells, keys atlas, decal path (L), then owner gate G2

**Files**
- `parts/keys.py` (full keys, short caps, secondary plates, small keys), plus the liners.
- `artwork/{svg_import,raster,inks,sdf,atlas}.py`, limited to cap tops, secondary plates and tabs.
- `assets/stage/ep40_keys_sdf.htex`.
- Decal UVs for caps.
- Decal path in `part.frag`.
- `Source/Stage/DecalAtlas.{h,cpp}`.
- Geometric cap text and numeral cubes removed.

**Accept**
- Lint passes, including interference at 3.2 mm travel.
- `compare_reference`: mean aperture-edge offset ≤ 0.3 mm, maximum ≤ 0.6 mm.
- Function cells are one aperture each, holding a short cap and a flat plate.
- `--check`: every cap legend in `spec.LEGENDS` is non-empty, and no ink lies outside its rectangle. The overlay PNG shows no outlines or glare shapes.
- Every pad shows its digit:
  - 8 and 0 keep their slot counters;
  - the group pads show the conga, headstock, keys and ball.
- `keys` at minimum radius: 2 mm label edges transition in ≤ 1.5 px.
- Pads travel `kKeyTravelMm` in `hit`.
- **G2:** the owner signs off on proportions, the step and the key artwork, and picks O5 (travel).

### M6: Compiler III: knobs, fader, speaker, ports, screen geometry, LEDs (M)

**Files**
- `parts/{knobs,fader,speaker,ports,display,leds}.py`; the remaining `CARRY_OVER` rows are deleted.
- `StageComponent.cpp`: glass pass (`GL_ONE, GL_SRC_ALPHA`, after the opaque pass) and the recessed `display_surface`, still showing the current display texture.

**Accept**
- Lint passes, including glass underside − icon plane ≥ 0.3 mm.
- `keys` and `low`: jack, USB-C and power-recess openings read as holes, not cylinders, and the orange slider is visible.
- `edge`: knob proportions overlay the top-edge SVG within 0.5 mm.
- Between two yaw settings on the `keys` preset, the icon plane shifts against the frame. That is parallax from real depth.
- The 18 LEDs are flush and unlit.
- Triangles about 90k, ≤ 150k.

### M7: Face artwork and neutral mode (M)

**Files**
- `artwork/*` extended to the deck silkscreen, label plate, port strip and side-wall icon.
- `artwork/neutral.py` (fontTools, OFL face, generic icons).
- `assets/stage/ep40_face_sdf.htex`.
- **`Source/Stage/FacePrint.h` deleted**, and the remaining geometric text removed.
- DD-006g entry in `docs/plan.md`.

**Accept**
- `--check`: every legend in §2.6 is assigned to a part, with no unassigned ink.
- `compare_reference`: ink ΔE ≤ 6 on the albedo view against the SVG fills.
- `keys` at minimum radius: over a 30-frame orbit in 1° steps, no legend-edge pixel flickers by more than 10%.
- `hero`: LEVEL and PITCH are readable (L* difference ≥ 30 against the deck).
- On the review sheet, RIDDIM and the silkscreen match TE's.
- The neutral build renders with no TE artwork.

### M8: Shading fidelity: baked AO, cached shadows, grain, special materials, floor reflection (M)

**Files**
- `ep40/ao.py`, which writes vertex AO and `ep40_deck_ao.htex`.
- `Source/Stage/ShadowSystem.{h,cpp}` (new): cached key shadow, PCSS-lite, deck cache.
- Analytic contact term in `part.frag`.
- `ep40/grain.py`, which writes `pa66_grain.htex` with Toksvig mips.
- Glass clearcoat and frosted-stem transmission.
- Mirrored floor-reflection draw.
- **Height-map AO code deleted**, along with its per-frame re-mip.

**Accept**
- Height-map AO and its 40 taps are gone from the code. Startup does no AO work.
- `HALO_STAGE_DEBUG=1`: the chassis wall next to a well reads at least 10% darker than an open wall, and the deck shows no AO streaks along triangle edges.
- `HALO_STAGE_DEBUG=2`: the shadow is crisp within 2 mm of contact and wider beyond. Penumbra crop variance is at most 50% of M3's (no fixed grain).
- Orbiting causes 0 shadow re-renders (stats).
- `hit` with `DEBUG=1`: the struck key's aperture darkens and returns to rest AO once the key settles.
- `hero`: the grain costs no texture taps (shader stats). At `keys` minimum radius, a cap crop at a fixed camera flickers ≤ 2/255 from frame to frame.
- `dark()` `hero`: the floor reflection is visible under the shell and fades within about 20 mm. Cost ≤ 0.6 ms on the iGPU.
- `card`: cream body and white keys differ in mean luminance by at least 4%.

### M9: Live LCD and bloom (M)

**Files**
- `Source/Core/LcdState.{h,cpp}` plus `Tests/LcdStateTests.cpp`.
- Segment count taken from `69021d87…svg`.
- `display_icons_sdf.htex` and the `DISP` chunk.
- `Source/Stage/LcdRenderer.{h,cpp}`.
- Bloom passes (`bloom_down/up.frag`).
- Edge glow and deck spill.
- `DisplayRenderer.{h,cpp}` moved to the Still tier only.

**Accept**
- With pose `lcd=demo,feed=live`, "120" shows in segment digits, the group B icon is lit and the PLAY triangle is lit.
- With `feed=preview`, the backlight is 0.15 and the digits read "PRV".
- At `keys` range the off icons are crisp, with no texture blur.
- Lit icons bloom, with a radius of at most 4 mm equivalent.
- `LcdStateTests`:
  - exhaustive over modes, groups, pads and feed modes;
  - every lit ID has a backing `DisplayState` field and exists in the shipped `DISP` chunk; nothing lights without backing;
  - **every preview frame differs from the equivalent live frame** in backlight and digits.
- A display change costs < 0.1 ms on the message thread.

### M10: Motion, emissive feedback, on-demand repaint (M)

**Files**
- `Source/Core/PadSpring.h` plus `Tests/PadSpringTests.cpp`.
- `Source/Core/StageState.{h,cpp}`: slot and LED maps.
- Aperture underglow, point lights, and the O3 LED-echo variant behind a setting; the `uGlow` cap tint removed (`StageComponent.cpp:516-518`).
- LED binding; group travel.
- `StageCamera` inertia, presets and eased reset.
- **On-demand repaint**: `setContinuousRepainting(false)` and the `requestFrame(reason)` funnel.

**Accept**
- `PadSpringTests`:
  - settles within 90 ms of a press;
  - release overshoot ≤ 6% of travel;
  - dt 1/30 and 1/144 agree within 0.1 mm;
  - travel is independent of velocity.
- `hit`:
  - `DEBUG=7` (albedo): the struck cap's albedo is identical to rest;
  - `DEBUG=8` (emissive): emission only on the aperture rim and liner (or on its LED, under O3), and none on any cap material;
  - neighbouring caps' near sides warm by ΔE ≥ 2 in the lit shot.
- A pose sequence at t = 0, 10, 20, 60 and 150 ms shows the travel curve and the fade.
- **Idle, connected with transport stopped** (mock): 0 stage frames over 60 s, and render-thread time ≈ 0.
- **Disconnected:** ≤ 6 stage frames/s, every one tagged with the reason `display`.
- With the mock clock running, frames come only in bursts, and the stats give a reason for every frame.

### M11: Tiers, fallbacks, hardening (M), then owner gate G3

**Files**
- `Source/Stage/StageTier.{h,cpp}` (High, Low, Still) and `StagePlanFallback.cpp`.
- The Still-tier guard and `context.detach()` demotion.
- `assets/stage/ep40_plan.png`, from `stage_shots.ps1 -Poster`.
- A `KHR_debug` callback count in Debug.

**Accept**
- `HALO_STAGE_TIER=low|still` each produce a complete, non-blank canonical shot set. In the Still tier, pad hits light up.
- The Still guard demotes cleanly in a simulated failure (forced `GL_VERSION` 2.1 in Debug), with no null-pointer call.
- Deleting one GLSL file in hot-reload mode demotes to Low or Still with a log line, never a blank stage.
- High tier on the AMD iGPU (Release):
  - p95 ≤ 6 ms while the mock MIDI runs;
  - an orbit drag has no frame above 16.7 ms across 600 frames.
- A 30-minute mock soak on both GPUs: GL debug errors = 0 and VRAM stable.
- **G3:** the review sheet (`hero`, `low` and `keys` in both looks, beside TE photos, plus the O3 cue shots side by side) is signed off as "recognisably the real device, trophy-worthy", and O3 is decided.

### If time runs short

The minimum chain that still delivers most of the "this is the real EP-40" effect is **M1 → M2 → M3 → M4 → M5a → M5b → M7**, in that order. Each milestone depends on the one before it:
- M3 needs M2's HDR targets;
- M5a and M5b need M4's v3 slots and materials;
- M7 needs M5b's decal path.

After those, M6's knobs and screen depth have the highest value.

---

## 9. Risks

| Risk | Impact | Mitigation |
|---|---|---|
| Heights read wrongly from the drawings (step, shell, cap, feet) | Proportions read "off", the main recognisability cue | `svg-path` measurement required before M5a closes; provenance lint; side-SVG overlay; optional O1 caliper numbers; G2 |
| Axis or mirroring mistakes between frames | Subtle wrongness | Frame round-trip tests; `albedo` overlay against the front SVG in M1, M5a, M5b and M7; landmark lint (VOLUME far left, speaker far right, ports at +Y) |
| Compound paths, fill rules, strokes and masks in TE's SVG | Broken counters, outlines printed as ink | Per-ink rasterisation with the true fill rule; strokes, masks and rings dropped; explicit `spec.LEGENDS` rectangles; `--check` fails on stray ink and writes an overlay PNG |
| JUCE premultiplies and swizzles PNGs | Corrupted SDF and grain channels | All data textures in `.htex`; a known-texel unit test |
| Python 3.14 wheels missing | Compiler cannot run | M0 wheel check; numpy fallbacks for the distance transform and triangulation; Pillow only for review images |
| AMD or NVIDIA driver differences (float MSAA, uniform arrays) | Corrupt or blank stage | FBO completeness probe; byte attributes fed as floats; no UBOs; Low and Still tiers; logged demotion; soak on both GPUs |
| GL 1.1 or Basic Render Driver context | Crash before any fallback | Version and entry-point guard before the first shader call; `detach()` and 2D poster |
| HDR MSAA resolve aliasing on bright edges | Shimmering LEDs and glass | Luma-weighted resolve; FXAA when MSAA isn't granted |
| On-demand repaint misses a trigger | Stage looks frozen | Single `requestFrame(reason)` funnel; stats log reasons; a test per listener path |
| Live-resize reallocations | Hitching when the rail opens | Grow-only targets with settle-to-reallocate |
| Dark redesign backdrop | Slab floats, shadow lost | `dark()` preset in every shot set from M1; rim light; floor reflection |
| Cream-on-cream looks flat between milestones | Milestones look underwhelming | Lighting before geometry; keys get numerals in the same milestone as their geometry (M5b); G1 and G2 |
| Thin features (0.6 mm gaps, ribs) shimmer | Cheap look in motion | Specular AA; Toksvig mips; MSAA; baked AO in gaps |
| The redesign changes backdrop or palette | Stage recoloured by accident | `StageLook` tokens are the only coupling; device colours are not tokens; border-equality check |
| Redesign expects overlays on the stage | Invisible UI | Overlay rule in §3.9, handed to the redesign |
| New emissives invent state | User misled | D4; exhaustive `LcdStateTests` including preview ≠ live; LED map holds only observed fields; Halo cues named in DD-006g |
| Contract drift with the Mac app | Broken name binding | `Ep40EntityNames.h` frozen; `usdz_to_hmesh.py --contract-check`; asset test |
| Committed binaries bloat the repo | Slower clones and builds | Only SVGs and tested crops vendored; photos kept local; `.htex` with zlib; ≤ 8 MB asset budget; shaders in their own binary-data target |

---

## 10. Out of scope

- **Excluded entirely:**
  - The Mac app, its USDZ and `reference/halo-mac/tools/blender/build_ep40.py`.
  - The OP-1 / OP-XY redesign itself: UI chrome, panels and typography. This plan only exposes `StageLook` and the overlay rule.
  - Installing Blender.
  - Underside detail beyond the feet (screws, battery door, moulded grids). Pitch is clamped to at least 12°.
  - Knurling, pointers and "rubber" pad textures, none of which the device has.
  - Impact squash. At 2% of travel it is about 0.25 px at hero distance and cannot be expressed by the slot format.
  - Depth of field and auto-exposure. Auto-exposure would pump with every LED flash.
  - Adaptive quality tiers and Intel-specific paths.
  - Compute shaders and any GL above 3.2.
- **Not built until evidence exists:**
  - Knob and fader animation, until the EP-40 is confirmed to send CC.
  - Key animation beyond the pads and groups, until Halo can observe those presses.
  - An audio-reactive speaker cone, unless the meter is proven to be the device's output.
- **Not changed:** `assets/halo_screen_indicators.png` is kept for the Still tier.

---

## Appendix A. Where each critique item was resolved

| # | Resolution |
|---|---|
| 1 | D5, §2.1, §4.3, §4.6: glass 1.0 mm, icon plane at z 14.4, gap lint |
| 2 | D8, §3.3, §5, §10: translation + rotation slots only; no squash, no `uNormalMatrix` |
| 3 | D10, §3.3: analytic aperture-SDF contact term |
| 4 | D13, §7, M10: idle redefined; preview-bound sweep; render-thread measure |
| 5 | D9, §3.7, §4.5, M3: `.htex`, no PNG data textures, known-texel test |
| 6 | D9, §2 sources, §4.5, M5b, §9: per-ink fill-rule rasterisation, drop strokes, masks and rings, include rectangles |
| 7 | §2.1, §3.6, §4.6, §5, M1: bounds from `castsShadow` parts only |
| 8 | §8 "If time runs short": the real dependency chain |
| 9 | §2.1, §2.2, M5a: feet flush to 0.3 mm; `boundsMin.z` ≈ −8.3 |
| 10 | §2 provenance rule, §2.1, M5a: step, shell and protrusion measured separately from path coordinates |
| 11 | §2.4, §4.3: power recess and slider dimensions |
| 12 | §2.1, §2.3: knob height 11.45 with spread recorded |
| 13 | §2.2 side walls, §4.3, M5a acceptance |
| 14 | §2.3, §4.3: vertical cylinder, travel along ±Y |
| 15 | §2.5, M9: segment count from the SVG before freezing `DISP` |
| 16 | O1: caliper numbers only, optional |
| 17 | §2 sources, §4.6: registration against the front SVG; render only on the review sheet |
| 18 | D12, §7, M0, M11: RTX 5080 and AMD iGPU; High, Low and Still tiers; renderer logging |
| 19 | §7 Still-tier guard, M11 |
| 20 | §3.9 overlay rule |
| 21 | §3.2 composite: unpremultiply, tone map, re-premultiply; screen-blend floor light |
| 22 | §3.2: FXAA before dither, dither last |
| 23 | §5: byte attributes fed as floats; material table in an RGBA32F texture |
| 24 | §3.2 resizing, M2 acceptance |
| 25 | §3.3 tone curves: shared constants file plus Debug ramp self-test |
| 26 | §3.4, §3.3: neutral bake; runtime SH9 floor bounce |
| 27 | M0 baseline; Release-available stats; §7 |
| 28 | D10, §4.2, §4.5, M8: offline AO; constrained Delaunay deck; no runtime bake |
| 29 | §7 logging, harness: `HALO_STAGE_LOG`, per-PID default |
| 30 | D6, M1: floor pool in the existing ground shader |
| 31 | D4, §6 screen mapping, M9: provenance treatment, preview ≠ live test, pad grid dropped |
| 32 | M1: `legend_25..29` deleted; BPM printed via `FacePrint` |
| 33 | D3, D4, O3, G3: subtle underglow named as a Halo cue; LED echo as the alternative |
| 34 | M10: albedo and emissive debug-buffer checks |
| 35 | D3, O5, §6: fixed travel; velocity drives the cue only |
| 36 | D7, §1, §3.5, M1, M8: `dark()` preset in every shot set; floor reflection brought into scope |
| 37 | O2: graph-paper floor by default |
| 38 | §3.8: grain gated to 8 px/mm and above; constant roughness below |
| 39 | Owner gates G1, G2, G3 |
| 40 | M5b: keys atlas and decal path arrive with the keys |
| 41 | M5a/M5b split; in-app poster replaces `plan_view.py`; `CARRY_OVER` table |
| 42 | §4.1: wheel check for scipy, shapely and fontTools; numpy fallbacks; Pillow added in M0 |
| 43 | §4.1 reference vendoring |
| 44 | D7, §3.1: `StageLook` by value under the SpinLock |
| 45 | Moot, because AO is baked offline into the vertex byte (D10) |
| 46 | Harness swatch-card definition; "≤ 3 geometry draws" in §7; pose override of input in the hooks table; line references corrected in §6 |
---

## Appendix B. Milestone log

Measured results, newest last. Shots live in the git-ignored `build/shots/<milestone>/`
(regenerate with `tools/dev/stage_shots.ps1`); numbers here are the record.

### M0 (baseline, today's renderer)

- **Harness:** `tools/dev/stage_shots.ps1` (one MOCK instance per shot, own `HALO_STAGE_LOG`,
  waits for `HALO_STAGE_READY`, captures by PID, JSON sidecar with the stage rectangle) and
  `tools/dev/compare_shots.py` (`card`, `mask`, `same`, `diff`, `border`, `hist`, `fringe`, `sheet`).
  `small` is the app's minimum window (1180 × 720, rail open): the app does not allow 900 × 640.
  Window size is set by the script (`SetWindowPos`), not by a `HALO_WINDOW_SIZE` hook.
- **Python wheels (3.14.8):** numpy, Pillow, svgelements, scipy, shapely 2.2, fontTools all
  resolved as binary wheels; no numpy fallbacks needed (`tools/model/requirements.txt`).
- **Determinism:** two `hero` runs of the `rest` pose: max delta 0/255 over the whole stage.
- **GPU, RTX 5080, Release, `hero` 1412 × 780 stage, mock MIDI animating:** p50 0.360 ms,
  p95 0.366 ms; 367 geometry draws per frame (123 parts × main + shadow + height passes).
  `GL_RENDERER=NVIDIA GeForce RTX 5080`, `GL_VERSION=3.2.0 NVIDIA 616.56`, default framebuffer
  `GL_SAMPLES=4`.
- **GPU, AMD Radeon iGPU:** not measured. It needs Halo.exe set to "Power saving" in Windows
  Graphics settings, which is the owner's call (a system setting); run
  `stage_shots.ps1 -Shots hero -Stats -HoldSeconds 12 -Exe <Release exe>` after switching.
- **Card (Neutral, today's asset):** deck #C4C1B8 (ΔE 6.3 to #DCD2BF), body ΔE 10.1, key ΔE 2.2,
  orange ΔE 4.5, green #0F644D (ΔE 8.4), label plate ΔE 1.6; the shell band is grey (ΔE 33 to
  the green shell) and the screen frame near-black (ΔE 13.7).
- **Mask, `hero`:** margins L 23.5 % R 26.6 % T 18.6 % B 9.4 %; fill 49.9 % × 72.1 %.

### M1 (quick wins on the current asset)

- **Asset:** the section 2.5 palette (cream #DCD2BF body and deck, #E9E9E5 numeral keys,
  #DDD3C2 group pads and beige function caps, #0A4A36 green, #FA5A16 orange, #0B2E24 glossy
  frame, #1C1B19 gaps, TE's inks); the bottom shell is a 3.8 mm dark-green stripe all round;
  `legend_25..29` dropped (no more "DUB"); `halo_ring` is a zero-triangle anchor.
- **Deviation (LEDs):** the plan said to remove `status_lamp_top_*`, but TE's front drawing and
  section 2.3's own LED table have a fourth LED row at y 132.0 (LEVEL / PITCH / TIME). All 16 + 2
  LEDs are now generated as flush 1.8 mm unlit discs at the section 2.3 positions (the Blender
  lamps were 2.5 mm pills 1.3 mm above the plate and about 4 mm right of the real spots).
- **Print:** TE positions in reference mm, green #00503A / orange #FF681D ink, 2.1 mm caps, plus
  VOLUME, BPM ─ METRONOME, GAIN and SWING brackets and the big X / Y. The bracket row sits at
  y 108.6 instead of 108.0 until M5a, because the carried-over display band reaches y 107.4.
- **Card:** deck ΔE 1.75, key 1.31, orange 2.59, green 0.67, label plate 1.55, frame 1.47.
- **Mask:** `hero` margins ≥ 6.9 %, limiting fill 84.7 %; `rack` 6.9 % / 86.1 %; `small` 7.0 % / 84.7 %.
- **Border:** `dark()` stage border is exactly #060607 in `hero`, `rack`, `small`, `low`, `top`,
  `edge` (floor terms fade out over the outer 4 % of the stage). With `paper()` the backdrop is
  unchanged from M0 wherever M0 had no floor shadow (the only differences are M0's shadow
  reaching the stage edge, which M1 no longer does).
- **Determinism:** two `hero` runs: max delta 0.

### M2 (HDR core and tone mapping)

- **Pipeline:** 4x MSAA RGBA16F scene (device radiance + floor shadow, MRT) -> custom resolve
  (device samples weighted 1/(1+luma); floor averaged over uncovered samples only) -> composite
  (tone map once, floor, exact sRGB, ±0.45 LSB dither) -> FXAA only when MSAA < 4 or Low tier.
  The window framebuffer requests no MSAA. GLSL lives in `Source/Stage/Shaders/` (HaloShaders
  binary data; Debug hot reload from `HALO_STAGE_SHADER_DIR`, verified). Low tier = the M1
  forward path into an LDR target + FXAA (`HALO_STAGE_TIER=low` renders a complete frame).
- **Deviation (floor blend):** the ring pool and the contact shadow are blended in display values
  in the composite (as the Low tier does), not as `base + (1 − base)·T(light)` in display-linear:
  a display-linear screen of the saturated orange reads pink on the cool paper floor, and a
  linear multiply makes the tuned shadow far weaker. Both tiers now match.
- **Card:** Neutral worst ΔE2000 2.27 (orange), cream 1.47, white key 1.16, green 0.89 — passes
  ≤ 3. **AgX fails ≤ 4** at exposure 1.0 (cream 7.7, white 9.1, orange 7.6 — AgX's base curve
  compresses albedo-range values hard by design); ACES Hill 7.7–11.9 (reported only). Neutral
  stays the default (D2).
- **Tone curves:** `ToneCurvesTests` (reference values, monotonic, sRGB piecewise and 8-bit round
  trip) pass; the Debug ramp self-test passes on the RTX 5080 (max |GLSL − C++| 1e-7 Neutral,
  9e-6 AgX, 4e-7 ACES). Not run on the AMD iGPU (needs the owner's GPU switch).
- **Border:** `dark()` border exactly #060607 in all six framings; floor terms are exactly zero in
  the outer 2 px (near black, the sRGB curve made a 0.1 % residue a visible +2). `paper()` border
  matches M1 within ±1 (dither on the anti-aliased grid lines).
- **Banding:** `dark()` floor gradient (ring off): no empty histogram bins (R 6–30, G 6–30, B 7–30).
- **Fringe:** 152 of 1395 silhouette samples fall outside their 2–4 px neighbours' range, all on the
  bottom shell where the silhouette is the shadowed underside fillet (M1's LDR path: 170 at the same
  places); none elsewhere. No halo is visible at 4x zoom.
- **Uniform lookups:** 0 per frame after the first (GlProgram caches locations).
- **Resize:** a 5 s window-edge drag (`tools/dev/stage_resize.ps1`) reallocated targets once, after
  the size settled.
- **GPU, RTX 5080, Release, `hero`:** p50 0.468 ms, p95 0.472 ms (M0 0.366 ms), `GL_SAMPLES=4`.
- **Determinism:** two `hero` runs: max delta 0.

### M3 (studio IBL, GGX, perspective floor) and gate G1

- **Environment:** `tools/model/ep40/env_bake.py` -> `assets/stage/studio.henv` (2.1 MB):
  256^2 x 7-mip GGX-prefiltered RGB9E5 cube map, SH9 ambient irradiance, key and rim light
  records. Two runs give the same SHA-256 (586a2fb0...). `Source/Core/{ChunkFile,HenvFormat,
  HtexFormat}` parse the shared chunked container with full rejection checks
  (`HenvFormatTests`, `HtexFormatTests`, including a known unpremultiplied RGBA texel, RGB9E5
  known vectors, CRC and every truncation). Python side: `tools/model/htex_io.py` plus
  `tools/model/tests` (6 tests).
- **Deviations (studio values):** key 12 (plan 6), fill 1.4 (0.8), room 0.05 (0.03); at the plan's
  values the cream deck lit at L* 63 (target 78-88). The rim strip is 1.0 x 0.02 m at radiance
  100 (plan 0.12 m at 10), behind-left at 37 degrees elevation (plan: behind-right, 20 degrees):
  the hero camera's reflections from the flat screen look there, and a 0.12 m strip was taller
  than the screen's reflection window, so it washed the whole glass instead of crossing it.
  Mip m holds roughness (m/6)^2 and the shader samples lod = sqrt(roughness) x maxMip (plan:
  linear), so glossy acrylic is not blended with a rough mip's haze.
- **Deviation (direct light):** the key and rim are in the prefiltered map for reflections (that
  is what draws the softbox and the streak), with the key's share of each reflection shadowed;
  their diffuse is analytic (shadowed key, wrapped for satin) on top of SH9 ambient. There is no
  separate analytic GGX lobe, which would count the lights twice.
- **Shading:** split-sum DFG table (64^2 RG16F, height-correlated Smith, computed at startup),
  multi-scatter compensation, Lagarde specular occlusion, Kaplanyan-Hoffman specular AA, SH9 with
  a runtime floor-bounce lobe (paper vs dark changes the cream walls by 4.7 L*, no rebake).
- **Floor:** a real plane at the device's lowest point, found per pixel from the inverse
  view-projection; graph paper (5 mm cells, 25 mm major) recedes in perspective with
  derivative-filtered lines and fades to the token before the stage edge.
- **Checks:** `hero` deck (up-facing) mean L* 78.7 (78-88, ok); aperture gaps median L* 3.1 (ok);
  PLAY broad highlight peak/median 1.40 (ok); **RECORD 1.14 (fails 1.25)**: the carried-over caps
  are flat-topped, so at hero distance the rim's reflection is uniform across the top and only
  the small back fillet peaks; M5b's caps with real 0.7 mm top fillets are the fix. Card
  unchanged (worst ΔE 2.27). `dark()` border exact in six framings; banding: no empty bins.
  The screen shows one continuous diagonal streak in `hero`. Release, RTX 5080, `hero`: GPU p50
  0.492 ms, p95 0.496 ms. AMD iGPU still unmeasured (owner's GPU switch).
- **Found and fixed:** on this machine the desktop compositor drops one exact colour,
  RGB (13, 11, 12): the dark floor's dithered pixels landing on it showed as black dots on screen
  and in captures although the framebuffer held them (proved with the new Debug
  `HALO_STAGE_READBACK` framebuffer dump). Final outputs nudge that value to (13, 11, 13).
  Also fixed: an undefined GLSL `clamp` (lo > hi) in the floor grid.
- **G1 (delegated, self-review against `c_keys.png` and TE's renders):** passed with notes. The
  cream body, white keys, riddim orange and green, near-black apertures and the satin response
  read as the TE photographs; the device sits on a receding paper floor (or a lit dark sweep)
  with a grounded contact shadow. Open: RECORD's highlight (above), white keys a touch brighter
  than TE's photo, the glass streak is strong at `rack` framing, and proportions and artwork are
  still the carried-over Blender asset (M5a/M5b).

### M3 follow-up: screen sheen (review note)

- The rim streak blew the left half of the LCD out to near-white (peak channel 254, LCD detail
  under it down to 48 % of the bare screen at `rack`). The acrylic's reflection is now a sheen:
  scaled by 0.55 and rolled off toward a ceiling of 0.13 linear luma (`uGlassSheen`,
  `uGlassPeak`; `HALO_STAGE_GLASS=sheen,roughness,peak` tunes it in Debug, `0` removes it).
- `tools/dev/stage_glass.ps1` shoots `hero`, `keys`, `rack`, `small` in both looks with and
  without the sheen; `compare_shots.py glass` reports the sheen's lift, its peak and the LCD's
  fine detail under it. Now: peak channel 241-242 (no clipping), mean lift 20-22 / 255, LCD detail
  under the sheen 82-88 % of the bare screen in all eight shots (target >= 60 %).

### M4 (hmesh v3 and single draw)

- `assets/stage/EP40.hmesh` v3 (1.97 MB, was 3.4 MB as v2): 19 materials, 45,724 vertices, 71,786
  triangles, 16 slots. The converter is byte-deterministic (two runs, same SHA-256 546908ae...).
  Deviation: the v3 header is the shared 32-byte chunk-container header with bounds in a `HEAD`
  chunk (one parser for every data file), and a material record is 72 bytes (the plan's fields do
  not fit in 64).
- The stage draws the device in **one** call (geometry draws per frame: 3 in all, shadow + height
  + main, was 357); pads move by slot uniforms in the vertex shaders; the LCD and the printed deck
  are found by material flags. The v2 asset is deleted.
- `m4` against the pre-M4 renderer in the same app (`hero`, `keys`, `low`, `top`, `card`, both
  looks): mean ΔE 0.00 everywhere, worst pixel 1.44 (limit: mean < 1, nothing above 3).
- Tests: `HmeshFormatTests` (round trip, every rejection, unknown chunks, legacy v1/v2, 2000
  corruptions, the shipped asset's contract names, slots and anchors); Python `test_hmesh_io`
  (round trip, byte determinism, octahedral normals, shipped asset).

### M5a/M5b geometry (keys, step, function cells, knobs) - shots verified

- `tools/model/build_ep40.py` rebuilds the body and every key from `tools/model/ep40/spec.py`
  (each value tagged with its source; all heights measured from TE's side drawings at 0.600 mm
  per unit): a 3.17 mm shell band, the deck at 12.83 mm, the upper body stepped up to 15.96 mm
  from y 104, 3.21 mm keys with 0.7 mm top fillets in near-black apertures, function cells as a
  short cap over a flat 0.2 mm plate, small keys, and knob towers (16 mm disc, 8.07 mm stem, cap
  top at 11.2 mm). Display, label plate, speaker and port tabs are carried over from the old
  asset, scaled onto the new heights. `lint.py`: 0 failures, 81,418 triangles.
- Shots `m5` (hero, keys, low, edge, top, hit; dark and paper) all render; the device now reads
  as the low, stepped EP-40 rather than the tall Blender block. Compared with `m4` it is a new
  model by design, so no pixel diff is gated.
- Highlight on the caps: the specular anti-aliasing widened a 1-2 px fillet to roughness ~0.65,
  so no edge caught the key. The widening is capped at 0.04 (was 0.18) and caps are satin 0.30
  (was 0.45). `regions` on `m5r2` hero: PLAY 1.33 (ok); RECORD 1.11 (still under 1.25: its top
  already sits on the tone shoulder at R 245, so the line of light cannot add much luminance).
  Deck L* 85.8, gaps 9.3 (both ok). The `small` shot shows no fillet sparkle.
- Glass sheen re-check (`m5glass`, hero and keys, both looks): peak channel 241-242, LCD detail
  under the sheen 85-87 % of bare (ok).
- Open for G2: pad 7's carried-over legend reads as "=", cap and plate legends are too small to
  read and not TE's lettering (decal atlas, M5b remainder), the VOLUME label sits partly under
  its knob.
