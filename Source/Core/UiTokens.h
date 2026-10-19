#pragma once

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>

// The redesign's colour tokens (docs/redesign/plan.md section 2.1), as plain
// 0xRRGGBB values so they can be tested without JUCE. Source/UI/Design/Theme
// wraps them as juce::Colour roles. Contrast is WCAG 2 relative luminance;
// distance is CIEDE2000.
namespace halo::tokens
{
    using Rgb = std::uint32_t;   // 0xRRGGBB
    using Argb = std::uint32_t;  // 0xAARRGGBB (legacy palettes only)

    struct Screen
    {
        Rgb bg, panel, wave, waveSel, grid, gridDot, rule, frame, mute, dim, ink, hot;
        std::array<Rgb, 8> ramp;
        Rgb paper, paperHi, paperShade, tapeEmpty, tapeWound, ledOff, hatch;
        Rgb stepA, stepB;   // paired step-key shading
        Rgb droppedTail;    // slicer: dropped (trimmed) tail fill
        Rgb neutralKey;     // ActionKey neutral cap
    };

    struct Encoder
    {
        Rgb line;    // line / text on dark
        Rgb fill;    // pills, card headers, tags
        Rgb onFill;  // text on fill
        Rgb cap;     // deck cap
        Rgb skirt;   // skirt / dim
    };

    struct Honest
    {
        Rgb mock, preview, danger, info;
    };

    struct Chassis
    {
        Rgb deckTop, deckBottom, deckEdge, glassBezel;
        Rgb key, keyDown, keyLip, keyShift, keyShiftText;
        Rgb print, printCap, printDim, grille, ledOn, ledOffDeck;
        bool brushed;
    };

    struct Palette
    {
        Screen scr;
        std::array<Encoder, 4> enc;
        Honest honest;
        Chassis deck;
    };

    constexpr Screen blackGlass()
    {
        return Screen {
            0x000005, 0x07090C, 0x121318, 0x1E2028, 0x101C26, 0x24394B, 0x1E252B, 0x383E43, 0x545E69, 0x7A8590, 0xCDD0C2, 0xFFFFFF,
            { 0x000000, 0x2F2F37, 0x484850, 0x616169, 0x7A7A82, 0x96969B, 0xAFAFB4, 0xF7F5F5 },
            0xCDD0C2, 0xF4F4EC, 0xA1A3A5, 0x29221D, 0x5E564B, 0x57304A, 0x6B7680,
            0x26272D, 0x2C2D34,
            0x33343C,
            0x1C1D21,
        };
    }

    // OP-1 field encoder set: blue, ochre, grey, orange-red.
    constexpr std::array<Encoder, 4> fieldEncoders()
    {
        return { Encoder { 0x4D7EA9, 0x2C4F7A, 0xFFFFFF, 0x2C4F7A, 0x2C4362 },
                 Encoder { 0xB38E5E, 0xB38E5E, 0x120B04, 0x9A7243, 0x8B663C },
                 Encoder { 0xA7B1B8, 0xA7B1B8, 0x06080A, 0x8C9196, 0x5B6770 },
                 Encoder { 0xEF3E24, 0xEF3E24, 0x160600, 0xF05023, 0xB0301A } };
    }

    constexpr Honest honestHues() { return { 0xFAB413, 0xB07CFF, 0xDC1E5A, 0xFF6A2A }; }

    constexpr Palette graphite()
    {
        return { blackGlass(), fieldEncoders(), honestHues(),
                 Chassis { 0x1D1D20, 0x121214, 0x2C2C30, 0x2A2A2E,
                           0x2B2C30, 0x34353A, 0x0B0B0D, 0xB4B6B9, 0x1A1B1E,
                           0x8A8C90, 0xA3A5A9, 0x55575C, 0x060607, 0xFFFFFF, 0x34343A, false } };
    }

    // Alternate skin: aluminium deck, same black screen (R13 finishes it).
    constexpr Palette field()
    {
        return { blackGlass(), fieldEncoders(), honestHues(),
                 Chassis { 0xD9DBDA, 0xC8CACB, 0xA3A6A7, 0x16181A,
                           0xE9E9E4, 0xDCDDD7, 0xAEB1AC, 0x36393C, 0xE9E9E4,
                           0x2B2B2B, 0x2B2B2B, 0x6E7173, 0x55595B, 0xFFFFFF, 0x34343A, true } };
    }

    // ---- Legacy panels (until R13) -------------------------------------------
    // The pre-redesign palette, role for role, so panels not yet rebuilt keep
    // working: legacyPaper() is today's "graph paper"; legacyDark() maps the
    // same roles onto the black glass (plan section 5.1).
    struct Legacy
    {
        Argb canvas, gridMinor, gridMajor, paper, paperHigh, metal,
             ink, inkSoft, orange, orangeHot, riddimGreen, lcdGreen, warning;
        bool dark;
    };

    constexpr Legacy legacyPaper()
    {
        return { 0xFFAEB9BE, 0x85CAD3D6, 0x57869399, 0xFFE6E8E5, 0xFFF4F4EF, 0xFFA6B0B4,
                 0xFF111315, 0xFF343A3D, 0xFFFF5A1F, 0xFFFF6A2A, 0xFF405C4C, 0xFFA6D39F, 0xFFD94A24, false };
    }

    constexpr Legacy legacyDark()
    {
        constexpr auto s = blackGlass();
        constexpr auto e = fieldEncoders();
        constexpr auto h = honestHues();
        return { 0xFF000000u | s.bg, 0x40000000u | s.rule, 0x60000000u | s.frame,
                 0xFF000000u | s.panel, 0xFF000000u | s.wave, 0xFF000000u | s.frame,
                 0xFF000000u | s.ink, 0xFF000000u | s.dim, 0xFF000000u | e[3].line, 0xFF000000u | h.info,
                 0xFF000000u | s.ramp[6], 0xFF000000u | s.ramp[6], 0xFF000000u | h.danger, true };
    }

    // ---- Measurement ---------------------------------------------------------
    inline double channelToLinear (int c)
    {
        const double v = c / 255.0;
        return v <= 0.04045 ? v / 12.92 : std::pow ((v + 0.055) / 1.055, 2.4);
    }

    inline double relativeLuminance (Rgb c)
    {
        return 0.2126 * channelToLinear ((int) (c >> 16) & 255)
             + 0.7152 * channelToLinear ((int) (c >> 8) & 255)
             + 0.0722 * channelToLinear ((int) c & 255);
    }

    inline double contrast (Rgb a, Rgb b)
    {
        const double la = relativeLuminance (a), lb = relativeLuminance (b);
        return (std::max (la, lb) + 0.05) / (std::min (la, lb) + 0.05);
    }

    struct Lab { double l, a, b; };

    inline Lab toLab (Rgb c)
    {
        const double r = channelToLinear ((int) (c >> 16) & 255), g = channelToLinear ((int) (c >> 8) & 255),
                     b = channelToLinear ((int) c & 255);
        const double x = (0.4124564 * r + 0.3575761 * g + 0.1804375 * b) / 0.95047;
        const double y = (0.2126729 * r + 0.7151522 * g + 0.0721750 * b);
        const double z = (0.0193339 * r + 0.1191920 * g + 0.9503041 * b) / 1.08883;
        auto f = [] (double t) { return t > 216.0 / 24389.0 ? std::cbrt (t) : (24389.0 / 27.0 * t + 16.0) / 116.0; };
        return { 116.0 * f (y) - 16.0, 500.0 * (f (x) - f (y)), 200.0 * (f (y) - f (z)) };
    }

    // CIEDE2000 (Sharma, Wu and Dalal 2005).
    inline double deltaE2000 (Rgb c1, Rgb c2)
    {
        constexpr double pi = 3.14159265358979323846;
        auto rad = [] (double d) { return d * pi / 180.0; };
        auto deg = [] (double r) { return r * 180.0 / pi; };
        const auto p = toLab (c1), q = toLab (c2);
        const double cAvg = (std::hypot (p.a, p.b) + std::hypot (q.a, q.b)) / 2.0;
        const double g = 0.5 * (1.0 - std::sqrt (std::pow (cAvg, 7) / (std::pow (cAvg, 7) + std::pow (25.0, 7))));
        const double a1 = (1.0 + g) * p.a, a2 = (1.0 + g) * q.a;
        const double cp1 = std::hypot (a1, p.b), cp2 = std::hypot (a2, q.b);
        auto hue = [&] (double b, double a) { const double h = deg (std::atan2 (b, a)); return h < 0 ? h + 360.0 : h; };
        const double h1 = hue (p.b, a1), h2 = hue (q.b, a2);
        const double dL = q.l - p.l, dC = cp2 - cp1;
        double dh = 0.0;
        if (cp1 * cp2 != 0.0)
        {
            dh = h2 - h1;
            if (dh > 180.0) dh -= 360.0;
            else if (dh < -180.0) dh += 360.0;
        }
        const double dH = 2.0 * std::sqrt (cp1 * cp2) * std::sin (rad (dh / 2.0));
        const double lBar = (p.l + q.l) / 2.0, cBar = (cp1 + cp2) / 2.0;
        double hBar = h1 + h2;
        if (cp1 * cp2 != 0.0)
        {
            if (std::abs (h1 - h2) <= 180.0) hBar = (h1 + h2) / 2.0;
            else if (h1 + h2 < 360.0)        hBar = (h1 + h2 + 360.0) / 2.0;
            else                             hBar = (h1 + h2 - 360.0) / 2.0;
        }
        const double t = 1.0 - 0.17 * std::cos (rad (hBar - 30.0)) + 0.24 * std::cos (rad (2.0 * hBar))
                       + 0.32 * std::cos (rad (3.0 * hBar + 6.0)) - 0.20 * std::cos (rad (4.0 * hBar - 63.0));
        const double dTheta = 30.0 * std::exp (-std::pow ((hBar - 275.0) / 25.0, 2.0));
        const double rC = 2.0 * std::sqrt (std::pow (cBar, 7) / (std::pow (cBar, 7) + std::pow (25.0, 7)));
        const double sL = 1.0 + 0.015 * std::pow (lBar - 50.0, 2.0) / std::sqrt (20.0 + std::pow (lBar - 50.0, 2.0));
        const double sC = 1.0 + 0.045 * cBar, sH = 1.0 + 0.015 * cBar * t;
        const double rT = -std::sin (rad (2.0 * dTheta)) * rC;
        return std::sqrt (std::pow (dL / sL, 2.0) + std::pow (dC / sC, 2.0) + std::pow (dH / sH, 2.0) + rT * (dC / sC) * (dH / sH));
    }

    // The one written exemption to "honest hues are at least dE 15 from every
    // encoder colour": info and encoder 4 are both orange and are told apart by
    // form (outlined box with a circled i vs. bar / pill / marker / numeral).
    struct HueExemption { const char* honest; int encoder; };
    inline constexpr std::array<HueExemption, 1> kHueExemptions { HueExemption { "info", 3 } };
}
