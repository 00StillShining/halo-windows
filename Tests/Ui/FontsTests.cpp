#include "Core/UiFontMetrics.h"
#include "UI/Design/Fonts.h"

#include <doctest/doctest.h>

#include <juce_gui_basics/juce_gui_basics.h>

using namespace halo;

namespace
{
    float inkHeight (const juce::Font& font, const juce::String& text)
    {
        juce::GlyphArrangement ga;
        ga.addLineOfText (font, text, 0.0f, 0.0f);
        juce::Path outline;
        ga.createPath (outline);
        return outline.getBounds().getHeight();
    }

    float width (const juce::Font& font, const juce::String& text)
    {
        return juce::GlyphArrangement::getStringWidth (font, text);
    }
}

TEST_CASE ("fonts: all nine embedded faces register")
{
    REQUIRE (HaloFonts::instance() != nullptr);
    CHECK (HaloFonts::instance()->registeredCount() == 9);
    for (int f = 0; f < (int) HaloFonts::Face::count; ++f)
        CHECK (HaloFonts::instance()->typeface ((HaloFonts::Face) f) != nullptr);
}

TEST_CASE ("fonts: sizes are em sizes (cap height of H within 0.5 px of the table)")
{
    for (size_t i = 0; i < fontmetrics::kRoles.size(); ++i)
    {
        const auto& m = *fontmetrics::kRoles[i];
        const auto role = (Role) i;
        INFO (std::string (m.name));
        CHECK (HaloFonts::roleEm (role) == doctest::Approx (m.em));
        // Measure without tracking so only the em size is under test.
        const auto font = HaloFonts::font (role).withExtraKerningFactor (0.0f);
        CHECK (std::abs (inkHeight (font, "H") - m.capHeight) <= 0.5f);
    }
}

TEST_CASE ("fonts: tracking is converted from em to JUCE's height-relative factor")
{
    for (auto role : { Role::label, Role::tag, Role::title })
    {
        const float em = HaloFonts::roleEm (role);
        const float tracking = HaloFonts::roleTracking (role);
        const auto tracked = HaloFonts::font (role);
        const auto plain = tracked.withExtraKerningFactor (0.0f);
        const juce::String text ("HALOHALO");
        const float extra = width (tracked, text) - width (plain, text);
        // CSS letter-spacing: tracking x em after every character.
        INFO ("role " << (int) role << " extra " << extra);
        CHECK (std::abs (extra - tracking * em * (float) text.length()) <= 0.6f);
    }
}

TEST_CASE ("fonts: the generated metrics match the faces")
{
    for (auto role : { Role::ui, Role::body, Role::uiLight, Role::mono, Role::print })
    {
        const auto& m = *fontmetrics::kRoles[(size_t) role];
        const auto font = HaloFonts::font (role);
        for (auto ch : { U'A', U'm', U'0', U'1', U' ', U'.' })
        {
            INFO (std::string (m.name) << " char " << (int) ch);
            CHECK (std::abs (width (font, juce::String::charToString ((juce::juce_wchar) ch)) - fontmetrics::advanceOf (m, ch)) <= 0.35f);
        }
    }
}

TEST_CASE ("fonts: symbols are never font glyphs")
{
    // Every symbol in the UI is a Pictograms path, so the text faces must not
    // carry them (nothing may fall back to Segoe UI Symbol either).
    const juce::juce_wchar symbols[] { 0x25B6, 0x25A0, 0x25CF, 0x25CB, 0x2191, 0x2193, 0x2190, 0x2192, 0x23CE, 0x2303, 0x2702, 0x2713 };
    for (int f = 0; f < (int) HaloFonts::Face::count; ++f)
    {
        const auto tf = HaloFonts::instance()->typeface ((HaloFonts::Face) f);
        REQUIRE (tf != nullptr);
        for (auto cp : symbols)
        {
            INFO ("face " << f << " codepoint " << (int) cp);
            CHECK_FALSE (tf->getNominalGlyphForCodepoint (cp).has_value());
        }
    }
}
