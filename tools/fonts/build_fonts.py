"""Builds Halo's embedded UI fonts (docs/redesign/plan.md section 2.2).

Run in the tools venv, isolated, with the folder holding the downloaded OFL
sources (Google Fonts variable TTFs, the JetBrains Mono and Departure Mono
release files) and their licences:

    tools\\.venv\\Scripts\\python -I tools/fonts/build_fonts.py <downloads folder>

It
 1. instances the variable fonts at the weights roles.json names (static faces
    are the safe path through JUCE's typeface loader); converts Departure Mono
    from CFF to TrueType outlines;
 2. subsets every face to Latin-1 plus a few typographic marks;
 3. asserts cmap coverage for every role's characters (fails on a miss);
 4. reads each licence's Reserved Font Names and fails if an output family
    name uses one (subsetting makes a Modified Version under the OFL);
 5. writes assets/fonts/*.ttf plus OFL-*.txt, checks each family name, and
    emits Source/Core/UiFontMetrics.h (advances, tracking, cap heights per role)
    so pure C++ layout tests can measure real strings without JUCE.
"""

import json
import pathlib
import re
import shutil
import sys

from fontTools import subset
from fontTools.pens.cu2quPen import Cu2QuPen
from fontTools.pens.ttGlyphPen import TTGlyphPen
from fontTools.ttLib import TTFont, newTable
from fontTools.varLib import instancer

HERE = pathlib.Path(__file__).resolve().parent
ROOT = HERE.parent.parent
OUT_FONTS = ROOT / "assets" / "fonts"
OUT_HEADER = ROOT / "Source" / "Core" / "UiFontMetrics.h"
SIZE_BUDGET = 600 * 1024


def fail(msg):
    print("build_fonts: FAILED: " + msg)
    sys.exit(1)


# ---- CFF -> TrueType (fontTools' otf2ttf recipe) ------------------------------
def otf_to_ttf(font):
    glyph_order = font.getGlyphOrder()
    glyph_set = font.getGlyphSet()
    font["loca"] = newTable("loca")
    font["glyf"] = glyf = newTable("glyf")
    glyf.glyphOrder = glyph_order
    glyf.glyphs = {}
    for name in glyph_order:
        tt_pen = TTGlyphPen(glyph_set)
        glyph_set[name].draw(Cu2QuPen(tt_pen, 1.0, reverse_direction=True))
        glyf.glyphs[name] = tt_pen.glyph()
    del font["CFF "]
    if "VORG" in font:
        del font["VORG"]
    glyf.compile(font)
    hmtx = font["hmtx"]
    for name, glyph in glyf.glyphs.items():
        if hasattr(glyph, "xMin"):
            hmtx[name] = (hmtx[name][0], glyph.xMin)
    font["maxp"] = maxp = newTable("maxp")
    maxp.tableVersion = 0x00010000
    for attr in ("maxZones", "maxTwilightPoints", "maxStorage", "maxFunctionDefs", "maxInstructionDefs",
                 "maxStackElements", "maxSizeOfInstructions", "maxComponentElements"):
        setattr(maxp, attr, 0)
    maxp.maxZones = 1
    maxp.compile(font)
    post = font["post"]
    post.formatType = 2.0
    post.extraNames = []
    post.mapping = {}
    post.glyphOrder = glyph_order
    font.sfntVersion = "\000\001\000\000"


def family_names(font):
    names = {}
    for rec in font["name"].names:
        if rec.nameID in (1, 2, 4, 6, 16, 17):
            try:
                names.setdefault(rec.nameID, rec.toUnicode())
            except UnicodeDecodeError:
                pass
    return names


def reserved_names(licence_text):
    # Only the copyright header names RFNs; the licence body defines the term.
    licence_text = licence_text.split("This Font Software is licensed", 1)[0]
    found = re.findall(r"Reserved Font Names?\s*[\"“']([^\"”']+)[\"”']", licence_text)
    return [n.strip() for n in found]


def codepoints(text):
    return sorted({ord(c) for c in text})


def main():
    if len(sys.argv) < 2:
        fail("usage: build_fonts.py <downloads folder>")
    src = pathlib.Path(sys.argv[1]).resolve()
    spec = json.loads((HERE / "roles.json").read_text(encoding="utf-8"))
    OUT_FONTS.mkdir(parents=True, exist_ok=True)

    latin1 = list(range(0x20, 0x7F)) + list(range(0xA0, 0x100))
    unicodes = sorted(set(latin1) | set(codepoints(spec["subset_extra"])))

    built = {}
    total = 0
    for face, info in spec["faces"].items():
        path = src / info["source"]
        if not path.exists():
            fail(f"missing source {path}")
        font = TTFont(str(path))
        if "fvar" in font:
            if "wght" not in info:
                fail(f"{face}: variable source needs a wght")
            font = instancer.instantiateVariableFont(font, {"wght": info["wght"]}, updateFontNames=True)
        elif "wght" in info and info.get("wght") not in (None, 400):
            fail(f"{face}: static source can't be instanced at {info['wght']}")
        if font.sfntVersion == "OTTO":
            otf_to_ttf(font)

        options = subset.Options()
        options.layout_features = ["kern", "tnum", "lnum", "case", "calt", "liga", "ccmp", "locl", "mark", "mkmk"]
        options.name_IDs = ["*"]
        options.name_languages = ["*"]
        options.hinting = True
        options.notdef_outline = True
        options.glyph_names = False
        sub = subset.Subsetter(options)
        sub.populate(unicodes=unicodes)
        sub.subset(font)

        # Licence and Reserved Font Names.
        lic_path = src / info["licence"]
        if not lic_path.exists():
            fail(f"{face}: missing licence {lic_path}")
        lic = lic_path.read_text(encoding="utf-8", errors="replace")
        if "SIL Open Font License" not in lic:
            fail(f"{face}: {lic_path.name} is not an OFL licence")
        rfns = reserved_names(lic)
        names = family_names(font)
        for rfn in rfns:
            for nid, value in names.items():
                if rfn.lower() in value.lower():
                    fail(f"{face}: name ID {nid} '{value}' uses the Reserved Font Name '{rfn}'")
        print(f"{face}: family '{names.get(16, names.get(1))}' style '{names.get(17, names.get(2))}'; "
              f"RFN {rfns if rfns else 'none'} (not used by the output names)")

        out = OUT_FONTS / f"{face}.ttf"
        font.save(str(out))
        shutil.copyfile(lic_path, OUT_FONTS / lic_path.name)
        total += out.stat().st_size
        built[face] = TTFont(str(out))

    # Coverage per role.
    sets = spec["sets"]
    for role, r in spec["roles"].items():
        font = built[r["face"]]
        cmap = font.getBestCmap()
        needed = "".join(sets[s] for s in r["needs"].split())
        missing = [c for c in needed if ord(c) not in cmap]
        if missing:
            fail(f"role {role} ({r['face']}) misses {''.join(missing)!r}")

    if total > SIZE_BUDGET:
        fail(f"faces total {total // 1024} KB, over the {SIZE_BUDGET // 1024} KB budget")
    print(f"faces: {len(built)}, {total // 1024} KB in total")

    # ---- Metrics header ----------------------------------------------------
    chars = spec["metrics_chars"]
    cps = codepoints(chars)
    lines = [
        "#pragma once",
        "",
        "// GENERATED by tools/fonts/build_fonts.py from assets/fonts/*.ttf. Do not edit.",
        "// Per type role: em size (px), tracking (em), cap height, ascent and descent (px)",
        "// and the advance width (px, at that em) of every character layout measures.",
        "",
        "#include <array>",
        "#include <cstdint>",
        "#include <string_view>",
        "",
        "namespace halo::fontmetrics",
        "{",
        f"    inline constexpr int kGlyphCount = {len(cps)};",
        "    inline constexpr std::array<char32_t, kGlyphCount> kCodepoints {",
        "        " + ", ".join(f"0x{cp:04X}" for cp in cps),
        "    };",
        "",
        "    struct Role",
        "    {",
        "        std::string_view name, face;",
        "        float em, tracking, capHeight, ascent, descent;",
        "        std::array<float, kGlyphCount> advance;",
        "    };",
        "",
    ]
    role_names = []
    for role, r in spec["roles"].items():
        font = built[r["face"]]
        upm = font["head"].unitsPerEm
        em = float(r["em"])
        scale = em / upm
        cmap = font.getBestCmap()
        hmtx = font["hmtx"]
        os2 = font["OS/2"]
        cap = getattr(os2, "sCapHeight", 0) or 0
        if cap <= 0:
            glyf = font["glyf"]
            g = glyf[cmap[ord("H")]]
            cap = g.yMax if hasattr(g, "yMax") else 0.7 * upm
        hhea = font["hhea"]
        adv = []
        for cp in cps:
            gname = cmap.get(cp)
            adv.append(hmtx[gname][0] * scale if gname else 0.0)
        ident = "k" + role[0].upper() + role[1:]
        role_names.append(ident)
        lines.append(f"    inline constexpr Role {ident} {{ \"{role}\", \"{r['face']}\", {em}f, {float(r['tracking'])}f, "
                     f"{cap * scale:.3f}f, {hhea.ascent * scale:.3f}f, {-hhea.descent * scale:.3f}f, {{")
        for i in range(0, len(adv), 12):
            lines.append("        " + ", ".join(f"{a:.3f}f" for a in adv[i:i + 12]) + ",")
        lines.append("    } };")
        lines.append("")
    lines += [
        "    inline constexpr std::array<const Role*, " + str(len(role_names)) + "> kRoles { " + ", ".join("&" + n for n in role_names) + " };",
        "",
        "    // Advance of one character, or the advance of 'M' for anything unmeasured.",
        "    constexpr float advanceOf (const Role& r, char32_t c)",
        "    {",
        "        for (int i = 0; i < kGlyphCount; ++i)",
        "            if (kCodepoints[(std::size_t) i] == c)",
        "                return r.advance[(std::size_t) i];",
        "        for (int i = 0; i < kGlyphCount; ++i)",
        "            if (kCodepoints[(std::size_t) i] == U'M')",
        "                return r.advance[(std::size_t) i];",
        "        return r.em;",
        "    }",
        "}",
        "",
    ]
    OUT_HEADER.write_text("\n".join(lines), encoding="utf-8", newline="\n")
    print(f"wrote {OUT_HEADER.relative_to(ROOT)} ({len(role_names)} roles, {len(cps)} glyphs each)")


if __name__ == "__main__":
    main()
