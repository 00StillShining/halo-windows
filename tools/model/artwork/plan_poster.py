"""The Still tier's plan view (plan M11): TE's front drawing rendered top-down
as an opaque colour image, assets/stage/ep40_plan.png (PNG is allowed for
opaque colour images, D9). The Still tier paints it when no GL tier can run,
with the live LCD image and the pad cues drawn over it.

    python -I tools/model/artwork/plan_poster.py [--px-per-mm 4] [out.png]
"""

import sys
from pathlib import Path

import numpy as np
from PIL import Image

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
from artwork import inks, raster, svg_import  # noqa: E402

# Display colours per ink class (the drawing's own fills).
COLOURS = {
    inks.NONE: (0xDB, 0xD0, 0xB9),       # outside every fill: the body
    inks.GREEN: (0x00, 0x50, 0x3A),
    inks.ORANGE: (0xFF, 0x68, 0x1D),
    inks.WHITE: (0xEE, 0xEF, 0xEA),
    inks.CHARCOAL: (0x1C, 0x1B, 0x19),
    inks.CREAM: (0xDB, 0xD0, 0xB9),
}
SUPERSAMPLE = 3


def render(px_per_mm=4):
    fills, _ = svg_import.load()
    lab = raster.label_image(fills, (0.0, 0.0, 176.0, 240.0), px_per_mm * SUPERSAMPLE, inks.classify)
    rgb = np.zeros(lab.shape + (3,), np.float32)
    for k, c in COLOURS.items():
        rgb[lab == k] = c
    h, w = lab.shape[0] // SUPERSAMPLE, lab.shape[1] // SUPERSAMPLE
    rgb = rgb[:h * SUPERSAMPLE, :w * SUPERSAMPLE].reshape(h, SUPERSAMPLE, w, SUPERSAMPLE, 3).mean(axis=(1, 3))
    return Image.fromarray(np.clip(np.round(rgb), 0, 255).astype(np.uint8))


if __name__ == "__main__":
    args = [a for a in sys.argv[1:] if not a.startswith("--")]
    density = 4
    if "--px-per-mm" in sys.argv:
        density = int(sys.argv[sys.argv.index("--px-per-mm") + 1])
        args = [a for a in args if a != str(density)]
    out = Path(args[0] if args else "assets/stage/ep40_plan.png")
    img = render(density)
    img.save(out, optimize=True)
    print(f"wrote {out}: {img.width} x {img.height}")
