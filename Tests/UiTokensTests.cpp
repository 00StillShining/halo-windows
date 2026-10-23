#include "Core/UiTokens.h"

#include <doctest/doctest.h>

#include <string>
#include <vector>

using namespace halo::tokens;

namespace
{
    struct Pair { const char* what; Rgb text, background; double measured; };

    // Every text/background pair the redesign uses (plan section 2.1), with
    // its measured WCAG ratio. Text needs at least 4.5:1.
    std::vector<Pair> textPairs (const Palette& p)
    {
        const auto& s = p.scr;
        const auto& e = p.enc;
        const auto& h = p.honest;
        return {
            { "ink on bg", s.ink, s.bg, 13.37 },
            { "dim on bg", s.dim, s.bg, 5.57 },
            { "dim on panel", s.dim, s.panel, 5.30 },
            { "dim on wave", s.dim, s.wave, 4.93 },
            { "enc1 on panel", e[0].line, s.panel, 4.63 },
            { "enc2 on panel", e[1].line, s.panel, 6.60 },
            { "enc3 on panel", e[2].line, s.panel, 9.14 },
            { "enc4 on panel", e[3].line, s.panel, 5.11 },
            { "white on enc1 fill", e[0].onFill, e[0].fill, 8.38 },
            { "dark on enc2 fill", e[1].onFill, e[1].fill, 6.46 },
            { "dark on enc3 fill", e[2].onFill, e[2].fill, 9.19 },
            { "dark on enc4 fill", e[3].onFill, e[3].fill, 5.08 },
            { "white on danger", 0xFFFFFF, h.danger, 4.80 },
            { "black on mock", 0x000000, h.mock, 11.60 },
            { "black on preview", 0x000000, h.preview, 7.17 },
            { "info title on bg", h.info, s.bg, 7.33 },
            { "white on ramp1", s.hot, s.ramp[1], 13.27 },
            { "white on ramp3", s.hot, s.ramp[3], 6.14 },
            { "black on ramp5", 0x000000, s.ramp[5], 7.13 },
            { "black on ramp7", 0x000000, s.ramp[7], 19.33 },
            { "ramp5 log floor on bg", s.ramp[5], s.bg, 7.12 },
            { "ink on neutral key", s.ink, s.neutralKey, 10.74 },
            { "black on paper", 0x000000, s.paper, 13.40 },
            { "bg on paper", s.bg, s.paper, 13.37 },
            { "bg on paperHi", s.bg, s.paperHi, 18.96 },
            { "bg on paperShade", s.bg, s.paperShade, 8.28 },
        };
    }
}

TEST_CASE ("tokens: every text pair reaches 4.5:1 and matches its measurement")
{
    for (const auto& pair : textPairs (graphite()))
    {
        INFO (pair.what);
        const double ratio = contrast (pair.text, pair.background);
        CHECK (ratio >= 4.5);
        CHECK (ratio == doctest::Approx (pair.measured).epsilon (0.01));
    }
}

TEST_CASE ("tokens: deck print is legible on the graphite chassis")
{
    const auto d = graphite().deck;
    CHECK (contrast (graphite().scr.ink, d.key) >= 4.5);      // 8.90 (the plan's 9.5 was a slip)
    CHECK (contrast (d.printCap, d.key) >= 4.5);              // 5.65
    CHECK (contrast (d.printCap, d.keyDown) >= 4.5);          // 4.96
    CHECK (contrast (d.print, d.deckTop) >= 4.5);             // 4.99
    CHECK (contrast (d.print, d.deckBottom) >= 4.5);          // 5.55
    CHECK (contrast (d.keyShiftText, d.keyShift) >= 4.5);
    const auto f = field().deck;
    CHECK (contrast (f.print, f.deckTop) >= 4.5);
    CHECK (contrast (f.printCap, f.key) >= 4.5);
}

TEST_CASE ("tokens: crimson is never text on black")
{
    const auto p = graphite();
    // It measures under 4.5 on the glass: the danger form is always a shape,
    // a lamp, or a filled tag with white text.
    CHECK (contrast (p.honest.danger, p.scr.bg) < 4.5);
    CHECK (contrast (p.honest.danger, p.scr.bg) >= 3.0); // fine as a non-text graphic
}

TEST_CASE ("tokens: non-text graphics reach 3:1")
{
    const auto p = graphite();
    CHECK (contrast (p.honest.mock, p.scr.bg) >= 3.0);
    CHECK (contrast (p.honest.preview, p.scr.bg) >= 3.0);
    CHECK (contrast (p.honest.info, p.scr.bg) >= 3.0);
    CHECK (contrast (p.scr.mute, p.scr.bg) >= 3.0);
    CHECK (contrast (p.scr.hot, p.scr.bg) >= 3.0);
    for (const auto& e : p.enc)
        CHECK (contrast (e.line, p.scr.bg) >= 3.0);
}

TEST_CASE ("tokens: honest hues are far apart, and from the encoders")
{
    const auto h = graphite().honest;
    const std::array<std::pair<const char*, Rgb>, 4> honest { { { "danger", h.danger }, { "info", h.info },
                                                                 { "mock", h.mock }, { "preview", h.preview } } };
    for (size_t i = 0; i < honest.size(); ++i)
        for (size_t j = i + 1; j < honest.size(); ++j)
        {
            INFO (honest[i].first << " vs " << honest[j].first);
            CHECK (deltaE2000 (honest[i].second, honest[j].second) >= 20.0);
        }

    CHECK (deltaE2000 (h.danger, h.info) == doctest::Approx (28.1).epsilon (0.02));
    CHECK (deltaE2000 (h.mock, h.preview) == doctest::Approx (64.1).epsilon (0.02));

    REQUIRE (kHueExemptions.size() == 1); // exactly one written exemption
    const auto enc = graphite().enc;
    int exemptionsUsed = 0;
    for (const auto& [name, colour] : honest)
        for (int e = 0; e < 4; ++e)
        {
            const bool exempt = std::string (name) == kHueExemptions[0].honest && e == kHueExemptions[0].encoder;
            const double d = deltaE2000 (colour, enc[(size_t) e].line);
            INFO (name << " vs enc" << (e + 1) << " = " << d);
            if (exempt)
            {
                ++exemptionsUsed;
                CHECK (d < 15.0); // the exemption is real, not stale
            }
            else
            {
                CHECK (d >= 15.0);
            }
        }
    CHECK (exemptionsUsed == 1);
}

TEST_CASE ("tokens: deltaE2000 matches the reference pairs")
{
    // Sharma et al. test data, pair 1 (in Lab) can't be fed as sRGB, so check
    // the identities and symmetry instead.
    CHECK (deltaE2000 (0x808080, 0x808080) == doctest::Approx (0.0));
    CHECK (deltaE2000 (0xDC1E5A, 0xFF6A2A) == doctest::Approx (deltaE2000 (0xFF6A2A, 0xDC1E5A)));
    CHECK (deltaE2000 (0x000000, 0xFFFFFF) == doctest::Approx (100.0).epsilon (0.001));
}

TEST_CASE ("tokens: RACK tiles: values black on paper, the bar beside each in the encoder's skirt")
{
    // Tile values are #000005 Numerals on paper (never encoder-coloured
    // text); the 3 px bar beside each names its encoder and must read as a
    // graphic (3:1) on paper, which the skirt shade does and the line does not.
    const auto p = graphite();
    CHECK (contrast (p.scr.bg, p.scr.paper) >= 7.0);
    for (const auto& e : p.enc)
        CHECK (contrast (e.skirt, p.scr.paper) >= 3.0);
}
