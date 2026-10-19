# Halo redesign plan: one big TE screen

**File:** `docs/redesign/plan.md` · **Branch:** `redesign/te-hardware` · **Status:** in progress (R0, R1, R2a landed; revised after critic review; §8 lists where each issue was resolved)
**Companion plan:** `docs/redesign/riddim-3d-plan.md`. It owns everything inside the 3D stage: `Source/Stage/*`, the model pipeline and the stage's look tokens. The two plans meet at only three points:
- `StageLook` (3D plan §3.1)
- the overlay rule (3D plan §3.9): nothing JUCE draws can sit on top of the stage
- a short list of interface requests (§4.4 below)

**Mockups.** All are 1440×900 SVGs in `docs/redesign/mockups/`. They are references for the look. **Where a mockup and this plan disagree, this plan wins.** That includes every colour that §2.1 corrects.

| Mockup | Role |
|---|---|
| `hybrid-play.svg`, `hybrid-chop.svg` | **Chosen direction.** Palette, numerals, rail modules, slicer, PLAY stage |
| `op1-play.svg`, `op1-chop.svg` | Grafts: vertical alignment of encoder, cell, pill and knob; the pop-up value card; the paper sampler band; the honest slot; deck dressing |
| `opxy-play.svg`, `opxy-chop.svg` | Grafts: caution band around the GL viewport; cell value bars and press captions; the lane; paper rack tiles; the honest-state form rules |

**Ground rules for every milestone:**
- The product rules in §0.3 hold at every commit, starting with R0.
- Each milestone lands and is reviewed on its own, and stays under about 1.5 k changed lines.
- The owner's daily Halo is never touched:
  - Branch builds go to `build\Redesign`.
  - Screenshots come from MOCK instances captured by process id. They run beside the daily build inside a sandboxed data root (§6.1), so they never write to the owner's real Music, Documents or settings folders.

---

## 0. Decisions

### 0.1 Direction

**Chosen: "hybrid", OP‑1 field × OP‑XY**, with the judges' grafts applied. Both judges picked it:

| Direction | Aesthetic | Buildable |
|---|---|---|
| hybrid | 8.6 | 8.0 |
| op1 | 8.2 | 6.0 |
| opxy | 7.4 | 7.5 |

**Why hybrid wins:**
- It is the only direction that delivers both halves of the owner's steer:
  - **OP‑1 field soul:** the four colour encoders, monoline numerals with a bare-bar "1", small line-drawn machines, level arcs and tape lanes.
  - **OP‑XY discipline:** one page skeleton, grey-ramp cells, a stepped slicer, lowercase soft keys and snapping lists.
- That mix keeps a data-heavy app calm and still makes it look like a TE screen.
- Its colour grammar, **grey means read-only and an encoder colour means you can turn it**, is the most useful idea in any of the three directions. A mouse user can see where the wheel works without hovering.

**Where it falls short, and what fixes it:**
- **Alignment:** the encoders were bunched at the right, away from their cells (fixed by U2).
- **PREVIEW** had no hue of its own (U8).
- **Reds and amber:** there were three near-identical reds, and amber was reused for "writes locked" (U6, U7).
- **Pop-up card:** it was never actually shown (U4).

### 0.2 Decision table

| # | Decision | Source and reason |
|---|---|---|
| U1 | **The window is one black TE screen** (the glass) set into a graphite deck. The deck holds 7 mode keys, SHIFT, REC and the 4 colour encoders | hybrid |
| U2 | **The parameter strip sits at the bottom of the screen, directly above the deck.** Each encoder is centred under its cell at every width. Cell value, press pill and knob form one vertical line | op1/opxy graft. The hybrid mockups put the strip under the header; this plan moves it |
| U3 | **One soft-key pill per encoder**, in a row between the strip and the deck. The pill is filled when the encoder has a press action and is a dim empty capsule when it has none | op1 |
| U4 | **The pop-up value card docks in the lane**, the area left of the strip. The lane only ever holds non-critical, redundant content, so the card can never hide a blocker, an armed instruction or a reason (§2.5). The card never touches GL | resolves the op1 card covering slices |
| U5 | **Encoder palette = OP‑1 field** (blue, ochre, grey, orange-red). The vivid 2011 set is dropped: it looks toy-like, and its green clashes with riddim green | both judges |
| U6 | **One red: crimson `#DC1E5A`.** It is used for danger, blockers, REC, device-write confirms, the limit zone and the limit lamp. There is no separate record red. It is cooler than the first draft's `#E5243B`, so it stays clear of encoder 4 and info orange (§2.1) | buildable judge; critic ΔE fix |
| U7 | **MOCK = TE yellow `#FAB413`. Nothing else is ever yellow or amber.** "Writes locked" is a hollow LED plus a padlock. GRAB kind tags are *outlined* ochre, never filled | fixes the hybrid contradiction |
| U8 | **PREVIEW = violet `#B07CFF`**: filled tag, hollow lamp, dashed frame. This overrides both judges. Pink `#F0468C` sits about 15° from the crimson U6 requires, and blue collides with encoder 1. Violet belongs to nothing else in the app | stated override |
| U9 | **Info notes stay Halo orange `#FF6A2A`** (product rule) and are always an **outlined box with a circled "i"**. Blockers are always **filled crimson with a triangle**. The two never share a form | opxy form rules |
| U10 | **The authoritative MOCK and PREVIEW marks are chrome**, drawn in an 8 px band *around* the GL child window. In-scene marks are redundant extras requested from the 3D plan. **There is no GL HUD.** Corner readouts live in the rail and the lane | opxy graft |
| U11 | **Shortcuts:** Ctrl+1…6 stay as they are today. **CHOP becomes a mode at Ctrl+7.** Nothing is renumbered | op1/buildable judge |
| U12 | **The 3D stage appears only in PLAY.** Every other mode gets the full glass and renders 0 stage frames. Coming back to PLAY must reach the first frame within **150 ms** (measured, §4.3). Until then a JUCE-painted poster of the last frame fills the stage cell | all three; critic fix for context recreation |
| U13 | **Waveforms:** EDIT uses the OP‑1 field paper **sampler band** (precise trim). CHOP uses the OP‑XY **stepped slicer**. Both share one `WaveCache`. The owner may switch CHOP to the band at gate U3 | hybrid plus aesthetic graft |
| U14 | **Fonts ship as OFL files.** TE's own faces are an optional local override and are never committed (§2.2). This is recorded as **DD‑007a**, the UI counterpart of DD‑006g in the 3D plan. **DD‑006e still governs the 3D silkscreen:** neutral typeface only, even with the override on | research brief; critic fix |
| U15 | **Nothing decorative pretends to be a control or a signal.** Every meter names its tap point (`in`, `pre-rack`, `out`). CAPTURE gets a real input meter, which needs a new metering tap (R9) but no change to the audio path. There is no record-level fader, because Halo has no input gain | product rule; critic fix |
| U16 | **Hold-to-delete is dropped.** Delete uses the same card as send: cancel has default focus, and Enter confirms only after Tab | buildable judge |
| U17 | **Default skin "graphite"**, with an alternate **"field"** skin (aluminium deck, same black screen) behind one `setTheme()` call. `legacyPaper` is kept only until R13 | aesthetic graft |
| U18 | **Showcase drift (3D plan O4): off, with no toggle in v1.** TE screens never loop idly | answers the 3D plan |
| U19 | **Modifier grammar:** Shift is the **shift layer**, as on TE hardware. **Alt = fine. Ctrl = coarse**, except over the CHOP canvas, where Ctrl+wheel zooms. One gesture, one meaning (§2.5) | critic fix |
| U20 | **Every device write goes through one `WriteGate`**, which always opens a confirm. That includes ASSIGN, which today writes with no dialog. This is fixed in R0 | critic fix (H3) |

### 0.3 Product rules: invariants checked at every milestone

| ID | Invariant | How it is checked |
|---|---|---|
| **H1** | When the source is MOCK, a MOCK tag shows in the status strip's reserved honest slot in **every** mode, at **every** width (1180–2560), at every DPI scale. The stage, or its fallback poster, carries the caution band | `HonestStateTests`, `UiLayoutTests`, and a masked pixel probe in `ui_shots` |
| **H2** | When the feed is PREVIEW (including disconnected demo frames), a PREVIEW tag shows in the strip, plus the violet stage frame | same |
| **H3** | A device write (SEND, SEND TO PADS, ASSIGN, DELETE) is impossible without a complete backup. **Every** write opens a confirm through `WriteGate`, and the confirm names the backup it relies on. The gate is re-checked live while the card is open and once more on confirm | The write-gating *logic* is unchanged. **R0 adds the missing ASSIGN confirm**, so H3 holds from R0 on. Checked by `WriteGateTests` and by `tools/dev/check_write_sites.py`, a CTest step that fails if any file under `Source/UI` calls an `Ep40Writer` write method outside `WriteGate`. The owner checks this on the device |
| **H4** | Blockers are filled crimson with a triangle and stay until the condition clears. Info notes are outlined orange with an "i". Both stay legible at the minimum window size | `UiTokensTests` (contrast); shots at 1180×660 and at 150% scale |
| **H5** | A disabled or dead control **looks** dead (dim outline) and **states its reason** in visible text. No hover is needed, and nothing critical lives only on the shift layer | gallery shot; code review |
| **H6** | Nothing JUCE-painted is placed over the GL child window | assertion in `StageFrame`; review |
| **H7** | Readouts of the same quantity agree. For example, the monitor level comes from one getter, and the backup state comes from one cached entry | `UiStateTests`; shot comparison |

**Debug exception, written down:** `HALO_DEVTEST=writetest` in `Main.cpp` performs a guarded write round trip without the UI.
- It is Debug-only, invoked by the owner, and gated by `writesUnlocked()`.
- `ui_shots` refuses to run if it is set.
- It is the only write path that does not go through `WriteGate`.

---

## 1. Vision

> **You are looking at, and playing, a Teenage Engineering screen the size of a window.**

**The screen**
- Halo is a single slab of black glass. Every mode on it is an OP-style page: a quiet header, one large canvas, and four parameter cells in the four encoder colours.
- Under the glass is a strip of graphite hardware:
  - low-profile pictogram keys with LEDs
  - a light SHIFT key
  - a TP‑7 red REC key
  - a speaker-grille dot field
  - **four knurled colour encoders**
- Turn the mouse wheel over a knob, over its cell or over the marker it owns, and the value moves. The cell's top bar turns white, and a small off-white card in the lane shows the value beside a drawing of that encoder's cap.

**The machines**
- Grey is information, and colour is something you can turn.
- Small line-drawn machines give each mode its character. Each one reacts only to the signal:
  - tape reels that coast to a stop
  - level needles with real ballistics
  - scissors that snap when a cut lands
  - a sound-path chain of echo rings, spring coil, filter curve and speaker cone

**The hero object**
- On PLAY, the EP‑40 Riddim sits on the black glass as the one full-colour object, in its true cream, riddim green and orange, lit like a product photograph.

**Honesty is part of the look.**
- Yellow hazard stripes mean MOCK.
- A violet dashed frame means PREVIEW.
- Filled crimson means stop. Outlined orange means note.
- An always-visible padlock says whether Halo may write to the device.
- Every write asks first.

### 1.1 What we borrow

| From | Borrowed | Used for |
|---|---|---|
| **OP‑1 field screens** | Four encoder colours; monoline numerals (1 as a bar, 0 as a stadium, square 5); pop-up value card with a cap drawing; paper sampler band (black mirrored waveform, colour-coded marker tabs, hatched trims); tape reels, lanes and loop bracket; level arcs; LED ladders with plum unlit segments; sound-path chain with warning glyphs; the "POWER OFF?" card; soft-key pills; HOLD/ERASE-style tags | encoder grammar, EDIT, GRAB, MONITOR, RACK, ConfirmCard, toggles |
| **OP‑XY screens** | Page skeleton; 8-step cool grey ramp for read-only cells; ramp-filled "brain" header cells; stepped slicer waveform with black cut lines and square handles; lowercase soft words (`transient  even`, `cancel  done`); outlined focus and filled selection in lists; paired step-key shading; hatch for muted or not kept; paper tiles with encoder-shade dots and black values; red reserved for record | layout, CHOP, LOAD lists, last-pad cells, RACK tiles |
| **OP‑1 field / OP‑XY hardware** | Graphite (or aluminium) deck; square low-profile pictogram keys with LEDs and printed lowercase legends; light SHIFT (a real layer key); knurled colour encoders (mouse wheel = turn, click = press); speaker grille; VU LED window; printed nameplate | the deck, mode keys, status LEDs |
| **TP‑7** | One bright red record lamp; reels as transport and throughput feedback | REC key, CAPTURE, upload and backup progress |
| **EP sample tool / TE web** | Hard 3 px unblurred offset shadow on modal cards (the only shadow); 48 px minimum hit targets; te-yellow | ConfirmCard, hit targets, MOCK |
| **EP‑40** | Riddim green `#00503A`, cream `#DBD0B9`, accent `#EF522F`, used **only** on the model and its screen | keeps the stage visibly "the device" |

---

## 2. Design system

### 2.1 Palette

The hex values live once, as plain `uint32_t`, in `Source/Core/UiTokens.h`, so they can be tested without JUCE. `Source/UI/Design/Theme.h` wraps them as `juce::Colour` roles. Every contrast ratio below was measured (WCAG relative luminance). ΔE is CIEDE2000.

**Screen (glass)**

| Token | Hex | Role |
|---|---|---|
| `scr.bg` | `#000005` | glass: every page background |
| `scr.panel` | `#07090C` | ParamCells, name cells, wells |
| `scr.wave` | `#121318` | slicer panel |
| `scr.waveSel` | `#1E2028` | selected-slice backdrop |
| `scr.grid` / `scr.gridDot` | `#101C26` / `#24394B` | faint grids, stage-floor dot grid (non-text) |
| `scr.rule` | `#1E252B` | hairline rules, column separators (non-text) |
| `scr.frame` | `#383E43` | inactive outlines, gauge rests, dead capsules, selected list bar (non-text) |
| `scr.mute` | `#545E69` | **non-text only**: unlit ticks, dead pictograms |
| `scr.dim` | `#7A8590` | labels, hints, inactive text. **On `scr.bg`, `scr.panel` and `scr.wave` only** (5.57 / 5.30 / 4.93). Never on keys |
| `scr.ink` | `#CDD0C2` | primary text and lines, the warm field "paper" white (13.4 on bg) |
| `scr.hot` | `#FFFFFF` | the single hottest highlight: active LED, playhead, touched-cell bar |
| `ramp[0..7]` | `#000000 #2F2F37 #484850 #616169 #7A7A82 #96969B #AFAFB4 #F7F5F5` | OP‑XY ramp: read-only ReadoutCells (1/3/5/7); unselected waveform (`ramp4`); paired step-key shading (`#26272D` / `#2C2D34`); list selection fill (`ramp7` with black text); aged log text floor (`ramp5`) |
| `paper` / `paperHi` / `paperShade` | `#CDD0C2` / `#F4F4EC` / `#A1A3A5` | sampler band, selected region, outside-trim overlay, rack tiles, cards. **Text on paper is always `#000005`** (13.4 / 19.0 / 8.3) |
| `tape.empty` / `tape.wound` | `#29221D` / `#5E564B` | tape lanes, wound-tape discs |
| `led.off` | `#57304A` | plum: unlit ladder segments, unlit limit lamp |
| `hatch` | `#6B7680` 45° lines, 6 px pitch | "not kept / bypassed / dropped / incomplete" |

**Encoders.** These are the only colours that mean "turnable". **Encoder colours never colour text on paper**; paper tiles carry black values with a 3 px `encN` bar beside each one.

| Enc | Screen (line / text on dark) | Fill (pills, card headers, tags) | Text on fill | Cap (deck) | Skirt / dim |
|---|---|---|---|---|---|
| 1 blue | `#4D7EA9` (4.63 on panel; at least 12.5 px or Numerals) | `#2C4F7A` | `#FFFFFF` (8.38) | `#2C4F7A` | `#2C4362` |
| 2 ochre | `#B38E5E` (6.60) | `#B38E5E` | `#120B04` (6.46) | `#9A7243` | `#8B663C` |
| 3 grey | `#A7B1B8` (9.14) | `#A7B1B8` | `#06080A` (9.19) | `#8C9196` | `#5B6770` |
| 4 orange | `#EF3E24` (5.11) | `#EF3E24` | `#160600` (5.08) | `#F05023` | `#B0301A` |

Two rules go with the table:
- **Enc 3 grey** is cooler and lighter than the field's `#A1A3A5`, so it never reads as warm `scr.ink` text. It always appears with its cell's top bar and lamp.
- **Enc 4 orange** never fills a rectangle holding sentence text. That form belongs to danger.

**Honest states.** Each one is reserved and has its own form, so it survives greyscale.

| Token | Hex | Form |
|---|---|---|
| `mock` | `#FAB413` | **Filled** yellow tag with black UPPER text (11.6:1) and a 12 px black/yellow hazard stub on the left. A yellow/black 45° caution band (14 px pitch) around the stage. Yellow event-log lines. Nothing else in the app is yellow |
| `preview` | `#B07CFF` | **Filled** violet tag with black text (7.17:1) and a *hollow* lamp. A 1.5 px violet dashed frame (6 on, 4 off) around the stage |
| `danger` | `#DC1E5A` | **Filled** block, 3 px radius, white "!" triangle, white sentence in 600 weight (4.80:1). Never fades by itself. Also used for: the REC lamp, the write-confirm button, the limit zone, the limit lamp, the crimson lamp, the destructive outline. **Never text on black** (it measures only 4.37:1 there): crimson on glass is always a shape, a lamp or a filled tag with white text |
| `info` | `#FF6A2A` (Halo orange) | **Outlined** 1.5 px box, 4 px radius, circled "i", orange title word (7.33 on bg), **ink** body text. Used for notes only, never for toggles, arcs or status glyphs |
| `live` | `scr.hot` | solid round lamp |
| `absent` | `scr.dim` | hollow 1.5 px ring lamp; blinks at 1 Hz for WAIT |
| `ring.ui` | `scr.ink` arc | the status-strip ring glyph. Arc length = ring level. Recording and error use `danger`. **Orange ring colour lives only in the GL floor pool**, owned by the 3D plan |

**Lamp vocabulary (7 px dots):**

| Lamp | Meaning |
|---|---|
| solid white | live / connected / complete / unlocked / engaged |
| hollow `scr.dim` | off / none / locked / idle REC |
| hollow, blinking 1 Hz | waiting |
| yellow | mock |
| violet | preview |
| crimson | error, or recording / armed (pulsing 1 Hz) |

**Chassis**

| Token | graphite (default) | field (alternate) |
|---|---|---|
| `deck` | `#1D1D20 → #121214`, vertical. The only gradient in the app | `#D9DBDA → #C8CACB`, plus brushed lines (white 1 px at 10% every 4 px), buffered |
| `deck.edge` / `glass.bezel` | `#2C2C30` / `#2A2A2E` | `#A3A6A7` / `#16181A` |
| `key` / `key.down` / `key.lip` | `#2B2C30` / `#34353A` / `#0B0B0D` | `#E9E9E4` / `#DCDDD7` / `#AEB1AC` |
| `key.shift` | `#B4B6B9` | `#36393C` |
| `print` (legends on the deck) | `#8A8C90` (4.99–5.55) | `#2B2B2B` (8.6) |
| `print.cap` (legends on a key cap) | `#A3A5A9` (5.65 on key, 4.96 on key.down) | `#2B2B2B` (11.6) |
| `print.dim` | `#55575C`, **non-text only** (rules, screw dots) | `#6E7173`, non-text only |
| `grille` | `#060607` | `#55595B` |
| `led.on` / `led.offDeck` | `#FFFFFF` + 35% halo disc / `#34343A` | same |

**Device** (stage and its screen only; owned by the 3D plan's model spec):
- cream `#DCD2BF` (3D plan swatch)
- riddim green `#00503A` / `#0A4A36`
- accent `#EF522F` / `#FA5A16`

These never appear in Halo's own UI. The accent is ΔE ≈ 3 from encoder 4, so no encoder-owned element may sit within 16 px of the GL rect. The StageFrame band and the rail gutter guarantee that gap.

**Contrast and distance rules** (enforced by `Tests/UiTokensTests.cpp`, which lists every text/background pair used in §2–§3):
- **Text pairs: at least 4.5:1.** These are the measured values, and each is a test fixture:

| Pair | Ratio |
|---|---|
| ink on bg | 13.4 |
| dim on bg | 5.57 |
| dim on wave | 4.93 |
| enc1 on panel | 4.63 |
| white on enc1 fill | 8.38 |
| dark on enc2 / enc3 / enc4 fill | 6.46 / 9.19 / 5.08 |
| white on danger | 4.80 |
| black on mock | 11.6 |
| black on preview | 7.17 |
| white on ramp1 / ramp3 | 13.3 / 6.14 |
| black on ramp5 / ramp7 | 7.13 / 19.3 |
| ramp5 log floor on bg | 7.12 |
| ramp5 on bg (disabled key text) | 7.12 |
| ink on key | 9.5 |
| print.cap on key | 5.65 |
| black on paper | 13.4 |

- **Non-text graphics** (lamps, outlines, the crimson outline, meter segments): at least 3:1 against their background.
- **Honest hues are pairwise at least ΔE 20 apart:**

| Pair | ΔE |
|---|---|
| danger–info | 28.2 |
| danger–mock | 53.6 |
| danger–preview | 33.4 |
| info–mock | 27.5 |
| info–preview | 46.4 |
| mock–preview | 64.1 |

- **Honest hues are at least ΔE 15 from every encoder colour.** The lowest pairs are danger–enc4 19.1, mock–enc2 18.8 and preview–enc3 16.1.
- **One written exemption: info ↔ enc4 (ΔE 10.8).** Both must stay orange: info is a product rule, and enc4 is the field palette. They are told apart by form. Info is always an outlined box with a circled "i" and ink body text. Enc4 is always a cell bar, a pill, a marker or a numeral, and never a box around a sentence. The test asserts the exemption list has exactly this one entry.

### 2.2 Typography

**How sizes are defined:**
- Every face is embedded, loaded once into a `HaloFonts` singleton, and fixed in size. **Type never scales with the window**; larger windows get more air.
- **Sizes are em sizes**, built with `juce::FontOptions::withPointHeight()` (JUCE 8.0.15, `juce_FontOptions.h:153`). Never use `withHeight()`, which sets ascent + descent: about 1.25 em on Lexend, so a "10.5 px" label would come out with an 8.4 px em.
- **Tracking is given in em.** `HaloFonts::kerningFactor (face, trackingEm)` converts it to JUCE's height-relative factor: `trackingEm × em / fontHeight`.
- **The minimum text size is 10.5 px em.** The one exemption is deck micro-print (`^n`), which is chassis print and is always duplicated as legible text.

| Role | Face (OFL) | Em size / weight / tracking | Case | Where |
|---|---|---|---|---|
| `label` | **Lexend Exa** 500 | 10.5 px, +0.12 em | UPPER | screen labels, cell labels, section titles |
| `title` | **Lexend Giga** 300 | 19 px, +0.06 em | UPPER | sample names (`LONGSONG.01`), page titles, wordmark |
| `tag` | Lexend Exa 600 | 10.5 px, +0.10 em | UPPER | MOCK, PREVIEW, AUTO-TRIM, FIT, ENGAGED, WRITES LOCKED |
| `ui` | **Inter Tight** 400 | 12.5 px (status values 13) | lowercase words; sentence case for prose | soft words, step keys, status values |
| `ui.light` | Inter Tight 300 | 15 px | lowercase | list names, large inline values, units |
| `body` | Inter Tight 400 | 13 px | sentence case, **existing copy verbatim** | notices, dialogs, explainers |
| `print` | Inter Tight 400 / 500 | 11 px legends / 10.5 px micro | lowercase | deck legends, nameplate |
| `mono` | **JetBrains Mono** 400 | 10.5–11 px | as is | event log, rates, slot ids |
| `pixel` | **Departure Mono** 400 | `n × 11` *physical* px: n = 2 at 100% (22 logical), 2 at 125% (17.6 logical), 3 at 150% (22 logical) | UPPER | NAME cells |
| **Halo Numerals** | custom stroked `Path` glyphs, *not a font* | cap heights XL 46 / L 36 / M 26 / S 20 / XS 16; stroke 1.5 px (≤ 26) or 1.75 px (≥ 36), fixed in screen px | digits | every big value |

**Why Lexend Exa:** it is used for labels and tags because Lexend Giga is extra-wide. At 10.5 px with tracking, Giga made the honest slot and the compact cells overflow. Exa keeps the wide geometric TE voice about 15% narrower. Giga stays for titles and the wordmark, where width is free.

**Symbols are never font glyphs.**
- ▶ ■ ● ○ ↑ ↓ ← → ⏎ ⌃ ✂ ✓ and every pictogram are `Pictograms` paths, so nothing falls back to Segoe UI Symbol.
- Shortcut text in sentences is written as words: `ctrl g`, `ctrl 7`.
- Deck caps draw `^` as a pictogram.

**Halo Numerals** (`Source/UI/Design/Numerals.{h,cpp}`):
- Glyphs are drawn on a 10×16 grid with round caps and joins, traced from the stroked numerals in `hybrid-play.svg` and `hybrid-chop.svg`.
- Glyph rules:
  - `0` is a stadium
  - **`1` is a bare vertical bar** (so 12 reads as "I2")
  - `2` has a round head and a swept spine
  - `3` has a flat top and a single bowl
  - `5` is squared
  - `.` is a short stub
- The set also includes `- + : ~ % °`, plus `–` for "no value".
- Advance widths are tabular. Each glyph's `Path` is built once and cached; each draw is a transform plus `strokePath`.
- Units ("s", "db", "mb") are set in `ui.light` at 40% of the numeral's cap height.

**Where to get the files.** Commit them under `assets/fonts/` with their `OFL-*.txt` files.

| Face | Source | Build step |
|---|---|---|
| Lexend Exa, Lexend Giga | `github.com/google/fonts/tree/main/ofl/lexendexa`, `…/lexendgiga` (variable) | instance Exa 500/600, Giga 300 |
| Inter Tight | `github.com/google/fonts/tree/main/ofl/intertight` (variable) | instance 300/400/500/600 |
| JetBrains Mono | `github.com/JetBrains/JetBrainsMono/releases` | static 400 TTF |
| Departure Mono | `github.com/rektdeckard/departure-mono/releases` | TTF |

**`tools/fonts/build_fonts.py`** runs in `tools/.venv` with `-I`, using fontTools. It:
1. Turns the variable fonts into static instances with `varLib.instancer`. Static faces are the safe path through JUCE's typeface loader.
2. Subsets them with `pyftsubset` to Latin‑1 plus `– — · ’ “ ” … − ± × °`.
3. **Asserts cmap coverage** for the exact codepoint list each role uses (taken from a `roles.json` alongside the script) and fails the build on any miss.
4. Logs each licence's Reserved Font Name field. Subsetting makes a modified font, so any RFN must be honoured; none is expected for these five families, and the build fails if one appears.
5. Writes `assets/fonts/*.ttf`, verifies each family name, and **emits `Source/Core/UiFontMetrics.h`**: per role, the advance widths, kerning-adjusted tracking and cap height. This lets pure-C++ layout tests measure real strings without JUCE.

The target is under 600 KB for all nine faces together.

**TE faces (DD‑007a).** Univers TE20 / TE40 and Technotype EP may be used as a *local override* only.
- **Setup:** they go in `assets/fonts/local/`, which is git-ignored. Convert WOFF2 to TTF with `fontTools.ttLib.woff2`. They are enabled with the CMake option `HALO_LOCAL_FONTS=ON`, which maps `ui` to TE20 Light and `label`/`tag` to Technotype EP Bold.
- **Never committed:** the Linotype name table forbids redistribution. The OFL build is the reference look.
- **Self-check:** with the override on, a Debug start-up self-check measures every reserved-slot string (honest slot, rec, writes, compact cell labels) with the registered faces. If any overflows, it **refuses the override** and logs the reason.
- **Silkscreen:** DD‑006e still governs the 3D silkscreen (`FacePrint.h`). Any change there is decided by the 3D plan's DD‑006g.

### 2.3 Space, rows, radii, lines, depth

**Grid:** 8 px unit with a 4 px half-step. The glass gutter is 16 px; section gaps are 8 or 16 px; a hairline rule sits above each section.

**Row heights (closed set):**

| Height | Used for |
|---|---|
| 18 | log lines, captions |
| 22 | dense hints, ruler |
| 30 | step keys, list rows, chips |
| 44 | action keys, deck keys (default) |
| 86 / 72 / 64 | ParamCells (default / compact / ultra-compact) |

**Computed exception:** PadGrid row height is derived from its region (`(regionH − 3·gap) / 4`, clamped 48–100) and tested at every size. No other row height is allowed, which ends today's 22/26/28/30/32/34/36/38/40 drift.

**Radii:**

| Element | Radius |
|---|---|
| glass | 12 |
| cells / wells / tiles | 4 |
| step keys | 3 |
| tags | 2.5 |
| pills | full |
| deck keys | 6 |
| cards | 6 |

**Lines:**
- hairline: 1 *physical* px, snapped with `Paint::snap()` using `getPhysicalPixelScaleFactor()`
- standard: 1.5 px for outlines, pictograms and illustrations
- bold: 2–2.5 px for focus outlines, playheads, needles, cut lines and the LENGTH edge
- Round caps and joins throughout.

**Depth:**
- **Inside the glass: flat.** No gradients, glows, blurs or shadows. The exceptions:
  - the ConfirmCard's hard **3,3 px** black offset shadow over a 70% `scr.bg` scrim
  - LED halos on the deck, which are a solid 35% disc and never a blur
- **On the deck:** keys have a 2 px unblurred lip and 1 px of press travel. Encoders have a 3 px hard under-edge at 16% black and a 1 px top highlight at 6% white.
- The only soft light anywhere is inside the GL stage, which belongs to the 3D plan.

**Materials:**
- black glass with a 1 px bezel
- graphite anodised deck (brushed aluminium in the field skin)
- knurled encoder skirts with 48 ticks
- a speaker-grille dot field at a 7 px staggered pitch
- printed lowercase legends next to each control
- one nameplate print: `halo · hl–40 companion`

### 2.4 Motion tokens

| Token | Value |
|---|---|
| `cut` | 0 ms: tags, lamps, selections, mode switches |
| `flash` | 90 ms: pad hit, slice audition |
| `card.in` / `card.hold` / `card.out` | 80 ms / 1000 ms / 120 ms cut |
| `scroll` | 120 ms ease-out, no overshoot (scroll position only) |
| `needle.rise` / `needle.fall` | 10 ms / 300 ms |
| `peak.hold` | 1500 ms |
| `coast.tau` | 600 ms (reels) |
| `pulse` | 1 Hz (REC, live blocker on the ring) |
| `react.max` | 250 ms (machine reactions, no overshoot) |
| `gesture.end` | 250 ms after the last wheel detent (§2.5) |

### 2.5 Component primitives (`Source/UI/Design/Primitives/`)

Each primitive is a `juce::Component` with its own `paint()`, reads `theme()`, and is opaque where it fills its bounds. Shared painters live in `Paint.h`.

#### Input contract for anything an encoder owns (U19)

| Input | Effect |
|---|---|
| Wheel | 1 detent per notch |
| **Alt+wheel** | fine, 1/10 |
| **Ctrl+wheel** | coarse, ×10. **Over the CHOP canvas, Ctrl+wheel zooms instead**; it is the only override, and it is printed in the CHOP lane |
| **Shift held** | the **shift layer**: the deck SHIFT key draws pressed, and cell labels and pills swap live. A Shift+wheel turns the *shift binding*, if the cell has one; otherwise it does nothing and the cell shows `no shift`. It is never fine |
| Vertical drag | 180 px = full range; Alt+drag is fine; right-button drag is also fine |
| Click | press (the pill's action); Shift+click is the shift press |
| Double-click | reset to the binding's default |
| Hover | the owning cell's top bar turns `hot`, and the deck encoder shows a 1.6 px ring in its colour |

**Windows note:** a bare Alt tap can enter the system-menu loop. `Main.cpp` swallows `WM_SYSKEYUP` for a bare Alt on the Halo window (R4b), and the gallery verifies that the next keystroke still reaches Halo. If that fails, right-button drag remains the fine path.

#### EncoderBus (`Design/EncoderBus.{h,cpp}`)

- A page registers four `EncoderBinding`s:
  ```
  struct EncoderBinding {
      label, encIndex;
      getNorm(), setNorm(n), defaultNorm | reset();
      steps;                       // 0 = continuous; N = discrete (QUALITY 5, CHANNELS 3, SLICES 5)
      step, fine, coarse; format();
      pressLabel, press();
      shiftLabel?, shiftTurn?(delta), shiftPress?();
      beginGesture(), endGesture(); // drag start/end; wheel: first detent / 250 ms after the last
      available(), reason();        // dead state and its printed reason
      ownedRegions();               // on-screen rects/markers that also take the wheel
  };
  ```
- Deck encoders, ParamCells, pills, and any on-screen element listed in `ownedRegions` (a marker, arc or ruler) all route through the bus, so the four views can never disagree.
- **Expensive parameters commit in `endGesture`.** CHOP SENSITIVITY re-runs onset detection only after the gesture ends, never once per notch. The 3D plan's camera inertia is bypassed between `beginGesture` and `endGesture`.
- **Keyboard:**
  - F1–F4 focus encoder 1–4, shown by a ring in the encoder colour.
  - Then ↑/↓ turn it, Alt+↑/↓ turns fine, Ctrl+↑/↓ turns coarse, Shift+↑/↓ turns the shift binding, and Space or Enter presses. Esc unfocuses.
- The pure maths lives in `Source/Core/EncoderMath.h`: detent accumulation, fine/coarse, discrete stepping, drag mapping, and the `gesture.end` debounce. It is unit-tested.

#### Lane rule (U4)

The lane holds only **redundant or hint content**: clock readout, keycap hints, a copy of a status line. **Blockers, armed instructions, reasons and anything a user must see live in the page header, beside the control, or in a Notice.** The PopupCard may therefore cover the lane for its 1 s hold, and lane content reappears when the card cuts.

#### Primitive specs

| Primitive | Look | States and behaviour |
|---|---|---|
| **GlassScreen** | `scr.bg`, 12 px radius, 1 px bezel. Hosts the StatusStrip, page body, ParamStrip, SoftKeyRow | opaque; owns layout from `UiLayout` |
| **StatusStrip** | 40 px (36 compact), hairline below; slots as in §3.2 | repaints only changed slots (2 Hz poll of *cached* state); fixed collapse order; the honest slot's width is reserved |
| **DeckKey** | 44×44 cap (38 compact, 34 ultra); monoline 16 px pictogram; LED top-left; `^n` printed bottom-right on the cap in mono 7.5 (chassis print, exempt from the floor, duplicated in the soft-key hint); lowercase legend in `print` below the cap. **Compact and ultra: pictogram-only caps**; the hovered key's legend and shortcut appear in the SoftKeyRow hint, and the active mode's name is always in the page header | press cuts to `key.down` with 1 px travel within one frame; the active mode's LED is white; LEDs also carry status (§3.3) |
| **DeckEncoder** | 56 Ø (46 compact, 40 ultra), hit area 72; skirt with 48 knurl ticks in skirt colour; 30 px cap in `encN.cap`; off-white pointer (dark on enc3); parameter name printed below with a colour dot | bus contract; the pointer follows the value with no easing; dims to 40% with a "—" name when the binding is unavailable |
| **ParamCell** | 213×86 (`scr.panel`); a 3 px top bar in `encN`; lamp plus `label` in `encN`; value in Halo Numerals M (S compact) in `encN` with its unit; one gauge (tick dial, dot row, LED ladder, ruler or mini timeline); a 2 px normalised **value bar** along the bottom; optional tags | **Tag overflow rule:** at most one tag sits beside the label, and further tags stack into the gauge row's right end. A binding tag such as `limits 7` *replaces* the gauge caption (`7 slices cut`) rather than adding to it. At compact widths `AUTO-TRIM` reads `TRIM`. **Touched:** top bar `hot`, label inverts to a filled tag. **Unavailable:** everything `scr.frame`/`scr.dim`, the value reads "—", and a one-line reason (`even – no detection`). The wheel works on the cell itself |
| **ReadoutCell** | ramp-filled (`ramp1/3/5/7`); small lowercase label top-left, value bottom-right; **`scr.hot` text on ramp1/ramp3, `#000000` on ramp5/ramp7** | read-only: ignores the wheel and never has a top bar (the form that says "not turnable") |
| **SoftKeyRow / Pill** | 32 px row (28 compact, 26 ultra); pills 120×24 (22 compact) centred under each encoder; hint text on the left in `ui` 11 `scr.dim` | **available:** filled with the encoder's *fill* colour and its *text on fill* (white on `#2C4F7A` for enc1); **unavailable:** 1.2 px `scr.frame` capsule with `scr.dim` text on `scr.bg`; the shift layer swaps labels; clickable |
| **Lane** | the area left of the strip, at the same y as the cells (x 24…520): a mode-specific readout | redundant content only (lane rule); hosts the PopupCard while an encoder turns |
| **PopupCard** | horizontal 496×86 `paper` card, radius 6, no shadow: a header block in the encoder's **fill** colour with its **text on fill** label; a black value box with Numerals L in `scr.ink`; a drawing of the encoder cap (cylinder plus pointer at the current angle); a hint in `#000005` (`wheel ±0.5 s · alt fine`) | shows within 80 ms of a turn, holds 1 s after the last change, then cuts |
| **Tag** | 20 px, radius 2.5, `tag` type; optional lamp | filled = on, 1.2 px outline = off; variants: ink / encN / mock (hazard stub) / preview (hollow lamp) / danger / hatched (incomplete); switches by cut |
| **Lamp** | 7 px dot or 1.5 px ring | §2.1 vocabulary |
| **StepKeyRow** | flat caps, radius 3, 30 px, 2 px black lip; paired shading `#26272D`/`#2C2D34`; **equal widths computed once from the full row** | **selected:** `ramp7` with black text; **unselected:** ink text (9.5:1); **disabled:** no fill, a `scr.frame` outline, `ramp5` text on `scr.bg` (7.1:1), with the reason printed beside the row; the wheel moves the selection; ←/→ when focused |
| **ActionKey** | 44 px, label plus pictogram, width = text + 2×16 px (never fixed, so it never clips); label in ink; captions sit *below* the key on `scr.bg` | **neutral** (`#1C1D21`, ink label 10.7:1): export, show, audition, import, out. **primary** (`paper` cap, black text): back up now. **device-write** (`paper` cap, crimson write LED + ↑ pictogram, caption `writes to the ep–40 · asks first`): send, send to pads, assign. **locked write:** dim outline, padlock instead of the LED, caption `back up the ep–40 first (backups, ctrl 6)`. **destructive** (delete): crimson 1.5 px outline plus triangle, filled only inside the ConfirmCard. **busy:** dim, caption `busy: sending to the ep–40` |
| **Notice** | `blocker` / `info` / `plain`, as in §2.1; `body` type; wraps and grows (never ellipsis) | sits next to the thing it explains; blockers persist; priority from `HonestState::pickNotice()`; regions that hold notices are sized from the longest verbatim copy (§3.1) |
| **PadGrid** | 3×4 EP‑40 phone layout (7 8 9 / 4 5 6 / 1 2 3 / · 0 ↵) of StepKeys with paired row shading; device legend in mono 10.5 `scr.dim`, top-left inside an unfilled corner; payload centred (slice numeral in Numerals S, or sound name in `ui` 13 on 2 lines) | **start pad:** white LED plus 2 px ink outline. **selected / playing:** `ramp7` fill, black numeral, ▶. **trimmed:** 20×5 hatched chip with an orange edge, bottom-right. **empty:** outline with "empty". **assign-armed:** every pad gets a dashed `hot` outline and a `+`. **hit:** `flash` |
| **LevelArc** (was VuArc) | 180° arc **calibrated in dBFS, −40…0**; ticks every 6 dB; a **limit zone** −1…0 drawn as a 5 px `danger` arc; `hot` needle; ghosted big L/R behind; tap name printed under it (`out` / `in` / `pre-rack`); a `limit` lamp (plum → crimson, held 1.5 s) driven by the **pre-limiter `rawPeak` > −1 dBFS**, which is today's clip logic | ballistics on MotionClock; repaints its own rect only. It never shows a VU over-zone, because the published post-limiter level cannot exceed −1 dBFS |
| **LedLadder** | 2 px ticks, lit `encN` or ink, unlit `led.off` / `ramp2`; tap name printed | repaints only when the lit count changes |
| **DotTimeline** | a row or matrix of 2.4 px dots | used = `ramp3` filled, this operation = `hot`, free = hollow; drawn with one `RectangleList` |
| **Waveform** | one `WaveCache` (min/max per pixel column, built per width on a background thread, results handed back by message) with two renderers. **SampleBand:** paper band; black mirrored L/R lanes; colour-coded full-height markers with 6×4 tabs at both ends; grey fade wedges; hatched outside-trim. **Slicer:** 3 px columns on a 4 px pitch, heights snapped to 4 px, mirrored; selected slice `ramp7`, others `ramp4`; dropped tails `#33343C` plus hatch with a 1.5 px orange edge; 3 px black cut lines with 9 px `ramp5` square handles; the selected marker's handles orange; a ✂ pictogram on the tab | the overlay layer (markers, playhead, selection) repaints cheaply without rebuilding the cache |
| **TapeLane** | 12 px lane: `tape.empty`, buffered audio in `ramp2`, window fill in `encN`, bracket with an outlined start circle and a filled end circle, `hot` "now" head | |
| **BrowserList** | `ListBoxModel` wrapper: up to 3 columns (category `ramp5`, name ink 13, facts mono 10.5 `scr.dim`), 30 px rows. Selected row: `scr.frame` bar flush to the column, `hot` text (10.8:1), 3 px ink left bar. Keyboard focus: 1.5 px `hot` outline. A hairline column rule doubles as the scrollbar | snaps; ↑↓, Enter = encoder‑2 press, type-to-find |
| **NameCells** | 8 boxed `pixel` cells sized from the per-scale pixel size (§2.2); cursor cell outlined in ink; caption `a–z 0–9 · 8 max` | A–Z and 0–9 only, forced upper (today's rule) |
| **Progress** | pill title (`SENDING`, `BACKING UP`, `SYNCING`); a dot timeline 0…N with a filled `hot` line; Numerals L percent plus ETA in `ui.light`; two reels spinning at the measured KB/s | stays visible; coasts to a stop; cancel where supported |
| **ConfirmCard** | §3.13 | modal; the only element with a shadow |
| **WriteGate** (`Design/WriteGate.{h,cpp}`) | no look of its own | `request (WriteSpec, action)`: the **only** caller of `Ep40Writer` write methods in `Source/UI`. It opens the confirm, re-checks the gate live while the card is open and once more on confirm, then calls `action` (§3.13) |
| **StageFrame** | the 8 px band around the stage cell's active child (the GL view *or* the fallback poster), plus the 22 px chrome row above it | §3.4; asserts that no JUCE child overlaps the GL bounds (H6) |
| **Pictograms** (`Design/Pictograms.{h,cpp}`) | monoline on a 16 px grid, 1.5 px stroke, filled only when active: play, stop, rec, load (tray↓), edit, chop (scissors), capture (reels), rack (chained boxes), backup (cassette), monitor (headphones), grab (back-arc), usb, plug-x, lock-closed, lock-open, triangle, info, folder, eye, ear, speaker, caret `^`, arrows, enter, check | cached `Path`s, recoloured at draw time |

**Hatch implementation:** one `NativeImageType` tile per theme. That is the Direct2D image; it is never read back. It is used as a tiled `FillType` inside a clip. `SoftwareImageType` is reserved for images uploaded to GL. Buffered deck components use JUCE's native default.

---

## 3. Window layout and every screen

### 3.1 Frame

The geometry is computed by the pure `Source/Core/UiLayout.{h,cpp}` from `(width, height, physicalScale)` plus `UiFontMetrics.h`.

| Region | 1440×900 (default) | 1180×720 (compact) | 1180×660 (ultra-compact, minimum) |
|---|---|---|---|
| Glass | x 8–1432, y 8–780 | x 8–1172, y 8–628 | x 8–1172, y 8–580 |
| Status strip | y 8–48 (40) | y 8–44 (36) | y 8–44 (36) |
| **Page body** | x 24–1416, y 56–640 (**1392×584**) | x 24–1156, y 50–512 (**1132×462**) | x 24–1156, y 50–472 (**1132×422**) |
| Lane | x 24–520 (496), y 648–734 | x 24–424 (400), y 518–590 | x 24–424, y 480–544 |
| ParamStrip | x 532–1416: 4 cells of 213.5 px with 10 px gutters, y 648–734 | x 436–1156: 4 cells of 172.5 px, y 518–590 | same x, y 480–544 (cells 64) |
| Encoder / cell / pill centres | x ≈ 639, 862, 1086, 1309 | x ≈ 522, 705, 887, 1070 | same as compact |
| SoftKeyRow | y 740–772 (pills 24) | y 594–622 (pills 22) | y 548–574 (pills 22) |
| Deck | y 788–900 (112) | y 636–720 (84) | y 588–660 (72) |
| Deck under lane | grille 24–64 · 7 mode keys 76–420 (44 + 6 gaps) · SHIFT 432–472 · REC 480–520 | 7 pictogram keys 24–320 (38 + 5) · SHIFT 330–366 · REC 374–410; grille hidden | keys 34 at the same pitch |
| Deck under strip | 4 encoders at the centres above, names printed below | same, names below at 10.5 | encoders 40, names in the soft-key hint |
| Deck right | VU LED window x 1352–1424 (2×12 segments, tap `out`) plus `halo · hl–40` nameplate | hidden (width < 1280) | hidden |

**Layout rules:**
- `cellW = (stripRight − stripLeft − 3·10) / 4`, and encoder centre = cell centre. `UiLayoutTests` checks this to within ±0.5 px for widths 1180–2560 and heights 660–1600.
- **Compact** below 820 px client height: deck 84, pictogram-only keys 38, encoders 46, cells 72 tall with Numerals S.
- **Ultra-compact** below 700 px: deck 72, keys 34, encoders 40, cells 64, SoftKeyRow 26.
- **Page canvases yield height first.** Each page lists its yielding element and that element's minimum: CHOP canvas 120, EDIT band 140, BACKUPS shelf 120, CAPTURE list 3 rows.
- **Narrow** below 1280 px wide: the grille, VU window and nameplate drop; the lane is 400.
- Wider than 1440: the lane stays at 496 and the cells grow. Type keeps its size.
- **Text fit is tested.** For every page at 1440×900, 1180×720 and 1180×660, `UiLayoutTests` lays out the regions with `UiFontMetrics.h` and asserts that the **longest verbatim copy** of each notice, reserved tag and caption fits without clipping. For example:
  - CHOP's "N slices are over 20 s (longest X s), the EP-40's limit per sound. Turn on AUTO-TRIM…" with N = 16
  - EDIT's AUTO-TRIM note
  - LOAD's port-busy card
- **Window size:**
  - The initial window size is clamped to the display's `userArea`.
  - The minimum client size is **1180×660**, so a 1080p laptop at 150% (about 1280×690 of logical work area) fits.
  - `setResizeLimits` in `Main.cpp` and `HaloMetrics` change in R4a.
- The native title bar stays. R4a sets `DWMWA_USE_IMMERSIVE_DARK_MODE` on the HWND. It is a per-window attribute, not a system setting, so the title bar is dark in the graphite skin.

### 3.2 Status strip (all modes)

Labels use `label` in `scr.dim`. Values use `ui` 13 in `scr.ink`. Clusters are 24 px apart (16 at compact widths). Every value reads **cached** state; nothing in the strip touches the filesystem.

| # | Slot | Content | Collapse |
|---|---|---|---|
| 1 | wordmark | ring glyph (9 px circle with an `scr.ink` arc at the current ring level; crimson for rec/err) plus `halo` in Giga 300 19 | never |
| 2 | **honest slot** | strip short forms: a `MOCK` tag (yellow, hazard stub) and/or a `PREVIEW` tag (violet, hollow lamp). The long explanations live in the stage tab and the LOAD card. Width is reserved for the **both-on** case, measured from `UiFontMetrics.h` (≈ 196 px with Lexend Exa) | **never; its width never moves the other slots** |
| 3 | device | lamp + `ep–40` + `connected · os 2.5.0 · 477 sounds` / `no device` (hollow) / `in use by another app` (crimson lamp, ink text) / `synthetic` (yellow lamp) | detail first shortens to `connected`, then to the lamp + `ep–40` |
| 4 | feed | `feed` + lamp + `live` / `wait` (hollow, blinking) / `preview` (violet lamp) | label drops, then the word (the lamp stays) |
| 5 | ring | `dark · scan · link · mon · rec · tx 42% · err` (err is a filled danger tag) | the word drops; the glyph in slot 1 stays |
| 6 | out | output device name, middle-ellipsised to 22 chars | **first to hide** (it is also in the PLAY rail and on the `out` key) |
| 7 | mon | `−12 db` / `off`. Reads `AppModel::monitorGainDb()`, the same getter as encoder 1, the rail numeral and the meters (H7). In PLAY a blue dot shows that encoder 1 owns it | never |
| 8 | rec | idle: hollow `scr.dim` ring + `—:—`. Armed or recording: crimson lamp pulsing 1 Hz + `00:12` in ink | never |
| 9 | writes | padlock + `writes locked` / `writes unlocked` / `writes n/a` (no device or mock) | the word `writes` drops at compact widths; `locked` / `unlocked` / `n/a` never drops |

- The old EP‑40 / USB / STATE / DISPLAY / HALO chips merge into slots 3–5. The MIDI port name moves to the PLAY events header.
- `StatusStripTests` (in `UiLayoutTests`) lays out the strip at 1180 with MOCK + PREVIEW + REC + writes all at their longest. It asserts that nothing overlaps and that the never-collapse slots are present.

### 3.3 Deck and mode keys

**Order on the deck:**

| Key | Shortcut |
|---|---|
| play ▷ | ^1 |
| load (tray ↓) | ^2 |
| edit | ^3 |
| **chop ✂** | **^7** |
| capture (reels) | ^4 |
| rack (chained boxes) | ^5 |
| backups (cassette) | ^6 |

**LEDs report state.** They cut; they never fade.

| Key | LED |
|---|---|
| active mode | white, cap pressed |
| load | blinks 2 Hz while syncing; crimson on a read error |
| edit | dim white while a sample is open |
| chop | dim white when cuts exist |
| capture | crimson while recording or grabbing |
| rack | white while engaged (not orange: orange is info) |
| backups | **hollow ring = no complete backup (writes locked)**; white = complete; blinking = running |

**Other deck keys:**
- **SHIFT** (light grey): held = shift layer. A click latches it for one press. The latched state shows a white LED on the key, and the cell labels stay swapped until the next press or Esc.
- **REC** (TP‑7 lamp key): the same as Ctrl+R. When monitoring is off it is disabled, and the reason is printed in the soft-key hint: `rec needs monitor on (ctrl m)`.
- **VU window:** the L/R `out` ladders. These are the same meters as PLAY's, so the meters are visible in every mode.

### 3.4 Stage frame and honest marks (PLAY only)

**Chrome row** (22 px, above the stage cell):
- left: `▷ play · ep–40 riddim · live midi`
- right: `drag orbit · wheel zoom · double-click reset`
- when MOCK: the yellow tab `MOCK SOURCE · synthetic ep–40 midi, not real hardware`
- when PREVIEW: the violet tab `PREVIEW · demo display frames, not the device`

**Band** (8 px around the stage cell's active child, painted by `StageFrame`):

| State | Band |
|---|---|
| live | `scr.bg` with a hairline |
| MOCK | yellow/black caution stripes |
| PREVIEW | violet dashed line |
| both | outer 5 px hazard, inner 3 px violet |

**The active child** is one of three things. The band wraps all three, and the H1/H2 probes run on each, including `HALO_STAGE_TIER=still`:
- the GL view
- the **return poster**, a JUCE-painted `Image` of the last rendered frame, shown while the GL child reinitialises (§4.3)
- the 3D plan's `StagePlanFallback` 2D poster after `context.detach()`

**Disconnected:** the stage follows the 3D plan's preview demo (D13), so H2 applies. The chrome row adds `no ep–40 connected · connect over usb`.

**Redundant in-scene mark:** requested from the 3D plan (§4.4). The chrome marks are authoritative and never depend on GL.

### 3.5 PLAY (Ctrl+1); mockup `hybrid-play.svg` with grafts

| Region | 1440×900 | 1180×720 |
|---|---|---|
| Stage cell | x 24–1000; GL child x 32–992, y 88–632 (960×544) | x 24–800; GL child 32–792 × 80–504 |
| Rail | x 1010–1416 (406) | x 810–1156 (346) |
| Rail collapsed | 28 px spine: vertical `MONITOR`, L/R mini ladders; the stage widens | same |

The rail is open by default. Click the spine or press Esc to collapse it; use `HALO_RAIL=closed` for shots.

**Rail sections.** Heights are for 1440, with compact heights in brackets. At compact the sections total 64 + 150 + 90 + 30 + 72 + 4×8 = **438 ≤ 462**, and the same sum is tested.

1. **LAST PAD, 84 [64].**
   - Four ReadoutCells (ramp 1/3/5/7). They are read-only, so they are grey: `pad a 5 · note c2 · vel 96 · ch all 16`.
   - With no live feed, every cell shows "–", and a hollow-lamp caption reads `no live feed`.
   - With MOCK, the section title carries a small yellow `MOCK` tag.
2. **MONITOR, 182 [150].**
   - **Header:** `MONITOR` + an `ON` tag + `48.0 khz · 480 fr · ~10 ms`, rounded to whole ms.
   - **Meters:** L and R LevelArcs, tap `out` (after level, rack and limiter), each with a `limit` lamp.
   - **LEVEL:** Numerals L `−12` `db` in enc1 blue, with a 20-segment blue ladder. Encoder 1 owns it.
   - **Output:** an **`out` ActionKey** showing `speakers (realtek)`. It opens the themed PopupMenu of outputs: "Windows default (…)" plus the outputs, with EP‑40 outputs excluded and captioned `ep–40 outputs hidden (feedback)`. The PopupMenu is a desktop window, so it is airspace-safe. Next to it: `limiter −1 dbfs`.
   - **Keys:** StepKeys `monitor` and `● rec`.
   - **Last take line:** `last take · 18 oct 17:29 · 12.4 s` (middle-ellipsised) with `show`.
   - **Info line, in priority order:**
     1. a dropped-samples blocker while recording (`warning: N ms dropped…`, today's copy)
     2. the GRAB result as an info notice (e.g. "grabbed 8 bars", "only 2 of 4 bars in history")
     3. running stats; while recording, a filled crimson `REC 00:12` tag with white text
     4. a failure, as a blocker
     5. otherwise plain `off`, or a blocker `no ep–40 audio input found`
   - **When the input disappears:** the needles rest, and a blocker reads `monitor stopped: ep–40 disconnected`.
3. **GRAB, 106 [90].** Encoder 2, ochre.
   - The title **always** reads `GRAB`. The unit lives in the chips: `4 · 8 · 16 BARS` when clock and transport run, otherwise `5 · 10 · 30 S`. The subtitle says `clock running → bars`.
   - A TapeLane of the last 30 s of history, with the ochre bracket showing exactly what GRAB will take.
   - An ActionKey `grab · ctrl g`.
   - `last grab · 18 oct 17:29 · 8 bars · 10.7 s` (middle-ellipsised) with `show`.
4. **RACK, 80 [30].**
   - At 1440, the sound-path chain `in → echo → spring → sweep → low → out` as 50×44 outlined tiles with mini machines:
     - echo: rings with an orange dot
     - spring: an ochre coil
     - sweep: a curve
     - low: a sine
   - Bypassed tiles are dashed `ramp3` with `· off`.
   - An `ENGAGED` tag, and `echo mix 35` in enc4 orange.
   - Caption `ctrl 5 to tweak`.
   - **At compact it collapses to a one-line chain**: lamp + `rack engaged · echo spring · mix 35`.
5. **EVENTS, at least 3 lines (100 at 1440, 72 compact).**
   - mono 10.5, 18 px rows
   - the newest line in ink; older lines step down ink → `ramp6` → `ramp5` and never go dimmer than `ramp5` (7.1:1)
   - MOCK lines in yellow; error lines with a small filled danger chip and ink text
   - header right: the MIDI port name
   - never 0 px tall; `UiLayout` reserves 3 lines

**Lane** (redundant content only):
- `CLOCK`, ▶/■, Numerals L `180.0` `bpm`, `▶ playing · bar 17.3`
- a **16-step paired beat ruler**, with the current step lit white
- bottom line: `ctrl g grab · ctrl m monitor · ctrl r rec`
- With no clock: `no clock`, and the ruler shows hollow steps. The same state is in the events log.

**Encoders:**

| | Parameter | Press | Shift |
|---|---|---|---|
| 1 blue | LEVEL (silence floor…0 db; default −12) | monitor on/off | — |
| 2 ochre | GRAB length (discrete, 3 steps) | grab now | — |
| 3 grey | ORBIT yaw | reset view | turn = tilt; press = top view |
| 4 orange | ECHO MIX | engage / bypass rack | — |

Soft-key hint: `drag: orbit · wheel on stage: zoom · double-click: reset`.

### 3.6 Shared page skeleton (every mode except PLAY)

- **Page header (44 px):** pictogram tag, `label` mode word, `title`, a dim meta line. On the right: the soft words, a big count, action keys, and **persistent state tags** (ASSIGN ARMED, ENGAGED and so on). The lane never carries these.
- **Canvas.**
- **Lower band.**
- **ParamStrip, SoftKeyRow and Lane**, as in §3.1.
- **Dead cells:** when a page has fewer than four encoder parameters, the extra cells are drawn dead with "—", and their pills are empty capsules.

### 3.7 CHOP (Ctrl+7, or EDIT's `chop`); mockup `hybrid-chop.svg`, with op1 LENGTH and card grafts

CHOP is an `AppMode` and is visible only in CHOP mode, which fixes the bug where it lingered over other modes. Esc returns to EDIT.

| Region | 1440 (y) | 1180×720 (y) |
|---|---|---|
| Header | 56–100 | 50–90 |
| Overview strip (whole file + selection bracket) | 104–118 | 94–104 |
| Ruler (1 s minor, labels every 8 s) | 122–138 | 106–120 |
| **Slicer canvas** (yields, min 120) | 140–344 (204) | 122–(whatever the lower band leaves) |
| Selection bracket `SLICE 4 · 6.2 S → 4.0 S · PLAYING` | below the canvas, 16 | 14 |
| Status line (mono): `12 slices (the 11 strongest of 311 cuts found)` | 18 | 16 |
| Lower band | 390–640 (250) | measured, 200–236 |

**Header:**
- ✂, `CHOP`, `LONGSONG.01`
- meta `64.0 s · 44.1 khz · stereo · from edit 0.0–64.0 s`
- soft words `transient  even` (the mode)
- **`clear cuts` ActionKey**, always visible, so it is not hidden on the shift layer
- Numerals L `12` `SLICES`
- an `esc` chip

**Canvas:** the slicer as specified in §2.5.
- Trimmed tails (AUTO-TRIM or LENGTH) are hatched with a 2 px orange edge and an orange foot tab, plus a `−3.5s` chip when there is room.
- Slice numbers sit top-left of each slice; the selected one is in a white chip.
- The playhead is `hot`.

**Mouse over the canvas:**

| Input | Action |
|---|---|
| click | audition |
| double-click | add a cut |
| drag a marker | move it |
| right-click a marker | remove it |
| wheel | scroll (when zoomed) |
| **Ctrl+wheel** | zoom |
| wheel over a marker handle | nudge that cut 10 ms |
| Alt+wheel over a marker handle | nudge that cut 1 ms |

The Ctrl+wheel zoom is printed in the lane.

**Lower band at 1440** (three zones, hairline separators):

1. **Pads** (x 24–470):
   - GROUP StepKeys `a b c d` (equal widths) and **PROJECT ‹ 3 ›** on the same 30 px row. The send project is chosen here and mirrored in LOAD; both read the same model value.
   - PadGrid, with computed rows (≈ 48). Slice numerals in Numerals S, device legends small, the start pad with a white LED, the selected slice in white, trimmed slices with hatched chips. Clicking a pad sets the start pad, and the wheel over the grid moves it.
   - **Plan column** (130 px):
     - `12 slices · from pad 7 · group a · project 3`
     - a white lamp `all 12 fit on group a`, or a blocker `2 slices don't fit on this group's pads. they're still exported.`
     - a legend
2. **Output** (x 486–930):
   - QUALITY StepKeys (original / high / balanced / lo 22k / lo 11k)
   - CHANNELS (keep / mono / stereo, captioned `shared with edit`)
   - NAME as NameCells `LONGSONG`
   - totals in Numerals S: `5.21 mb · 12 pads · ~2:10 to send`, with `~40 kb/s · mono or lo quality is quicker`
   - a memory DotTimeline: `used 99.7 · +5.2 these · 17.0 mb free after`
3. **Send** (x 946–1416):
   - **Notice, in priority order:**
     1. a blocker: too long with AUTO-TRIM off / too big / nearly full, in today's copy
     2. info: `LENGTH: 7 slices cut to 4.0 s (your LENGTH), from each slice's start, with a fade-out. The shaded tails are dropped.`
     3. a plain unlocked note / padlock locked line / plain disconnected line
   - the backup line: open padlock + white lamp + `backup 18 oct 21:04 · 477 sounds, 9 projects · complete`
   - actions: `export slices`, `show`, **`send to pads`** (device-write key)
   - While sending, the actions are replaced by a Progress component with reels.

**At 1180:**
- The pads zone is x 24–420.
- The output and send zones stack in one column (x 432–1156):
  - quality | channels on one row
  - name | totals
  - memory
  - notice (up to 3 lines)
  - backup line
  - actions
- That needs 200–236 px. **The canvas yields height to the notice**, down to its 120 px minimum, and never the other way round. `UiLayoutTests` asserts the 16-slice "over 20 s" blocker fits at 1180×720 and at 1180×660.

**Encoders:**

| | Parameter | Gauge | Press | Shift |
|---|---|---|---|---|
| 1 blue | SENSITIVITY (commits in `endGesture`, as today's "applies on release") | tick dial, `311 cuts seen` | detect | press = clear cuts (also on the header key) |
| 2 ochre | SLICES `fit · 4 · 8 · 16 · all` (discrete) + FIT tag | 16-dot "pads left" row | even on/off | — |
| 3 grey | **LENGTH 1–20 s** (0.5 s steps) | **tick ruler with a triangle pointer; a red tick at the EP‑40's 20 s limit**; caption `7 slices cut`, which `limits 7` (orange outline) replaces when LENGTH is what binds; AUTO-TRIM tag (`TRIM` at compact) | auto-trim on/off | — |
| 4 orange | SLICE (select 1…N) | mini timeline dot | audition | turn = nudge the selected slice's leading cut 10 ms; Alt+shift-turn = 1 ms |

- In `even` mode, cell 1 is dead with the reason `even – no detection`.
- Soft keys: `detect · even · auto-trim · audition`.
- Lane: keycap chips `1–9 0 hear · dbl-click add cut · drag move · del remove · ctrl+wheel zoom · esc edit`, plus `ctrl e export · ctrl ⏎ send`.

**Keyboard:** 1–9 and 0 audition slices 1–10; Delete/Backspace removes the selected cut; Tab moves to the next marker.

**States:** detecting (canvas dot sweep plus the status line), no cuts, overflow, sending (per-pad progress), send failed (blocker), exported (white lamp with `show`).

### 3.8 EDIT (Ctrl+3)

| Region | 1440 (y) | 1180×720 (y) |
|---|---|---|
| Header | 56–100 | 50–90 |
| Trim readout row: `▶  in 0.000 s – out 4.000 s  (4.000 s)`, each value in its marker's colour on glass | 104–122 | 94–110 |
| **SampleBand** (yields, min 140; an overview strip appears in its top 14 px when zoomed) | 126–386 (260) | 112–292 (180) |
| Lower band | 396–640 | 300–512 |

**Header:**
- edit pictogram, `EDIT`, the title, and meta `44.1 khz · 16-bit · stereo · 120.000 s`
- soft words `trim  shape` choose the page. **The shape page is EDIT's shift layer**: holding Shift shows it, and clicking `shape` latches it.

**Empty state:** an empty paper band, `no sample open`, and the header line `nothing open: pick a take, grab or file` with the `open library` ActionKey, which goes to CAPTURE.

**SampleBand markers:**
- **in** = blue
- **fade-in end** = ochre
- **fade-out start** = grey
- **out** = orange

These are line and tab colours on paper; none of them is text. The fades are grey wedges, and the area outside the trim is hatched over `paperShade`. The playhead is `hot` during audition. Dragging the handles works as it does today, and the wheel over a marker turns its encoder.

**Band states:**
- decoding: a dot sweep across the band plus `decoding…`
- error: a blocker inside the band
- no sample

**Lower band at 1440:**
- **Left (x 24–700):**
  - the CHANNELS and QUALITY StepKey rows
  - the NORMALIZE tag
  - the size block: Numerals M `20.19` `mb`, plus mono `44.1 khz 16-bit stereo 120.000 s`
  - a memory DotTimeline `16.5% of ~122 mb (exact bytes, before the device's file overhead)`, which wraps rather than truncates
- **Right (x 716–1416):**
  - **Notice, in priority order:**
    1. blocker `needs 20.2 mb but the ep–40 has 14.9 mb free`
    2. info `AUTO-TRIM: over 20 s, so SEND sends the first 20 s of the selection, with a fade-out…`
    3. plain unlocked / padlock locked / plain disconnected (copy verbatim)
  - backup line
  - actions: `audition`, `export`, `chop`, **`send to ep–40`** (device-write key)
  - status line with `show`; `Export failed…` is a blocker
  - **while sending:** a Progress card with reels, `42% · about 1 min left`
- At 1180 the two columns are 560 and 560 px.
- **Each page owns its status line**, so CHOP's status never appears in EDIT.

**Encoders:**

| Page | 1 blue | 2 ochre | 3 grey | 4 orange |
|---|---|---|---|---|
| trim | IN, press = audition | FADE IN 0–2000 ms, press = zoom to the start | FADE OUT 0–2000 ms, press = zoom to the end | OUT, press = reset trim |
| shape (shift) | GAIN −24…+12 db, press = reset | CHANNELS keep/mono/stereo (discrete), press = cycle | QUALITY (5 steps), press = cycle | NORMALIZE on/off, press = toggle |

Lane: `▶ 00:01.240 · space audition`, `ctrl e export · ctrl ⏎ send`.

### 3.9 LOAD (Ctrl+2): the EP‑40 device library

**Available state, 1440:**

**Header:**
- device pictogram, `LOAD`, `ep–40 library`
- meta `os 2.5.0 · 477 sounds`. During sync it becomes a step DotTimeline: `saying hello → file system → storage → listing → reading sound 58 of 477`.
- header errors appear as blockers
- on the right, the `refresh` ActionKey, disabled while syncing with the reason `syncing…`

**Storage row (y 104–128):** a 61-dot DotTimeline and `99.7 mb used · 22.2 mb free of 121.9 mb`. Above 90% the free dots are crimson-outlined and a blocker line appears.

**Left (x 24–560):**
- **PROJECT 1–9 and GROUP a–d StepKeys on one 30 px row**, with equal widths within each group. This fixes the shrinking bug.
- A PadGrid with computed rows (about 100 at 1440, 56 at 1180×720, 48 at 1180×660), cells 172 wide: the sound name in `ui` 13 on 2 lines, the legend top-left, the slot top-right.
- Click = select and audition the pad's sound.

**ASSIGN armed:**
- a persistent `ASSIGN ARMED` tag in the **header**, with the instruction beside it: `click a pad to put "BIGKICK" on it (project 3)`, plus the soft word `cancel`
- dashed `hot` outlines and a `+` on every pad
- the lane only repeats the instruction
- a pad click goes to `WriteGate` → ConfirmCard (§3.13)

**Right (x 576–1416):** a BrowserList with three columns:
- category 160 (`ALL, KICKS … USER`)
- sound 480 (`002  BIGKICK`)
- facts 200 (`0.50 s  m/st`)

The facts column has a thumbnail tile, **drawn only once the sound has been downloaded for audition**. Otherwise it is an outline captioned `not downloaded`. Double-click = audition.

**Status line (y 572–592):**
- results in ink with a white lamp
- `Assign failed: …` and `Delete failed: …` as blockers
- downloads as plain text

**Actions (y 596–640):** `audition` · `save to library` · **`assign to pad`** (device-write toggle) · **`delete`** (destructive outline, on the right).

**Locked:** assign and delete are padlock outlines, with an inline line `back up the EP-40 in BACKUPS (ctrl 6) to unlock`. No tooltip is needed.

**At 1180:** the pads column is 440 (PROJECT 9×30 + GROUP 4×36 on one row; rows 56) and the list is 680 (category 130, sound 380, facts 170). The column needs 30 + 8 + 4×56 + 3×6 = **280 ≤ 318**, and this is tested.

**Encoders:**

| | Parameter | Press |
|---|---|---|
| 1 blue | CATEGORY | `all` |
| 2 ochre | SOUND | audition |
| 3 grey | PROJECT 1–9 (discrete) | read pads |
| 4 orange | PAD | assign (via `WriteGate`); dead with a padlock reason when locked |

**Unavailable states.** The body becomes one centred card with a 64 px pictogram and today's copy. `refresh` stays, and the strip cells are dead.

| State | Card |
|---|---|
| MOCK | yellow `MOCK SOURCE` tag; "Halo is running on the MOCK source, so there's no real EP-40 to read." |
| Disconnected | USB pictogram; "Connect the EP-40 over USB. Halo reads its sounds and pads automatically." |
| Port busy | **blocker**; "The EP-40's MIDI output is in use by another app (a DAW or TE's tool). Close it and replug." |
| Waiting | blinking hollow lamp; "Waiting for the EP-40 to answer..." |

### 3.10 CAPTURE (Ctrl+4): recorder plus local library

**Header:** reels pictogram, `CAPTURE`, Numerals M `23` `items`, and the actions `import` and `folder` (which reveals the recordings folder).

**Tape machine (1440 y 104–330; 1180 y 94–250):**
- the counter `00:12:40` in Numerals L, with a scrub tick ruler
- supply and take-up reels: the wound disc on the take-up reel grows with the take length
- the tape path, a square head block and the `hot` playhead
- **L/R input LED ladders, tap `in`**: the raw EP‑40 input, which is exactly what raw takes and GRAB record. This uses the new **input metering tap** from R9, because today's meter sits after level, rack and limiter.
  - With PRINT FX on, a second pair of ladders, tap `print`, shows the post-rack signal that is actually being recorded, captioned `print fx: takes include level and rack`.
- a TP‑7 **crimson REC lamp**: pulses while armed, solid while recording
- GRAB StepKeys and the `grab` key, the same as PLAY
- a `last take · title · 12.4 s` line with `show`
- **while recording:** the dropped-samples blocker (`warning: N ms dropped…`) appears here as well as in PLAY
- **with no input:** a blocker `no ep–40 audio input found`

**Library (1440 y 338–640; 1180 y 258–512):**
- a search field (type to filter; Ctrl+F focuses it)
- kind chips `all · takes · grabs · files` and sort chips `date · name · length`
- a BrowserList with 40 px rows:
  - a kind tag: GRAB is an **outlined** ochre tag, TAKE an ink tag, FILE an outline
  - a 60×18 stepped thumbnail. Peaks are computed lazily on a background thread, with an empty outline until ready.
  - the title, and `4.0 s · 44.1k st`
- double-click = open in EDIT
- bottom actions: `edit` and `chop` (disabled with no selection, captioned `select an item`), and `forget` (imported files only)

**Empty state:** a component above the list (not under the ListBox): two still reels and "Nothing here yet. Record a take or GRAB in PLAY, or IMPORT / drop audio files here."

**Drag hover:** a 2 px `hot` dashed outline around the glass, and `DROP AUDIO TO IMPORT` in `title`.

**Import result:** `Imported 3 files` (white lamp), or plain `Nothing new to import (unsupported, or already here)`.

**Encoders:**

| | Parameter | Press |
|---|---|---|
| 1 blue | ITEM | audition |
| 2 ochre | FILTER (discrete) | open in edit |
| 3 grey | GRAB length (discrete) | grab now |
| 4 orange | MONITOR LEVEL (the same `monitorGainDb`), captioned `what you hear · in takes only with print fx` | `● rec` |

### 3.11 RACK (Ctrl+5), full page

**Header:**
- rack pictogram, `RACK`, `dub`
- tags:
  - `ENGAGED` (filled ink, white lamp) / `BYPASSED` (outline)
  - `PRINT FX`: disabled while recording, with the inline reason `can't change while recording`
  - `MONITOR`
- soft words `echo spring sweep low` (focus; the keys 1–4 also focus)

**Canvas (1440 y 104–470):**
- an `in` port, four **paper tiles** (`paper`, radius 4, about 250×300) joined by hairline arrows, then `limiter −1 dbfs → out`
- L/R ladders at **`pre-rack`** (after level, before the rack) and **`out`**, so the effect is visible. Both are tap-labelled. `pre-rack` is the new tap from R10.
- **Each tile** has:
  - a title in `#000005`
  - an ON/BYPASS tag
  - encoder-shade dots in the corners
  - a black line illustration: echo reels plus travelling rings, spring coil, filter curve labelled LP/BP/HP, speaker cone
  - **its values in the corners as black (`#000005`) Numerals XS, each with a 3 px `encN` bar beside it**, never encoder-coloured text on paper
- a bypassed tile is dark (`scr.panel`), hatched, with dim lines and `bypassed` in `scr.dim`
- the focused tile has a 2.5 px `hot` outline
- a module that is on with mix at 0 gets an info notice: `mix 0: this module is silent`

**Lower band (y 478–640):**
- echo extras: `FREE/SYNC` tag, `tap`, `clock 120 bpm` (or its source), and the division StepKeys `⅛ ⅛. ¼ ⅛T` (SYNC only)
- TIME shows the effective ms in dim while in SYNC
- notices:
  - info `start MONITOR to hear the rack (ctrl m)`
  - info `BYPASSED: click ENGAGE to hear the rack`
  - plain `on the monitor route, before the -1 dBFS limiter`

At 1180, tiles are 200×230 and the lower band is 120.

**Encoders are bound to the focused module.** Dead slots are dead cells.

| Module | 1 blue | 2 ochre | 3 grey | 4 orange |
|---|---|---|---|---|
| echo | TIME 40–1200 ms (shows the division in SYNC) | FEEDBACK | TONE (shift: WOW) | MIX |
| spring | MIX | DECAY | TONE | — |
| sweep | SWEEP (LP/BP/HP) | RESO | — | — |
| low end | AMOUNT +0…+6 db | — | — | — |

- Pills: `tap` (echo only), `sync` (echo only), `on/off`, `engage`.
- Turning a module on also engages the rack, as today.
- Double-click resets to the initial value; drag sensitivity is 180 px, as today.

### 3.12 BACKUPS (Ctrl+6)

**Hero lock plate** (1440 y 56–176; 88 px at ultra-compact). It is the largest element on the page.

| State | Plate |
|---|---|
| No device | a hollow lamp and `Connect the EP-40 (see LOAD) to back it up` |
| Locked | a 64 px closed padlock; the `WRITES LOCKED` tag (outline, hollow lamp); `This EP-40 (SERIAL) has no complete backup yet: writes are locked`; an info line `SEND, ASSIGN and DELETE unlock after one` |
| Unlocked | an open padlock; the `WRITES UNLOCKED` tag (filled ink, white lamp); `… has a complete backup: writes are unlocked` |

Keys on the plate:
- **`back up now`**: primary. It is a read, so it gets no write LED.
- `cancel`: shown while running.
- `open folder`.

**Canvas, running:** a full-size Progress component:
- big reels
- a **9 + 477 dot matrix**, one dot per project and sound: saved = ink, current = blinking white, pending = hollow. It is one `RectangleList`.
- Numerals XL `58` `/ 477` and `about 24 min left`
- `saving sound 58 of 477`
- `keep the ep–40 connected and awake`
- errors such as `Can't create …` as blockers

**Canvas, idle:** a shelf of cassette drawings, 200×120 each, wrapping and scrolling vertically (yields, min 120). Each cassette shows:
- the date and time in Numerals S
- `477 sounds · 9 projects · os 2.5.0`
- a `COMPLETE` ink tag, **or** an `INCOMPLETE` **hatched outline tag** with `incomplete · doesn't unlock writes` in `scr.dim`. Crimson is reserved for a run that failed with an error, which gets a blocker tag plus its error line.
- double-click or `show` reveals the folder

**Empty shelf:** an empty cassette drawing and `No backups yet. BACK UP NOW copies every sound and project; nothing on the EP-40 changes.`

**Explainer** (`body`, `scr.dim`): today's copy, verbatim.

**Strip:**
- Encoder 1 (blue) is SHELF; press = reveal.
- Cells 2–4 are **ReadoutCells** (`sounds 477 · projects 9 · last 18 oct`). This is the read-only grammar, with dead pills.

All backup state shown anywhere comes from the cached `Ep40Backup::latestComplete(serial)` (§5.1, H7).

### 3.13 ConfirmCard (every device write, through WriteGate)

`WriteGate::request (WriteSpec, action)` takes:
```
{ kind, title, body (today's copy verbatim), confirmLabel, destructive,
  available(), reason() }
```
- `available()` is the same predicate the write key uses: `writesUnlocked() ∧ ¬writer.isBusy() ∧ connected`.
- `backupLine` is filled from `latestComplete(serial)`.

**Card:**
- 480 px wide, `paper`, radius 6, **hard 3,3 px black shadow**, over a 70% scrim across the whole window
- a **crimson header bar** with white `tag` text: `SEND TO EP–40?` / `SEND TO PADS?` / `ASSIGN TO PAD?` / `DELETE FROM EP–40?`
- a pictogram row: file → ep–40 → slot or pads
- the body in `body` `#000005`, including today's optional sentences ("It's auto-trimmed…", "7 slices are cut to 4.0 s.")
- **the backup line, always present**: an open padlock + `backup 18 oct 2026 21:04 · complete · 477 sounds`
- a dot timeline for the time estimate

**New ASSIGN copy** (U20): `Put "<sound>" on pad <group> <key> in project <N>? It currently plays "<old>". Nothing is deleted: "<old>" stays in the library.` If the pad is empty, the second sentence reads `The pad is empty now.`

**Keys:**
- `cancel`: grey outline, **default focus**, Esc.
- `send` / `assign` / `delete`: filled crimson, white text. Enter activates it **only after Tab** has moved focus to it; Enter on a fresh card cancels.

**Live gate:**
- While the card is open, `available()` is re-evaluated at 2 Hz.
- If the gate closes (the device disconnects, a send starts, or the backup disappears), the confirm key turns dead and the card shows the printed `reason()` as a blocker inside it.
- On confirm, WriteGate forces a synchronous backup rescan, re-checks `available()` once more, and only then calls `action`. If the check fails, nothing is written and the card shows the reason.
- `show` with writes locked asserts in Debug and cancels in Release. Upstream gating makes that unreachable (H3).

**Airspace:** if the stage is visible, it is hidden while the card is open (H6). No write is reachable from PLAY anyway.

**Other dialogs:** every remaining `AlertWindow` gets the same look through `HaloLookAndFeel::createAlertWindow` and `drawAlertBox`. The native `FileChooser` stays native.

### 3.14 Cross-cutting states

| State | Treatment |
|---|---|
| MOCK | Strip honest slot `MOCK`; device lamp yellow `synthetic`; stage caution band plus tab; yellow tag on PLAY LAST PAD and on the LOAD card; yellow event lines; `writes n/a` |
| PREVIEW / WAIT / LIVE | PREVIEW: violet strip tag, violet feed lamp, violet dashed stage frame plus tab. WAIT: hollow lamps blinking 1 Hz (feed, device). LIVE: white lamps |
| Disconnected | Hollow device lamp `no device`; ring `dark`; LOAD and BACKUPS LEDs off; device-write keys dead with `connect the ep–40 to send. export works now.`; PREVIEW applies to the stage |
| Port in use | Crimson device lamp `in use by another app`; blocker in LOAD; event line |
| Syncing / loading | LOAD LED blinking; header step DotTimeline; waveform dot sweep `decoding…`. No spinners |
| Busy / transfer | Ring `tx 42%`; Progress card on the page; all write keys dead with `busy: sending to the ep–40`; an open ConfirmCard goes dead live |
| Writes locked | Strip padlock; BACKUPS LED hollow; padlock keys plus inline lines in LOAD, EDIT and CHOP; the BACKUPS hero plate |
| Errors | Blocker where it happened; event line with a crimson chip; ring `err` (filled tag); crimson ring pool; the `limit` lamp crimson for 1.5 s on pre-limiter peaks |
| Info | Orange outlined notice with "i" |
| Recording problems | Dropped-samples blocker in PLAY MONITOR and CAPTURE while recording, plus the event line |
| Legacy panels (R4a–R11 only) | See §5.1: an honest `not rebuilt yet` plate |
| Tooltips | One `juce::TooltipWindow` owned by MainComponent, styled as a small paper card. Nothing depends on it: every reason is also printed |

### 3.15 Keyboard and mouse (complete map)

| Input | Action |
|---|---|
| Ctrl+1…6 | play / load / edit / capture / rack / backups, **unchanged** |
| **Ctrl+7** | chop |
| Ctrl+M / Ctrl+R / **Ctrl+G** | monitor / rec / grab. All three are now printed (PLAY lane, GRAB block, REC key) |
| F1–F4 | focus encoder 1–4; ↑↓ turn, Alt fine, Ctrl coarse, Shift = shift binding, Space/Enter press, Esc unfocus |
| Wheel / Alt+wheel / Ctrl+wheel | turn / fine / coarse on any owned element. **CHOP canvas: Ctrl+wheel zooms** |
| Shift (held) / SHIFT click | shift layer / latch it for one press |
| Ctrl+E / Ctrl+Enter | export / send (send always goes through WriteGate) |
| Ctrl+F | focus CAPTURE search |
| Space | audition / stop (EDIT, CHOP, LOAD, CAPTURE) |
| Esc | close card → leave CHOP to EDIT → cancel ASSIGN ARMED → unfocus encoder → collapse rail |
| CHOP | 1–9 and 0 audition; Delete/Backspace removes a cut; Tab next marker |
| Lists | ↑↓, Enter, type-to-find |
| Stage | drag = orbit, wheel = zoom, double-click = reset (camera feel from 3D plan §3.6) |

`UiStateTests` holds this table as data. It asserts that no (context, modifier, input) triple maps to two actions, and that Ctrl+1…6 are unchanged.

---

## 4. Motion, sound-reactive details, and the stage

### 4.1 One clock

- `Design/Motion.{h,cpp}` provides `MotionClock`: one `juce::VBlankAttachment` per window. It is in `juce_gui_basics`, so no new module is linked.
- It attaches while anything animates and detaches once everything has settled.
- Animations register a `tick(dt) → bool stillMoving`.
- `Timer`s remain only for slow polling of *cached* state: status at 2 Hz, backup and progress at 4 Hz, and the backup-folder rescan at 0.1 Hz on a background thread.
- Debug `HALO_UI_FREEZE=1` pins MotionClock's time source and all animation phases, so shots are deterministic.

### 4.2 Behaviours

| Element | Behaviour |
|---|---|
| Encoder turn | The owned cell's top bar turns `hot` within 1 frame. The value **snaps**, with no tween. The PopupCard appears within 80 ms, holds 1 s, then cuts. The deck pointer follows the value at once. Expensive commits wait for `endGesture` |
| Mode switch | Hard cut. The key drops 1 px and its LED cuts on. Returning to PLAY shows the return poster at once, then the GL child replaces it on its first rendered frame (§4.3). There is no chrome fade |
| Lists / tags / lamps | Cut. Only the scroll position eases (120 ms) |
| Level needles | Rise 10 ms, fall 300 ms. LedLadders use the same ballistics, quantised. Peak hold 1.5 s, then 20 db/s |
| Meters repaint | Only their own rectangle, only when the quantised value changes. This replaces MonitorPanel's 30 Hz `infoArea` repaint |
| Reels (CAPTURE, upload, backup) | Angular velocity follows the transport or the measured KB/s. They coast to a stop with τ 0.6 s and hold still while armed |
| GRAB | On save, the tape-lane bracket flashes (`flash`) and slides into the "last grab" line (cut) |
| Pads (PadGrid) | Hit flash 90 ms, then decay over 250 ms. The 3D pads follow the 3D plan (D3, M10) |
| CHOP scissors | When a cut is placed or moved, the ✂ on its tab closes and opens (2 frames, 120 ms). Audition: the slice flashes white |
| Playheads | Linear position per frame |
| Beat ruler (PLAY) | The step lights from MIDI clock, as a cut |
| RACK machines | Signal-driven only: echo rings travel at the delay time and fade with feedback; the spring coil jiggles on transients and decays with DECAY; the sweep peak tracks SWEEP and RESO; the cone pumps with RMS. Each reaction is under 250 ms with no overshoot. Bypassed or silent machines stand still |
| Red | Pulses at 1 Hz only for REC (armed or recording), a write in progress, or a live blocker on the ring. Blocker boxes never blink. Idle REC is not red |
| Reduced motion | When Windows "show animations" is off (`SPI_GETCLIENTAREAANIMATION`, read-only), machine motion and reel coasting stop. Meters, playheads and progress keep working |

### 4.3 The stage on black

Everything here goes through the 3D plan's API. This plan only supplies values and budgets.

**Look:**
- `Theme::stageLook()` builds a `StageLook` from `StageLook::dark()` (3D plan M1):
  - backdrop = `scr.bg`
  - floor = near-black, with a dot-grid `floorTexture` (`scr.gridDot`, 24 px) fading to the backdrop. This answers 3D plan O2: a dark plain sweep with a dot grid.
  - shadowTint = black
  - ring colours per `RingState`:

| RingState | Floor pool |
|---|---|
| discovering | orange sweep |
| connected | 20% |
| monitoring | 40% |
| recording | `danger` breathing |
| transfer | orange progress |
| error | `danger` |

  The orange pool is GL-only; Halo's UI never uses orange for ring state.
- The 3D plan's border-equality check ensures the stage edge blends exactly into the glass.

**Lighting and materials:** owned by the 3D plan (M2 and M3: key, rim strip, fill, PBR Neutral, floor reflection about 0.15 in Dark). That plan's gates G1–G3 judge them on the `dark()` preset this redesign uses.

**Framing:**
- The 3D plan's fit-to-bounds camera (§3.6) fixes cropping at every aspect ratio.
- PLAY's encoder 3 drives yaw, and Shift + encoder 3 drives pitch. Press = hero reset; Shift+press = the `top` preset.
- Wheel zoom stays on the stage.

**Hiding has a real cost, so it is measured and budgeted.**
- JUCE detaches the GL context when the component is not showing (`juce_OpenGLContext.cpp:1143-1158`).
- Today `newOpenGLContextCreated` then re-parses the 3.8 MB hmesh, recompiles the shaders and rebuilds the FBOs (`StageComponent.cpp:256-290`).
- The rules:
  - **Budget:** mode switch to first stage frame **≤ 150 ms** on the owner's GPU, logged by `HALO_STAGE_STATS=1` as `stage: first frame after show N ms`. This is an R4a acceptance item.
  - **Return poster:** on leaving PLAY, the stage hands over its last frame. The 3D plan already reads frames back for the DisplayRenderer path, and one readback per mode switch is acceptable. `StageFrame` paints that frame in the stage cell while the GL child is hidden, and the GL child is shown only after its first frame has rendered. The poster is painted *instead of* the GL child, never over it, so H6 holds. The honest band wraps the poster too.
  - **Interface request** (§4.4): parse the mesh once on the CPU outside the context callback, and do only the GPU upload in `newOpenGLContextCreated`.
- **Cost outside PLAY:** the stage is hidden and renders 0 frames, using `setContinuousRepainting(false)` until the 3D plan's M10 on-demand funnel lands. This is verified with `HALO_STAGE_STATS=1`.

### 4.4 Interface requests to the 3D plan (owner-coordinated, non-blocking)

1. **Provenance cue in-scene.** A `StageLook` field `provenance { live, preview, mock }` that varies the Halo-owned floor pool: dashed yellow segments for mock, a violet tint for preview. It is subject to that plan's D4 truthfulness rule. It is **not** a stamp on the device LCD, because the device does not show one. Redundant to §3.4.
2. **Fast re-show:**
   - CPU mesh parse cached outside the context, so only GPU upload happens on re-creation.
   - `lastFrameImage()` (a `SoftwareImageType` image captured on hide).
   - A `firstFrameRendered` callback so `StageFrame` can swap the poster for GL.
   - Target ≤ 150 ms (§4.3).
3. **Disconnected:** optional desaturation of the stage (exposure about 60%). If declined, PREVIEW labelling alone carries it.
4. **Camera setters** for encoder driving: `setYaw / setPitch / preset()`, with the 3D plan's inertia bypassed between the bus's `beginGesture` and `endGesture`.
5. **One READY line:** `ui_shots` waits for `HALO_UI_READY` and, in PLAY, for the 3D plan's `HALO_STAGE_READY`. No new stage line is added.
6. **Fallback poster:** `StagePlanFallback` exposes its bounds so `StageFrame` can wrap it in the honest band.
7. **Answers to the 3D plan's open items:**
   - O2: dark dot-grid floor
   - O4: drift off and no toggle (U18)
   - Palette: graphite UI uses `dark()`; the field skin also uses `dark()`, because the screen stays black
   - DD‑006e stays in force for the silkscreen (U14)

---

## 5. Implementation in JUCE 8

### 5.1 Architecture

```
Source/Core/                      (HaloCore: pure C++20, unit-tested in HaloTests, no JUCE)
  UiTokens.h                      palettes as uint32 (graphite, field, legacyPaper) + contrast/ΔE2000 helpers
  UiFontMetrics.h                 generated by tools/fonts/build_fonts.py: advances, tracking, cap heights per role
  UiLayout.{h,cpp}                frame regions, cell/encoder anchors, compact/ultra/narrow rules, strip collapse,
                                  per-page region solver with yielding canvases, text-fit queries
  HonestState.{h,cpp}             inputs {source, feed, connected, libraryReady, writesUnlocked, latestBackup?, busy}
                                  -> HonestFlags, status-slot list, write-gate view, pickNotice()
                                  (it never re-derives writesUnlocked; AppModel is the one formula)
  EncoderMath.h                   detents, fine/coarse, discrete steps, drag mapping, gesture-end debounce
Source/Device/Ep40Backup.{h,cpp}  + latestComplete(serial) -> std::optional<Entry>, cached; invalidated by
                                  start()/finish()/cancel(), a 10 s background rescan, window activation,
                                  and forceRescan() (WriteGate's confirm)
Source/App/HaloPaths.h            one place for Music\Halo\{Recordings,Export,From EP-40}, Documents\Halo\Backups,
                                  settings, protocol trace; Debug HALO_DATA_ROOT redirects all of them
Source/UI/Design/
  Theme.{h,cpp}                   struct Theme { Palette, Metrics, TypeScale, Motion }; const Theme& theme();
                                  setTheme() + ChangeBroadcaster; presets graphite(), field(), legacyPaper(), legacyDark();
                                  stageLook() -> StageLook
  Fonts.{h,cpp}                   HaloFonts singleton (owned by HaloApplication); Typeface::Ptr per face;
                                  font(role) via withPointHeight; kerningFactor(face, em); override self-check
  Numerals.{h,cpp}                cached glyph Paths, draw(g, text, capH, colour)
  Pictograms.{h,cpp}              cached Paths on a 16 px grid (every symbol in the UI)
  Paint.h                         snap(), hairline(), hardShadow(), hatchFill() (cached NativeImageType tile), lamp(),
                                  tag(), dotTimeline(), stepKey(), pill()
  Motion.{h,cpp}                  MotionClock, Ballistics, Coast, Flash; HALO_UI_FREEZE
  EncoderBus.{h,cpp}              bindings, focus, shift layer, hover links, gestures
  WriteGate.{h,cpp}               the only UI caller of Ep40Writer writes; live gate; ConfirmCard
  Primitives/                     GlassScreen, StatusStrip, Deck (DeckKey, DeckEncoder, Grille, VuWindow),
                                  ParamStrip (ParamCell, ReadoutCell), SoftKeyRow, Lane, PopupCard,
                                  StepKeyRow, ActionKey, Notice, PadGrid, LevelArc, LedLadder, TapeLane,
                                  BrowserList, NameCells, Progress, Waveform (WaveCache, SampleBand, Slicer),
                                  StageFrame, ConfirmCard
  Gallery.{h,cpp}                 Debug-only fixture page (HALO_UI_GALLERY=1)
Source/UI/Pages/
  PlayPage, LoadPage, EditPage, ChopPage, CapturePage, RackPage, BackupsPage
Source/UI/HaloLookAndFeel.{h,cpp} thin: TextButton/ComboBox/PopupMenu/ScrollBar/AlertWindow/TooltipWindow
                                  mapped onto Paint.h; getTypefaceForFont; set as default L&F
```

**Two test targets:**
- **`HaloTests`** (existing): HaloCore + doctest. It runs `UiTokensTests`, `UiLayoutTests` (including StatusStrip and text fit), `HonestStateTests`, `EncoderMathTests`, and the extended `MonitorDspTests`.
- **`HaloUiTests`** (new): links JUCE's gui modules, uses a `ScopedJuceInitialiser_GUI`, and adds doctest. It runs:
  - `FontsTests`: measures the cap height of "H" per role against the table to within ±0.5 px, checks that the tracking conversion matches the CSS em spec, and checks that symbol codepoints are absent from text roles
  - `UiStateTests`: single getters, the keymap/modifier table, the CHOP→RACK switch
  - `WriteGateTests`: every write kind opens a card; locking writes while the card is open, then confirming, performs no write
- `tools/dev/check_write_sites.py` runs as a CTest step (H3).

**Theme access:**
- The **13** App/UI files that bind `const auto& c = HaloColours::graphPaper;` at file scope switch to `theme()` calls:
  - `Source/App/Main.cpp`
  - `Source/UI/{BackupsPanel, ChopSurface, EditPanel, HaloLookAndFeel, LibraryPanel, LoadPanel, ModeBar, MonitorPanel, PlayRail, RackPanel, StatusBar, WaveformView}.cpp`
- `StageComponent.cpp` is handled by 3D plan D7.
- `HaloColours.h` becomes a forwarding shim until R13 deletes it.
- `setTheme()` broadcasts: top-level components repaint once, and the stage gets `setLook()`.

**Legacy panels during migration (R4a–R11).**
- **Placement:** each not-yet-rebuilt panel keeps its **current rail width** (37% of the window, clamped 520–640; RACK 420), docked at the right of the page body. The rest of the body shows a dim plate: the pictogram plus `page not rebuilt yet · arrives in R#`. It is an honest state, never a fake page. `ChopSurface` fills the whole body, as it does today over the stage.
- **The `legacyDark` role mapping:**

| Legacy colour | New role |
|---|---|
| paper | `scr.panel` |
| paperHigh | `scr.wave` |
| ink | `scr.ink` |
| inkSoft | `scr.dim` |
| metal | `scr.frame` |
| riddimGreen | `ramp6` |
| **orange (toggles, arcs, selection)** | **enc4** |
| warning | `danger` |

- Info orange applies only where R3 has explicitly switched a note to `info`.
- Each page milestone deletes the panel it replaces.

**Model touch points (small, explicit):**
- `AppModel::AppMode` gains `chop`, and `EditSession::chopOpen` maps to it.
- `AppModel::monitorGainDb()` is the single getter (H7).
- `AppModel::writesUnlocked()` keeps today's formula (library ready ∧ complete backup for the serial) but reads `latestComplete()` from the cache, so it never touches the filesystem.
- The send-project value is exposed for CHOP.
- `MonitorEngine` gains two **metering taps**. They cover peak and RMS per block via the existing `MeterMath`, and nothing in the audio path changes:
  - `in`, at the raw recorder/history tap (R9)
  - `preRack`, after gain and before the rack (R10)
  - Both publish through the same lock-free meter mechanism as today's `out`.

**No primitive inherits LookAndFeel colour IDs.** Stock widgets go through `HaloLookAndFeel` only.

### 5.2 Fonts and assets in CMake

```cmake
juce_add_binary_data(HaloAssets
    HEADER_NAME HaloAssets.h
    NAMESPACE HaloAssets
    SOURCES
        assets/EP40.hmesh
        assets/halo_screen_indicators.png
        assets/fonts/LexendGiga-Light.ttf    assets/fonts/LexendExa-Medium.ttf    assets/fonts/LexendExa-SemiBold.ttf
        assets/fonts/InterTight-Light.ttf    assets/fonts/InterTight-Regular.ttf  assets/fonts/InterTight-Medium.ttf
        assets/fonts/InterTight-SemiBold.ttf assets/fonts/JetBrainsMono-Regular.ttf
        assets/fonts/DepartureMono-Regular.ttf)
option(HALO_LOCAL_FONTS "Use TE faces from assets/fonts/local (never committed)" OFF)
```

- `HaloFonts::init()` calls `Typeface::createSystemTypefaceFor (Span<const std::byte>)` once per face and keeps the `Ptr`s for the app's lifetime. It logs `fonts: 9 faces registered` and asserts the count.
- `HaloLookAndFeel::getTypefaceForFont` maps the family names, so ComboBox, PopupMenu and AlertWindow get Inter Tight.
- `HaloType::monoFace()`'s `findAllTypefaceNames()` scan is deleted.
- If any face fails, the fallback is Segoe UI and Cascadia Mono, and the log says so.
- Icons are code (`Pictograms`), not SVG files. No PNGs are added.
- New `HaloCore` sources: `Source/Core/UiLayout.cpp`, `Source/Core/HonestState.cpp`, the generated `Source/Core/UiFontMetrics.h`.
- New test files:
  - `Tests/UiLayoutTests.cpp`, `Tests/HonestStateTests.cpp`, `Tests/UiTokensTests.cpp`, `Tests/EncoderMathTests.cpp` (in `HaloTests`)
  - `Tests/Ui/FontsTests.cpp`, `Tests/Ui/UiStateTests.cpp`, `Tests/Ui/WriteGateTests.cpp` (in `HaloUiTests`)

### 5.3 Performance guardrails (each one is checked)

| Guardrail | Check |
|---|---|
| No blur in UI paint (`DropShadow`, `GlowEffect` and `ImageConvolutionKernel` are banned under `Source/UI`) | `grep` step in `ui_shots.ps1`; review |
| Flat fills; snapped hairlines; hatch from one cached `NativeImageType` tile; `SoftwareImageType` only for images uploaded to GL | review; the gallery's hatched-slicer stress tile is included in the stats budget |
| `setOpaque(true)` on GlassScreen, cells and pages; deck and grille use `setBufferedToImage(true)` | review |
| No filesystem access on the message thread from paint or poll paths (`writesUnlocked`, backup state, thumbnails) | `HALO_UI_STATS=1` counts `Ep40Backup::list()` calls on the message thread: 0 per minute when idle |
| Meters repaint their own rect, only on quantised change | `HALO_UI_STATS=1` (Debug): paints per second per top-level child |
| Waveform and thumbnail peaks built off the message thread, cached per width | no stall over 16 ms when resizing (stats) |
| One `MotionClock`, detached when idle | 0 vblank ticks over 10 s with `HALO_MOCK_TRANSPORT=stopped`, `HALO_MOCK_SEED` pad strikes off, and the mouse still |
| Stage: 0 frames outside PLAY; ≤ 150 ms to first frame on return | `HALO_STAGE_STATS=1` over 30 s in modes 2–7; the switch timing log |
| Idle UI paint budget (mock, transport stopped, PLAY) | at most 10 paints per second in total |

---

## 6. Milestones

### 6.1 Common harness

**Build and test:**
```
set HALO_BUILD_DIR=build\Redesign
scripts\build.cmd build Debug
ctest --test-dir build\Redesign -C Debug
```
These are the same build directory and commands as the 3D plan. ctest runs `HaloTests`, `HaloUiTests` and `check_write_sites.py`.

**Sandbox (R0).** `ui_shots` sets `HALO_DATA_ROOT=build\shots\sandbox\<run>`. Through `HaloPaths.h`, that redirects all of these in Debug builds:
- recordings
- exports
- "From EP-40" saves
- the library index
- settings
- backups
- the protocol trace

`ui_shots` refuses to start if the variable is missing. It never sets `HALO_DEVTEST`, `HALO_AUTOMONITOR` or `HALO_AUTORECORD`, so a plugged-in EP‑40 is never opened and nothing lands in the owner's real Music or Documents folders. Recording, writes-unlocked and sending states are shot from **gallery fixtures**, never from live runs.

**Deterministic mock (R0):**
- `HALO_MOCK_TRANSPORT=stopped|running`: today's `runMockStep` always runs a 120 BPM clock; it gains a stopped state
- `HALO_MOCK_SEED=<n>`: a deterministic pad sequence, with `0` = no pad strikes
- `HALO_UI_FREEZE=1`: pins the UI time source
- `HALO_WINDOW_SIZE=w,h`
- `HALO_UI_SCALE=1.25|1.5`: Debug `Desktop::setGlobalScaleFactor`, to emulate DPI

**Shots: `tools/dev/ui_shots.ps1`** (new in R0). For each entry in its matrix it:
1. sets the sandbox, `HALO_MOCK_MIDI=1`, the hooks, the size and the scale;
2. starts `build\Redesign\Halo_artefacts\Debug\Halo.exe` with `-PassThru` (the MOCK instance runs beside the owner's daily build);
3. waits for `HALO_UI_READY`, plus `HALO_STAGE_READY` in PLAY;
4. runs `tools\dev\screenshot.ps1 -ProcessId $p.Id -Out build\shots\ui\<milestone>\<name>.png`;
5. stops *that* process.

It never targets the daily Halo by name.

**The matrix:**
- sizes `1440,900`, `1180,720` and `1180,660`, plus `1440,900` at scales 1.25 and 1.5
- `HALO_MODE=1…7`
- PLAY with `HALO_RAIL=open|closed` and `HALO_MOCK_TRANSPORT=stopped|running`
- EDIT and CHOP with `HALO_OPEN=build\fixtures\longsong.wav` and `…|chop`, plus `HALO_CHOP_SECONDS=4`. The fixture is generated by `tools\dev\make_test_loop.py`.
- `HALO_UI_GALLERY=1`
- `HALO_THEME=graphite|field|legacy`
- `HALO_UI_POSE=card:3,shift:1,drop:1,armed:1` (Debug: freezes a popup card, the shift layer, the drag-hover state or ASSIGN ARMED)

**Diffs and probes:**
- **Masked diff:** `tools/dev/ui_probe.py` (Pillow, run with `-I`) compares shots with a mask that excludes the GL rect, the event log and the clock lane, using a per-channel tolerance of 2 and an area tolerance of 0.1%. Pixel-identical comparisons are never used, because the live stage and log vary.
- **Probes** sample known regions:
  - caution stripes present in the PLAY mock band
  - honest-slot tag pixels yellow at every size and scale
  - no crimson pixels inside *UI regions* where no blocker should exist (the GL rect and the device poster are masked, so the device's own orange cannot trip it)

**Real-device checks:** states that need hardware are listed per milestone as **owner device checks**, for example writes unlocked, assign, sending, and a backup running. The owner closes the daily Halo, runs the branch build without mock, and ticks them off.

**Gallery fixtures:** the gallery renders every primitive in every state from fixture data, under a permanent yellow band `FIXTURE · gallery data, not device state`. Fixture states can never be mistaken for real ones, in the spirit of H1.

**Every milestone's acceptance includes:**
- ctest green
- H1–H7 re-checked
- masked shot diffs against the previous milestone reviewed

### R0: Harness, honest state, sandbox, and the missing assign confirm (S). One visual change: ASSIGN asks first.

**Files:**
- New tools: `tools/dev/ui_shots.ps1`, `tools/dev/ui_probe.py`, `tools/dev/check_write_sites.py`
- `Source/App/Main.cpp`: `HALO_UI_READY` log, `HALO_WINDOW_SIZE` (if 3D M0 has not landed it), `HALO_UI_SCALE`, `HALO_DATA_ROOT`, and ui_shots' refusal when `HALO_DEVTEST` is set
- `Source/App/AppModel.{h,cpp}`: `HALO_MOCK_TRANSPORT`, `HALO_MOCK_SEED`; `writesUnlocked()` reads the cache
- `Source/App/HaloPaths.h` (new). Used by `Source/Audio/SessionRecorder.cpp`, `Source/Audio/SampleIO.cpp`, `Source/Device/Ep40Backup.cpp`, `Source/Device/ProtocolTrace.h`, `Source/App/Library.cpp`, `Source/App/Settings.h` and `Source/UI/LoadPanel.cpp`
- `Source/Device/Ep40Backup.{h,cpp}`: `latestComplete(serial)`, cache, `forceRescan()`
- `Source/App/MainComponent.cpp`: `HALO_RAIL=closed`
- `Source/UI/LoadPanel.cpp`: the pad click in assign mode goes through a temporary `AlertWindow` confirm with the §3.13 assign copy plus the backup line
- `Source/Core/HonestState.{h,cpp}`, `Tests/HonestStateTests.cpp`
- `CMakeLists.txt`
- `docs/redesign/plan.md`

**The owner sees:**
- The app unchanged, except that **ASSIGN now asks first**, naming the pad's current sound and the backup.
- Baseline shots in `build/shots/ui/r0/`.

**Accept:**
- Baseline shots at all three sizes for modes 1–6, written only under the sandbox. A post-run check finds no new files in `Music\Halo` or `Documents\Halo`.
- `HonestStateTests`:
  - mock ⇒ MOCK flag in every mode
  - feed preview or disconnected ⇒ PREVIEW
  - HonestState passes `writesUnlocked` and `latestBackup` through unchanged
  - `pickNotice` returns blocker > info > plain for all of today's EDIT and CHOP note cases, copied verbatim as fixtures
- `check_write_sites.py` passes. In R0 the allow-list is today's three dialog call sites plus the new assign confirm; R3 shrinks it to `WriteGate` alone.
- With mock transport stopped, the mock sends no clock and no pads.
- **Owner device check:** assign a test sound to a pad, and confirm that the dialog appears and names the old sound and the backup.

### R1: Tokens, fonts, theme plumbing, tooltips (M). No intended visual change, except that tooltips now appear.

**Files:**
- `Source/Core/UiTokens.h`, `Tests/UiTokensTests.cpp`
- `Source/UI/Design/Theme.{h,cpp}`, `Fonts.{h,cpp}`
- `Source/UI/HaloColours.h` (shim)
- the 13 binding files listed in §5.1
- `Source/App/MainComponent.cpp`: `TooltipWindow`; `HALO_THEME`
- `assets/fonts/*` + `OFL-*.txt`
- `tools/fonts/build_fonts.py` (+ `roles.json`), which generates `Source/Core/UiFontMetrics.h`
- new test target `HaloUiTests` with `Tests/Ui/FontsTests.cpp`
- `CMakeLists.txt`
- `.gitignore`: `assets/fonts/local/`

**The owner sees:** the same app, with the tooltips for AUTO-TRIM, LENGTH and ASSIGN-locked now showing.

**Accept:**
- r1 shots match r0 under the **masked diff** with `HALO_THEME=legacy` (the default).
- `UiTokensTests` passes the §2.1 contrast pairs, the honest-hue ΔE rules, and the single info↔enc4 exemption.
- `FontsTests`: cap heights per role are within ±0.5 px of target; tracking conversion is correct.
- `build_fonts.py`: cmap coverage passes and no RFN is present.
- The Debug log reports `fonts: 9 faces registered`.
- The binary grows by less than 700 KB.

### R2a: Primitives I and the gallery shell (M)

**Files:**
- `Source/UI/Design/{Paint.h, Numerals.*, Pictograms.*, Gallery.*}`
- `Primitives/{Tag and Lamp in Paint, Notice, ActionKey, StepKeyRow, DotTimeline, NameCells}`
- `Source/App/Main.cpp` (`HALO_UI_GALLERY`)
- `CMakeLists.txt`

**The owner sees:** `HALO_UI_GALLERY=1` opens a black gallery page with:
- the palette and its contrast values
- the type scale
- Halo Numerals `0123456789 I2 28.60 −12`
- all pictograms
- tags, lamps and notices (blocker / info / plain, with the longest verbatim copy)
- action keys in every variant
- step keys, dot timelines and name cells at 100/125/150%

**Accept:**
- The gallery is shot at all sizes, both skins and three scales.
- Nothing is under 10.5 px em except deck micro-print.
- NameCells are crisp at 125% and 150%: the probe checks for no intermediate grey in the pixel cells.

### R2b: Primitives II, encoder system core, WaveCache (M); owner gate U1

**Files:**
- `Source/Core/EncoderMath.h`, `Tests/EncoderMathTests.cpp`
- `Design/{Motion.*, EncoderBus.*}`
- `Primitives/{DeckKey, DeckEncoder, ParamCell, ReadoutCell, SoftKeyRow, PopupCard, PadGrid, LevelArc, LedLadder, TapeLane, Waveform/WaveCache}`
- WaveCache threading test in `HaloUiTests`
- gallery pages

**The owner sees:**
- wheel-turnable encoders
- cells that are live, dead and touched, with the tag overflow rule
- the popup card in all four colours
- pads in every state, including armed
- meters animating from fixture data, with tap labels
- the shift layer (`HALO_UI_POSE=shift:1`)

**Accept:**
- `EncoderMathTests`: wheel, Alt fine, Ctrl coarse, discrete steps, drag, and the 250 ms gesture-end debounce.
- The gallery's bare-Alt check passes.
- WaveCache builds off the message thread and cancels cleanly on resize.
- **Gate U1:** the owner approves the palette, numerals and type, or picks adjustments, before any screen is rebuilt. The review includes a greyscale render of the gallery and the 150% shots.

### R3: WriteGate, ConfirmCard, and honest notice colours on today's panels (S)

**Files:**
- `Design/WriteGate.{h,cpp}`, `Primitives/ConfirmCard.{h,cpp}`, `Tests/Ui/WriteGateTests.cpp`
- `Source/UI/HaloLookAndFeel.{h,cpp}`: `createAlertWindow`, `drawAlertBox`, alert fonts, `setDefaultLookAndFeel`
- call sites moved to `WriteGate::request`, with the same callbacks and verbatim copy plus the backup line:
  - `EditPanel.cpp` (send, about :89)
  - `ChopSurface.cpp` (send to pads, about :110)
  - `LoadPanel.cpp` (delete, about :40; assign, about :228)
- `LoadPanel.cpp`, `EditPanel.cpp`, `ChopSurface.cpp`: status errors and blockers use `danger`; info notes use `info`
- `check_write_sites.py`: the allow-list becomes `WriteGate` only

**The owner sees:** the four write dialogs as field "POWER OFF?"-style cards with a crimson header and a backup line. LOAD failures are now red.

**Accept:**
- The gallery shows all four cards, including the dead-confirm state.
- Esc cancels; a fresh Enter cancels; Tab then Enter confirms.
- `WriteGateTests`: each kind opens a card; locking writes, disconnecting or starting a send while the card is open makes the confirm dead; confirming after the gate closes performs no write.
- **Owner device check:** send one sample, send slices, assign a pad, delete a test sound. Each opens the card naming the backup. Unplugging with a card open turns its confirm dead.

### R4: The shell (L, landed as R4a and R4b)

**R4a, chrome**

- **Files:**
  - `Primitives/{GlassScreen, StatusStrip, Deck, Grille, VuWindow, StageFrame}`
  - `Source/Core/UiLayout.{h,cpp}`, `Tests/UiLayoutTests.cpp`
  - `Source/App/AppModel.{h,cpp}` (`AppMode::chop`, `monitorGainDb()`)
  - `Source/App/EditSession.*` (chopOpen → mode)
  - `Source/App/Main.cpp` (minimum 1180×660, initial size clamped to `userArea`, dark title bar)
  - `Source/UI/HaloColours.h` (`HaloMetrics` minimum)
  - `Source/App/MainComponent.{h,cpp}`: new layout, Ctrl+7, `HALO_MODE=7`, stage only in PLAY plus the honest band and return poster, and the legacy-panel docking with the `not rebuilt yet` plate (§5.1)
  - delete `Source/UI/StatusBar.*` and `Source/UI/ModeBar.*`
  - `Tests/Ui/UiStateTests.cpp`
  - `CMakeLists.txt`
  - `Theme::stageLook()` wired once 3D M1 has merged. Until then the stage stays paper inside the black frame.
- **The owner sees:**
  - the black glass and the graphite deck with 7 pictogram keys and LEDs
  - the new status strip with the honest slot and padlock
  - CHOP on its own key
  - today's panels docked at the right of each page in the legacy-dark mapping, beside an honest `not rebuilt yet` plate
  - the stage only in PLAY, framed by the yellow caution band in mock

**R4b, the encoder system**

- **Files:**
  - `Primitives/{ParamStrip, Lane}` wired to `EncoderBus` and the deck
  - PLAY's four bindings (level, grab, orbit, echo mix)
  - `Main.cpp`: bare-Alt `WM_SYSKEYUP` handling
- Other modes show dead cells until their page lands. Dead cells are honest.
- **The owner sees:** four colour knobs that turn with the wheel, cells aligned above them, and the popup card in the lane.

**Accept (R4):**
- Shots in modes 1–7 at all three sizes and at 125% and 150%.
- `UiLayoutTests`:
  - encoder, cell and pill centres agree to ±0.5 px for widths 1180–2560
  - the honest slot, rec and writes slots never collapse
  - StatusStrip fits at 1180 with everything on
  - the PLAY rail sum fits at compact and ultra-compact
- Probe: the caution band is present in PLAY mock at every size and scale, with `HALO_STAGE_TIER=still` too.
- `HALO_STAGE_STATS`: 0 stage frames in modes 2–7; **mode switch to first stage frame ≤ 150 ms** on the owner's GPU (owner device check). If the budget is missed, the return poster still covers the gap, and the 3D plan's §4.4 item 2 becomes blocking for U2.
- `UiStateTests`:
  - switching CHOP → RACK shows RACK (fixes the lingering CHOP surface)
  - the strip's mon value equals `monitorGainDb()`
  - Ctrl+1…6 behave exactly as before
  - the modifier table has no duplicates

### R5: PLAY page (M); owner gate U2

**Files:**
- `Source/UI/Pages/PlayPage.{h,cpp}`
- Primitives used: LevelArc, LedLadder, TapeLane, ReadoutCell, ActionKey (`out`)
- delete `Source/UI/PlayRail.*` (including the dead `paintPlate`) and `Source/UI/MonitorPanel.*`
- `Source/App/MainComponent.cpp`

**The owner sees** the full PLAY of §3.5:
- last-pad cells
- level arcs labelled `out` with a blue LEVEL numeral
- the `out` selector
- last take and last grab lines with `show`
- the GRAB tape lane with its ochre bracket
- the rack chain (one line at compact)
- events
- the clock lane with the beat ruler
- the collapsed spine with meters

**Accept:**
- Shots: open and closed rail, at all sizes, with mock transport stopped and running.
- After `HALO_MONITOR_DB=-12`, the rail and the status strip show the same level (fixes the −60 / −12 mismatch).
- The GRAB title is identical in the transport-stopped and transport-running shots.
- The latency figure is rounded to whole ms.
- Choosing an output in the `out` menu changes the route, exactly as the old ComboBox did.
- The `limit` lamp lights from pre-limiter `rawPeak` (gallery fixture plus `MonitorDspTests`).
- **Gate U2:** the PLAY look and stage-on-black are approved, together with the 3D plan's G1 if it is ready.

### R6: CHOP page (L)

**Files:**
- `Source/UI/Pages/ChopPage.{h,cpp}`
- `Primitives/Waveform/Slicer.{h,cpp}` (on R2b's WaveCache)
- `Source/App/EditSession.{h,cpp}` (send project exposed to CHOP)
- delete `Source/UI/ChopSurface.*`

**The owner sees** `hybrid-chop.svg` made real, with the strip on the deck:
- the LENGTH ruler with the red 20 s tick
- hatched tails with orange edges
- the visible `clear cuts` key
- the project picked in CHOP
- channels shown
- the slice bracket

**Accept:**
- Shots with `HALO_OPEN=…|chop` and `HALO_CHOP_SECONDS=4`, at all sizes, plus `HALO_UI_POSE=card:3`.
- GROUP keys are equal width.
- EVEN shows SENSITIVITY dead with its reason.
- SENSITIVITY re-runs detection once per gesture: the stats count one detection per wheel burst.
- 1–9/0, Delete, double-click, drag and right-click all behave as today; Ctrl+wheel zooms.
- `UiLayoutTests`: the 16-slice blocker fits at 1180×720 and 1180×660, with the canvas at no less than 120 px.
- **Owner device check:** send slices to pads (card plus progress).

### R7: EDIT page (M); owner gate U3

**Files:**
- `Source/UI/Pages/EditPage.{h,cpp}`
- `Primitives/Waveform/SampleBand.*`
- `Primitives/Progress.*`
- delete `Source/UI/EditPanel.*` and `Source/UI/WaveformView.*`

**The owner sees:**
- the paper sampler band with four coloured markers
- the trim page, and the shape page as the shift layer
- size and memory without truncation
- send progress with reels

**Accept:**
- Shots at all sizes: empty, loaded, shift page, and the auto-trim note (a 120 s fixture).
- The status line never shows CHOP's status.
- **Gate U3:** EDIT and CHOP are approved, and the owner picks the slicer or the band for CHOP (U13).

R6 and R7 may land in either order; both depend only on R2b's WaveCache.

### R8: LOAD page (M)

**Files:**
- `Source/UI/Pages/LoadPage.{h,cpp}`
- `Primitives/BrowserList.*`
- delete `Source/UI/LoadPanel.*`

**The owner sees:**
- the pads plus the three-column browser
- the storage dots
- armed pads with dashed outlines, and the persistent ASSIGN ARMED header tag
- the padlock keys
- the full-page unavailable cards

**Accept:**
- The mock shot shows the MOCK card. The gallery shows the other three unavailable cards and the armed pose (`HALO_UI_POSE=armed:1`).
- PROJECT and GROUP share one row with equal widths within each group, and DELETE never clips.
- Turning an encoder while armed leaves the armed instruction visible in the header.
- **Owner device check:** sync progress, assign armed then assign (card), the delete card, the storage bar.

### R9: CAPTURE page (M)

**Files:**
- `Source/UI/Pages/CapturePage.{h,cpp}`
- `Source/App/Library.{h,cpp}`: lazy peak thumbnails on a background thread; filter and sort
- `Source/Audio/MonitorEngine.{h,cpp}`: the `in` metering tap
- `Tests/MonitorDspTests.cpp`: the tap's levels match the raw recorder input, and output samples are bit-identical with taps on and off
- delete `Source/UI/LibraryPanel.*`

**The owner sees:**
- the tape machine with `in` meters (and `print` meters when PRINT FX is on) and the REC lamp
- last take with `show`
- the searchable library with thumbnails
- a visible empty state
- the drop frame

**Accept:**
- Shots: an empty library (a fresh sandbox), a populated library (sandbox fixture files), and `drop:1`.
- The recording state comes from a gallery fixture.
- No record-level control exists anywhere.
- The LEVEL caption states its effect on takes.
- **Owner device check:** record a take and watch the `in` meters respond while LEVEL is moved: the `in` meters must not change.

### R10: RACK page (M)

**Files:**
- `Source/UI/Pages/RackPage.{h,cpp}`
- `Source/Audio/MonitorEngine.{h,cpp}`: the `preRack` metering tap, with a test in `MonitorDspTests`
- delete `Source/UI/RackPanel.*`

**The owner sees:**
- the full-width sound-path chain of paper tiles with black values and encoder bars
- `pre-rack` and `out` meters
- the strip bound to the focused module
- dead cells for unused slots

**Accept:**
- Shots: each module focused, bypassed (hatched), and the SYNC divisions.
- `DubRackTests` still pass, and the audio path is unchanged (bit-identical output test).
- `UiTokensTests` includes black-on-paper and the bar-beside-value rule.

### R11: BACKUPS page (M)

**Files:**
- `Source/UI/Pages/BackupsPage.{h,cpp}`
- delete `Source/UI/BackupsPanel.*`

**The owner sees:** the hero lock plate, the cassette shelf with hatched INCOMPLETE tags, the dot-matrix backup progress, and the empty shelf.

**Accept:**
- Shots: mock (no device), plus gallery fixtures for locked, unlocked, running, error and incomplete.
- The BACKUPS LED is hollow when locked.
- **Owner device check:** run a real backup, CANCEL partway (the INCOMPLETE tag is hatched, not crimson), then complete one. Confirm that writes unlock everywhere at once, in the strip, keys and LED, within one poll, driven by `finish()` invalidating the cache.

### R12: Character pass (M)

**Files:**
- `Design/Motion.*`
- machine drawings in `RackPage`, `CapturePage`, `BackupsPage` and `Progress`
- the ✂ snap in `Slicer`
- the reduced-motion read in `Main.cpp`

**The owner sees:** reels with inertia, echo rings, a jiggling spring, a pumping cone, snapping scissors and pad flashes, all driven by the signal.

**Accept:**
- The idle budget is met (§5.3), with 0 vblank ticks when silent.
- With reduced motion on, the machines stand still and the meters still move.

### R13: Field skin, cleanup, final audit (S); owner gate U4

**Files:**
- the `Theme` `field()` preset (brushed deck, buffered)
- delete `Source/UI/HaloColours.h`, `legacyPaper` and `legacyDark`
- `docs/redesign/plan.md` (status)
- refresh the `build\` reference shots

**The owner sees:** `HALO_THEME=field` gives an aluminium deck with the same black screen.

**Accept:**
- The full shot set in both skins, at all sizes and scales.
- Every §5.3 guardrail passes.
- `grep graphPaper Source` returns nothing.
- With `HALO_LOCAL_FONTS=ON`, the start-up self-check either passes or refuses the override with a logged reason.
- **Gate U4:** "trophy-worthy" sign-off.

**Dependencies:**
- R0 → R1 → R2a → R2b → R3 → R4a → R4b → R5.
- R6–R11 may land in any order after R4b. R6 and R7 both use R2b's WaveCache, and R9 and R10 each add their own metering tap.
- R3 may land before R2b if the owner wants the cards sooner, since it needs only R2a.
- 3D plan M1 (`StageLook`) is needed for the black stage in R4a/R5. Without it, the stage shows paper inside the frame and nothing else is blocked.
- The 3D plan's §4.4 item 2 (fast re-show) is needed for U2 only if R4a misses the 150 ms budget.

---

## 7. Risks, mitigations and scope

### 7.1 Risks

| Risk | Mitigation |
|---|---|
| **Airspace:** chrome or a card drawn over the GL child silently disappears, and MOCK or PREVIEW vanish | Honest marks live in the StatusStrip and the StageFrame band, which wraps GL, the return poster and the fallback (H6 assertion). There is no GL HUD (U10). The ConfirmCard hides the stage. The PopupCard docks in the lane. There is no chrome fade over GL |
| **Context recreation on return to PLAY** | A measured 150 ms budget, the return poster, and the 3D plan request for a cached CPU mesh parse (§4.3, §4.4) |
| **Writes without a confirm, or a stale gate** | `WriteGate` is the only write path (lint-checked in CTest); ASSIGN is fixed in R0; a live re-check while the card is open and on confirm; `WriteGateTests` |
| **Filesystem stalls from backup polling** | `latestComplete()` cache with explicit invalidation and a background rescan; a stats counter asserts no message-thread scans |
| **Warm hues crowd each other** (danger ≈ 345°, enc4 8°, info 18°, mock 42°) | Measured ΔE rules with one documented exemption (info↔enc4, separated by form); form carries the meaning (filled with a triangle, outlined with an "i", hazard stripe, dashed); greyscale review at U1 |
| **Small tracked text blurs under Direct2D greyscale anti-aliasing** | 10.5 px *em* floor via `withPointHeight`; medium weight or heavier below 12 px; embedded hinted statics; `FontsTests`; shots at 125% and 150% from R2a onward |
| **Wide display faces overflow reserved slots** | Lexend Exa for labels and tags; strip short forms; text-fit tests from generated metrics at every size; a runtime self-check for the TE override |
| **Modifier conflicts and the Windows Alt key** | One grammar (U19) tested as a table; bare-Alt suppression verified in the gallery; right-button drag as the fallback fine path |
| **Encoder metaphor confuses mouse users** | The wheel works directly on cells and markers; drag and click work; every press is printed on a pill; F1–F4 for the keyboard; nothing critical lives only on the shift layer |
| **Page body shrinks** (584 / 462 / 422 px) compared with today's full-height rail | Every page's compact layout is specified, with yielding canvases and minimums; `UiLayoutTests` lays out real copy at 1180×720 and 1180×660 |
| **Meters that lie about their tap point** | Every meter prints its tap; new taps (`in`, `preRack`) are metering-only, with bit-identical output tests; the level arc is calibrated in dBFS with a limit zone driven by the pre-limiter peak |
| **Shot runs touching the owner's data or device** | `HALO_DATA_ROOT` sandbox is mandatory; no auto-monitor, auto-record or devtest hooks in shots; device states come from gallery fixtures |
| **Flaky acceptance from a live mock** | Mock transport and seed hooks, `HALO_UI_FREEZE`, masked diffs |
| **Shortcut regressions** | Ctrl+1…6 untouched (U11); keymap table test in `UiStateTests` |
| **Repaint storms from more painted chrome** | Opaque components, buffered deck, quantised meters, one MotionClock, native hatch tile, `HALO_UI_STATS` budget |
| **Font registration lost, or faces missing** | `HaloFonts` owned for the app's lifetime; start-up log and assert; fallback to Segoe UI and Cascadia Mono; cmap coverage enforced at build time |
| **Migration leaves mixed old and new looks** | Legacy panels docked at their rail width beside an honest plate; the legacy-dark mapping (orange → enc4); each page milestone deletes its panel; whole-window shot reviews |
| **Coupling with the 3D plan stalls the stage look** | Only `StageLook` and the §4.4 requests couple the plans; UI milestones proceed on a paper stage inside a black frame |
| **Bigger build surface** (about 30 primitives, 7 pages) | Primitives split into R2a and R2b with gallery review; each milestone stays under about 1.5 k lines; machines deferred to R12 |

### 7.2 Explicitly out of scope

- **Anything inside the 3D stage:** geometry, materials, lighting, LCD, the ring pool implementation, camera feel, tiers. These belong to `docs/redesign/riddim-3d-plan.md`. This plan only makes the §4.4 requests.
- **Behaviour below the UI:** the device protocol, the write-gating *logic* (WriteGate only centralises the confirm), the backup format, the DSP and the rack algorithms. Behaviour is unchanged and only presentation changes, except for the items named here:
  - the ASSIGN confirm
  - the backup cache
  - metering-only taps
  - Debug-only sandbox, mock and DPI hooks
- **New features beyond presentation**, except these small ones:
  - CHOP as a mode
  - project choice in CHOP
  - CAPTURE search, sort and thumbnails
  - the single level getter
  - the `out` key replacing the ComboBox
- Theming the native Windows `FileChooser` and title-bar buttons, beyond the dark title bar.
- Committing TE's own typefaces (DD‑007a), and any page that imitates TE's branding outside this personal build.
- Localisation, accessibility beyond keyboard focus and contrast, light mode for the screen (the screen is always black), and a macOS build.
- Illustrated extras the judges suggested that carry no Halo job, for example a cow or boxer machine with no matching effect.

---

## 8. Review resolutions

| Critic issue | Resolved in |
|---|---|
| H3 broken (ASSIGN writes with no confirm) | U20, H3, §3.13 assign copy, R0 (temporary confirm), R3 (WriteGate), `check_write_sites.py` |
| Contrast/ΔE rules fail the palette | danger → `#DC1E5A`; measured tables in §2.1; info↔enc4 exemption; enc1 fill `#2C4F7A` |
| Illegible text placements | §2.1 encoder fill/text columns; black values on paper tiles (§3.11); PopupCard header (§2.5); ReadoutCell text; log floor `ramp5`; disabled keys; `print.cap`, with `print.dim` non-text only |
| Type sizes vs JUCE `withHeight` | §2.2: `withPointHeight`, em tracking conversion, `FontsTests` |
| Meters claim signals that do not exist | U15; tap labels; `in`/`preRack` taps in R9/R10; LevelArc in dBFS with a pre-limiter limit zone; CAPTURE LEVEL caption |
| Stage hide cost; chrome fade | U12, §4.3 budget and return poster, §4.4 item 2, R4a accept |
| `writesUnlocked()` hits the filesystem | §5.1 `latestComplete()` cache; R0; §5.3 stats counter |
| Confirm callbacks do not re-check the gate | §3.13 live gate; `WriteGateTests` |
| Non-deterministic mock | §6.1 mock transport, seed, freeze, masked diffs, probe masks |
| Modifier grammar contradicts itself | U19, §2.5 input contract, §3.15 table test |
| EncoderBinding underspecified | §2.5 struct (default, steps, shift setters, gestures, owned regions) |
| Minimum-size overflow; row-set contradiction | §2.3 computed pad rows; §3.1 yielding canvases and text-fit tests; LOAD, CHOP and PLAY compact fixes |
| Honest slot and cells overflow with Giga | Lexend Exa; strip short forms; measured slot width; ParamCell tag overflow rule; StatusStrip test |
| Glyph fallback; pixel-font crispness; RFN | §2.2 symbols as Pictograms, cmap and RFN checks, per-scale pixel size |
| Mock runs write to real data | §6.1 `HALO_DATA_ROOT` sandbox, `HaloPaths.h`, fixture-only recording states |
| Legacy panels' placement during migration | §5.1 docking, honest plate, orange → enc4 |
| R2 too large; WaveCache ordering | R2a/R2b split; WaveCache in R2b |
| PLAY items dropped | `out` key, last take line, dropped-samples blocker (§3.5, §3.10) |
| Red/yellow dilution | idle REC dim; hatched INCOMPLETE; GRAB tags outlined |
| Discoverability lost | visible `clear cuts` key; armed instruction in the header; lane rule; pictogram-only compact caps |
| Hatch image type | §2.5 `NativeImageType` tile; stats budget |
| Size and DPI fit | 1180×660 ultra-compact; `userArea` clamp; 125%/150% shots |
| Ring colour equals info | `ring.ui` in ink; orange only in the GL pool; RACK LED white |
| Bookkeeping gaps | 13 files listed; hooks assigned to R0/R1/R2b/R4a; one READY line per side; StageFrame wraps the fallback |
| TE override untested; DD‑006e | §2.2 self-check; U14 keeps DD‑006e for the silkscreen |