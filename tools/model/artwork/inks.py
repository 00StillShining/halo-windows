"""The drawing's fills grouped into the four inks of the SDF atlases (plan
section 4.5), plus the cap colours they sit on."""

NONE, GREEN, ORANGE, WHITE, CHARCOAL, CREAM = 0, 1, 2, 3, 4, 5
NAMES = {NONE: "none", GREEN: "green", ORANGE: "orange", WHITE: "white", CHARCOAL: "charcoal", CREAM: "cream"}

# Atlas channel per ink (R green, G orange, B white, A charcoal). Cream lettering
# (on a dark cap) prints in the white ink.
CHANNEL = {GREEN: 0, ORANGE: 1, WHITE: 2, CREAM: 2, CHARCOAL: 3}

# Display colours of the inks (sRGB), as part.frag prints them.
INK_SRGB = {GREEN: "#00503A", ORANGE: "#FF681D", WHITE: "#F4F2EC", CHARCOAL: "#2B2B28"}

_PALETTE = {
    GREEN: [(0x00, 0x50, 0x3A)],
    ORANGE: [(0xFF, 0x68, 0x1D)],
    WHITE: [(0xEE, 0xEF, 0xEA), (0xFF, 0xFF, 0xFF)],
    CREAM: [(0xDB, 0xD0, 0xB9)],
    CHARCOAL: [(0x0F, 0x0E, 0x12), (0x00, 0x00, 0x00), (0x4D, 0x51, 0x58)],
}


def classify(hex_colour):
    h = hex_colour.lstrip("#")
    rgb = tuple(int(h[i:i + 2], 16) for i in (0, 2, 4))
    best, dist = NONE, 1e9
    for ink, refs in _PALETTE.items():
        for r in refs:
            d = sum((a - b) ** 2 for a, b in zip(rgb, r))
            if d < dist:
                best, dist = ink, d
    return best
