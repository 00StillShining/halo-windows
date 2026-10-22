# TE EP-40 Riddim reference

Reference material for the Windows 3D stage (`docs/redesign/riddim-3d-plan.md`, section 2).
The owner has said copyright is not a concern for this personal project; that is recorded
as DD-006g. Everything here comes from teenage.engineering's own EP-40 pages and guide.

## Committed

TE's to-scale vector drawings, from the EP-40 guide (teenage.engineering/guides/ep-40).
They are served from `https://assets.teenage.engineering/_img/<id>…svg`; the exact URL
suffix was not recorded when they were fetched, so check it before re-fetching.

| File | View | Scale and use |
|---|---|---|
| `69033ff84b343523886b5edb.svg` | Front | **0.6091 mm per unit**. Outline 289.0 × 394.0 units (394/289 = 240/176). The geometry and artwork source of truth. |
| `69034044aa479d2cca611e2a.svg` | Back | Feet and bosses |
| `6903405a189dd72cc91154ef.svg` | Left side | **0.600 mm per unit** |
| `6903405ad2b3ad2c2f5de575.svg` | Right side | **0.600 mm per unit** |
| `6903449b7bfa382ccab2ae95.svg` | Top edge ("TOP SIDE") | Every Z height on the port face |
| `6903449b2343052c2f14b06f.svg` | Top edge with cables | Port positions |
| `69021d87c95e942b7d172721.svg` | Screen layout | `mm = 12.2 + (svgX − 31.4) × 0.184` |
| `6915acabcca196063b8ddaeb.svg` | Screen icon legend | Icon names and meanings |

Crops of TE's top-down product render that the tests and review sheets read. They are
**human review material only**: a lit marketing image with an unknown lens is never a
measurement source (plan section 2).

| File | Shows |
|---|---|
| `c_keys.png` | Function keys, numeral pads, knobs X/Y, deck silkscreen |
| `c_knobs.png` | Knob towers |
| `c_screen.png` | Display window, icons and ghost digits |
| `c_top.png` | Label plate and speaker |

## Not committed (`local/`, git-ignored)

The 2048 px product photos (`assets.teenage.engineering/_img/<id>_2048.webp` on the EP-40
product page, teenage.engineering/products/ep-40) and the remaining crops (`c_angle.png`,
`c_knobs2.png`, `c_photo_screen.png`, `s_photo.png`, `s_render*.png`). Fetch them again from
those pages when needed; they are only used on review sheets.
