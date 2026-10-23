"""Neutral artwork (plan D11, `--artwork neutral`): the same legend layout set in
OFL faces (Inter Tight, Lexend Exa; assets/fonts) with generic icons, so a build
carries none of TE's own lettering, numerals, illustrations or wordmark. It
produces the same Fill list as svg_import.load(), in reference millimetres, and
feeds the same atlas and face pipeline.

Each legend's ink is chosen to read on the part's own material colour."""

import math
from pathlib import Path

import numpy as np
from fontTools.pens.basePen import BasePen
from fontTools.ttLib import TTFont

from .svg_import import Fill

FONTS = Path(__file__).resolve().parents[3] / "assets" / "fonts"
GREEN, ORANGE, WHITE = "#00503a", "#ff681d", "#eeefea"


class _FlattenPen(BasePen):
    def __init__(self, glyph_set, steps=8):
        super().__init__(glyph_set)
        self.contours, self.current, self.steps = [], [], steps

    def _moveTo(self, p):
        self.current = [p]

    def _lineTo(self, p):
        self.current.append(p)

    def _curveToOne(self, p1, p2, p3):
        p0 = self.current[-1]
        for i in range(1, self.steps + 1):
            t = i / self.steps
            u = 1 - t
            self.current.append((u ** 3 * p0[0] + 3 * u * u * t * p1[0] + 3 * u * t * t * p2[0] + t ** 3 * p3[0],
                                 u ** 3 * p0[1] + 3 * u * u * t * p1[1] + 3 * u * t * t * p2[1] + t ** 3 * p3[1]))

    def _qCurveToOne(self, p1, p2):
        p0 = self.current[-1]
        for i in range(1, self.steps + 1):
            t = i / self.steps
            u = 1 - t
            self.current.append((u * u * p0[0] + 2 * u * t * p1[0] + t * t * p2[0], u * u * p0[1] + 2 * u * t * p1[1] + t * t * p2[1]))

    def _closePath(self):
        if len(self.current) >= 3:
            self.contours.append(self.current)
        self.current = []

    _endPath = _closePath


class Face:
    def __init__(self, file):
        self.font = TTFont(str(FONTS / file))
        self.glyphs = self.font.getGlyphSet()
        self.cmap = self.font.getBestCmap()
        self.cap = self.font["OS/2"].sCapHeight

    def text(self, s, cap_mm, x, y, align="centre", tracking=0.06, rotate=0.0):
        """Outlines of `s` with capitals cap_mm tall, its cap band centred on y;
        x is the left edge, centre or right edge. rotate: degrees, about (x, y)."""
        k = cap_mm / self.cap
        pen_x, contours = 0.0, []
        for ch in s:
            name = self.cmap.get(ord(ch))
            if name is None:
                continue
            pen = _FlattenPen(self.glyphs)
            self.glyphs[name].draw(pen)
            for c in pen.contours:
                contours.append([(pen_x + px * k, -py * k) for px, py in c])
            pen_x += self.glyphs[name].width * k + tracking * cap_mm
        width = pen_x - tracking * cap_mm
        dx = {"left": 0.0, "centre": -width / 2, "right": -width}[align]
        out = []
        ca, sa = math.cos(math.radians(rotate)), math.sin(math.radians(rotate))
        for c in contours:
            pts = np.array(c) + (dx, cap_mm / 2)
            pts = np.stack([pts[:, 0] * ca - pts[:, 1] * sa, pts[:, 0] * sa + pts[:, 1] * ca], axis=1) + (x, y)
            out.append(pts)
        return out


def _rect(x0, y0, x1, y1):
    return [np.array([(x0, y0), (x1, y0), (x1, y1), (x0, y1)])]


def _circle(cx, cy, r, n=48):
    a = np.linspace(0, 2 * math.pi, n, endpoint=False)
    return [np.stack([cx + r * np.cos(a), cy + r * np.sin(a)], axis=1)]


def _ring(cx, cy, r0, r1, n=48):
    outer = _circle(cx, cy, r1, n)[0]
    inner = _circle(cx, cy, r0, n)[0][::-1]
    return [outer, inner]


def fills(spec):
    """The neutral Fill list (painter's order), laid out from ep40/spec.py."""
    sans, display = Face("InterTight-Medium.ttf"), Face("LexendExa-SemiBold.ttf")
    out = []

    def add(colour, polys):
        f = Fill(len(out), colour, False)
        for p in polys:
            f.subpaths.append(np.asarray(p, dtype=np.float64))
            f.boxes.append((*f.subpaths[-1].min(axis=0), *f.subpaths[-1].max(axis=0)))
        out.append(f)

    cell = spec.CELL.mm
    centre = {name: (spec.COLUMN_LEFT[c] + cell / 2, spec.ROW_TOP[r] + cell / 2) for name, c, r in spec.FULL_KEYS}
    # Numerals: a neutral geometric sans, large, on the white pads.
    for n in range(10):
        x, y = centre[f"pad_{n}"]
        add(GREEN, display.text(str(n), 8.0, x, y, tracking=0.0))
    # Group pads: generic icons (circle, ring, bars, dot grid).
    gx, gy = centre["group_a"]
    add(GREEN, _circle(gx, gy, 4.0))
    gx, gy = centre["group_b"]
    add(ORANGE, _ring(gx, gy, 2.2, 4.0))
    gx, gy = centre["group_c"]
    add(GREEN, [p for i in range(4) for p in _rect(gx - 4.5 + i * 2.5, gy - 4.0, gx - 3.0 + i * 2.5, gy + 4.0)])
    gx, gy = centre["group_d"]
    add(ORANGE, [p for i in range(3) for j in range(3) for p in _circle(gx - 3 + 3 * i, gy - 3 + 3 * j, 0.9, 20)])
    x, y = centre["pad_dot"]
    add(ORANGE, _rect(x - 2.0, y - 2.0, x + 2.0, y + 2.0))
    x, y = centre["pad_enter"]
    add(ORANGE, sans.text("ENTER", 1.8, x, y))
    x, y = centre["button_minus"]
    add(GREEN, _rect(x - 2.5, y - 0.4, x + 2.5, y + 0.4))
    x, y = centre["button_plus"]
    add(GREEN, _rect(x - 2.5, y - 0.4, x + 2.5, y + 0.4) + _rect(x - 0.4, y - 2.5, x + 0.4, y + 2.5))
    x, y = centre["button_record"]
    add(WHITE, sans.text("RECORD", 1.8, x, y))
    x, y = centre["button_play"]
    add(WHITE, sans.text("PLAY", 1.8, x, y))

    # Function cells: short caps (beige, green ink; orange / green caps get white),
    # and their plates.
    words = {"button_sound": ("SOUND", "EDIT"), "button_main": ("MAIN", "COMMIT"), "button_tempo": ("TEMPO", "LOOP"),
             "button_sample": ("SAMPLE", "CHOP"), "button_timing": ("TIMING", "CORRECT"),
             "button_fx": ("FX", "OUTPUT"), "button_erase": ("ERASE", "SYSTEM")}
    cap_ink = {"button_sample": WHITE, "button_timing": WHITE}
    plate_ink = {"button_main": WHITE, "button_tempo": WHITE, "button_sample": WHITE, "button_timing": WHITE}
    for cap, plate, c, r in spec.FUNCTION_CELLS:
        x, top = spec.COLUMN_LEFT[c] + cell / 2, spec.ROW_TOP[r]
        upper, lower = words[cap]
        add(cap_ink.get(cap, GREEN), sans.text(upper, 1.6, x, top + 4.0))
        add(plate_ink.get(cap, GREEN), sans.text(lower, 1.6, x, top + cell - 4.0))
    sx = sum(spec.SMALL_KEY_X) / 2
    for name, word in (("button_keys", "KEYS"), ("fader_mode_button", "FADER"), ("button_shift", "SHIFT")):
        y0, y1 = spec.SMALL_KEY_Y[name]
        add(GREEN, sans.text(word, 1.6, sx, (y0 + y1) / 2))
    # Port inserts.
    for name, word, ink in (("port_output_tab", "OUT", GREEN), ("port_input_tab", "IN", WHITE),
                            ("port_sync_tab", "SYNC", WHITE), ("port_midi_tab", "MIDI", WHITE), ("port_usb_tab", "USB", GREEN)):
        x0, y0, x1, y1 = spec.PORT_TABS[name]
        add(ink, sans.text(word, 1.5, (x0 + x1) / 2, (y0 + y1) / 2 + 0.3))

    # Deck silkscreen: parameter names right of their LEDs, the bracket row,
    # GAIN and SWING, X and Y.
    names = [["LEVEL", "PITCH", "TIME"], ["LPF", "HPF", "FX"], ["ATK", "REL", "PAN"], ["TUNE", "VEL", "MOD"]]
    for row in range(4):
        for col in range(3):
            lx, ly = spec.LEDS[row * 4 + col + 1]
            add(GREEN, sans.text(names[row][col], 1.5, lx + 2.2, ly, align="left"))
    row_y, stroke = 108.0, 0.25
    def line(x0, y0, x1, y1, colour=GREEN):
        add(colour, _rect(min(x0, x1) - stroke / 2, min(y0, y1) - stroke / 2, max(x0, x1) + stroke / 2, max(y0, y1) + stroke / 2))
    add(GREEN, sans.text("VOLUME", 1.5, spec.KNOBS["knob_volume"][0], row_y))
    for seg in ((40.0, row_y, 48.6, row_y), (56.0, row_y, 64.4, row_y), (40.0, row_y, 40.0, row_y + 2.6), (64.4, row_y, 64.4, row_y + 2.6),
                (88.0, row_y, 88.0, row_y + 2.6), (88.0, row_y, 131.8, row_y), (141.0, row_y, 146.8, row_y)):
        line(*seg)
    add(GREEN, sans.text("LIVE", 1.5, 52.3, row_y))
    add(GREEN, sans.text("BPM", 1.5, 136.4, row_y))
    add(GREEN, sans.text("METRONOME", 1.5, 160.2, row_y))
    for x, sign, colour, word, rot in ((124.2, 1, ORANGE, "GAIN", -90.0), (172.3, -1, GREEN, "SWING", 90.0)):
        line(x, 114.6, x, 124.2, colour)
        line(x, 133.8, x, 141.5, colour)
        line(x, 114.6, x + sign * 2.0, 114.6, colour)
        line(x, 141.5, x + sign * 2.0, 141.5, colour)
        add(colour, sans.text(word, 1.5, x, 129.0, rotate=rot))
    add(ORANGE, display.text("X", 3.6, spec.KNOBS["knob_x"][0], 131.6, tracking=0.0))
    add(GREEN, display.text("Y", 3.6, spec.KNOBS["knob_y"][0], 131.6, tracking=0.0))
    line(4.0, 188.0, 13.0, 188.0)     # the fader's centre tick
    line(18.5, 188.0, 27.5, 188.0)

    # Label plate: Halo's own name in place of TE's wordmark.
    lx0, ly0, lx1, ly1 = spec.LABEL_PLATE
    add(GREEN, display.text("HALO", 7.0, lx0 + 6.0, ly0 + 11.0, align="left", tracking=0.02))
    add(ORANGE, sans.text("SAMPLE COMPANION", 2.2, lx0 + 6.0, ly0 + 19.0, align="left"))
    add(GREEN, sans.text("SAMPLER STAGE", 2.0, lx0 + 6.0, ly1 - 4.0, align="left"))
    add(GREEN, sans.text("POWER", 1.5, 156.0, 4.5))
    return out
