"""The Still tier's plan view (plan M11), recolourable by the device colourway.

TE's front drawing is rasterised top-down and every pixel is labelled with a
*class* instead of a colour: a material ("role:palette", the same names the 3D
model's materials carry, e.g. "pad:key white", "body:cream") for a part's own
fill, or an ink ("ink:green" ... for legends on caps, plates and inserts,
"face:green" / "face:orange" for the deck and label silkscreen). The stage
colours the classes at runtime from the active DeviceColourway (the same
lookups as the 3D model), so the flat fallback follows the colourway too.

    python -I tools/model/artwork/plan_poster.py
        -> assets/stage/ep40_plan_classes.png (8-bit grey, class ids, 12 px/mm)
        -> Source/Stage/PlanClasses.h       (class names and TE's sRGB colours)
"""

import sys
from pathlib import Path

import numpy as np
from PIL import Image

HERE = Path(__file__).resolve().parent
sys.path.insert(0, str(HERE.parent))
sys.path.insert(0, str(HERE.parent / "ep40"))
from artwork import inks, raster, svg_import  # noqa: E402
import hmesh_io  # noqa: E402
import spec  # noqa: E402

ROOT = HERE.parents[2]
PX_PER_MM = 12          # 2112 x 2880; the stage averages 3 x 3 down after colouring
LABEL_PLATE = (0.0, 8.57, 119.55, 56.50)      # build_ep40.LABEL_PLATE
FADER_SLOT = (14.2, 168.0, 17.3, 208.5)       # build_ep40.FADER_SLOT

# TE's colours per palette name (sRGB), as the model compiler's palette.
PALETTE = {"cream": "DCD2BF", "label": "ECEBE6", "key white": "E9E9E5", "key beige": "DDD3C2", "green": "0A4A36",
           "orange": "FA5A16", "gap": "1C1B19", "stem": "EDEBE3"}
INK_CLASSES = [("ink:green", "00503A"), ("ink:orange", "FF681D"), ("ink:white", "F4F2EC"), ("ink:charcoal", "2B2B28"),
               ("face:green", "00503A"), ("face:orange", "FF681D")]
INK_OF = {inks.GREEN: 0, inks.ORANGE: 1, inks.WHITE: 2, inks.CREAM: 2, inks.CHARCOAL: 3}
DRAWN_PALETTE = {inks.WHITE: "key white", inks.CREAM: "key beige", inks.GREEN: "green", inks.ORANGE: "orange",
                 inks.CHARCOAL: "green"}


def main():
    fills, _ = svg_import.load()
    lab = raster.label_image(fills, (0.0, 0.0, spec.WIDTH.mm, spec.DEPTH.mm), PX_PER_MM, inks.classify)
    h, w = lab.shape
    ys = (np.arange(h) + 0.5) / PX_PER_MM
    xs = (np.arange(w) + 0.5) / PX_PER_MM
    yy, xx = np.meshgrid(ys, xs, indexing="ij")

    mesh = hmesh_io.read_hmesh((ROOT / "assets" / "stage" / "EP40.hmesh").read_bytes())
    material_of = {p["name"]: mesh["materials"][p["material"]]["name"] for p in mesh["parts"]}

    classes = [name for name, _ in INK_CLASSES]
    colours = [hexc for _, hexc in INK_CLASSES]

    def class_id(name):
        if name not in classes:
            role, palette = name.split(":")
            classes.append(name)
            colours.append(PALETTE[palette])
        return classes.index(name)

    out = np.full((h, w), class_id("body:cream"), np.uint8)

    def rect(x0, y0, x1, y1):
        return (xx >= x0) & (xx < x1) & (yy >= y0) & (yy < y1)

    def disc(cx, cy, r):
        return (xx - cx) ** 2 + (yy - cy) ** 2 <= r * r

    def paint_part(r, inset, fill_class, margin=0.5):
        """The part's rectangle takes its fill; inside the include rectangle (past
        the drawn key border, as the 3D decal tiles), inks other than the
        dominant drawn one are its legends."""
        # A dark aperture ring round the part (the 3D model's wells), which
        # also covers the drawn key border: that belongs to the key, not the
        # silkscreen (as build_ep40.face_exclusions).
        out[rect(r[0] - margin, r[1] - margin, r[2] + margin, r[3] + margin)] = class_id("well:gap")
        region = rect(*r)
        include = rect(r[0] + inset[0], r[1] + inset[1], r[2] - inset[0], r[3] - inset[1])
        counts = np.bincount(lab[include], minlength=6)
        counts[inks.NONE] = 0
        base = int(counts.argmax())
        out[region] = class_id(fill_class)
        for ink, channel in INK_OF.items():
            if ink != base:
                out[include & (lab == ink)] = channel

    # The deck and the label plate: cream (or the plate's white) with the face silkscreen.
    face = np.isin(lab, [inks.GREEN, inks.ORANGE])
    out[face & (lab == inks.GREEN)] = 4
    out[face & (lab == inks.ORANGE)] = 5
    label = rect(*LABEL_PLATE)
    out[label] = class_id(material_of["brand_panel"])
    out[label & (lab == inks.GREEN)] = 4
    out[label & (lab == inks.ORANGE)] = 5

    # Keys, function cells, small keys: their own fill and legend inks.
    cell = spec.CELL.mm
    for name, col, row in spec.FULL_KEYS:
        x, y = spec.COLUMN_LEFT[col], spec.ROW_TOP[row]
        paint_part((x, y, x + cell, y + cell), (1.0, 1.0), material_of[name])
    cw, ch = spec.SHORT_CAP
    pw, ph = spec.PLATE
    for cap, plate, col, row in spec.FUNCTION_CELLS:
        x, y = spec.COLUMN_LEFT[col], spec.ROW_TOP[row]
        out[rect(x - 0.3, y - 0.3, x + cell + 0.3, y + cell + 0.3)] = class_id("body:cream")  # the cell's drawn outline
        paint_part((x + (cell - cw) / 2, y + 0.5, x + (cell + cw) / 2, y + 0.5 + ch), (1.0, 0.9), material_of[cap])
        paint_part((x + (cell - pw) / 2, y + cell - ph, x + (cell + pw) / 2, y + cell), (1.0, 0.7), material_of[plate])
    sx0, sx1 = spec.SMALL_KEY_X
    for name, (y0, y1) in spec.SMALL_KEY_Y.items():
        paint_part((sx0, y0, sx1, y1), (1.0, 1.0), material_of[name], margin=0.8)
    for name, r in spec.PORT_TABS.items():
        paint_part(r, (0.35, 0.35), material_of[name])

    # Knobs: the cap's own colour as drawn.
    for name, (kx, ky) in spec.KNOBS.items():
        region = disc(kx, ky, spec.KNOB_DISC[0] / 2)
        counts = np.bincount(lab[region], minlength=6)
        counts[inks.NONE] = 0
        out[region] = class_id("knob:" + DRAWN_PALETTE[int(counts.argmax())])

    # Speaker: slats, the cone behind them, the backing.
    spk = rect(*spec.SPEAKER_RECESS)
    out[spk] = class_id("spkr:key beige")
    out[spk & (lab == inks.WHITE)] = class_id("spkr:label")
    out[spk & (lab == inks.GREEN)] = class_id("spkr:gap")
    mx, my, mr = spec.MIC
    out[disc(mx, my, mr)] = class_id("spkr:gap")

    # The screen window (the live LCD is drawn over it), the fader, the slider.
    out[rect(*spec.WINDOW)] = class_id("screen:green")
    out[rect(*FADER_SLOT) & (lab != inks.NONE) & (lab != inks.CREAM)] = class_id("fader:gap")
    fx, fy, fd = spec.FADER_CAP
    out[disc(fx, fy, fd / 2)] = class_id("fader:stem")

    assert len(classes) < 64
    img = ROOT / "assets" / "stage" / "ep40_plan_classes.png"
    Image.fromarray(out).save(img, optimize=True)

    header = ["#pragma once", "",
              "// Generated by tools/model/artwork/plan_poster.py. Do not edit: rerun the tool.",
              "//",
              "// The classes of assets/stage/ep40_plan_classes.png (the Still tier's plan",
              "// view): pixel value = index here. \"role:palette\" classes are coloured like",
              "// the 3D model's materials (DeviceColourway::lookup); \"ink:\" classes take the",
              "// colourway's key inks (green, orange, white, charcoal), \"face:\" its face inks.",
              "", "#include <array>", "", "namespace halo::plan", "{",
              "    struct Class { const char* name; unsigned int teSrgb; };",
              f"    inline constexpr std::array<Class, {len(classes)}> classes {{{{"]
    for name, hexc in zip(classes, colours):
        header.append(f"        Class {{ \"{name}\", 0x{hexc}u }},")
    header += ["    }};", f"    inline constexpr int pxPerMm = {PX_PER_MM};", "}", ""]
    (ROOT / "Source" / "Stage" / "PlanClasses.h").write_bytes("\n".join(header).encode())
    print(f"wrote {img.name}: {w} x {h}, {len(classes)} classes; PlanClasses.h")


if __name__ == "__main__":
    main()
