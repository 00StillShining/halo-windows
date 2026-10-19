#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

namespace halo::numerals
{
    // Halo Numerals (docs/redesign/plan.md section 2.2): stroked monoline
    // digits on a 10 x 16 grid, traced from the hybrid mockups. 0 is a
    // stadium, 1 a bare bar (so 12 reads "I2"), 5 is squared, 3 has a flat
    // top. Not a font: each glyph is a cached Path, stroked at a fixed screen
    // width (1.5 px up to cap 26, 1.75 px from cap 36).
    //
    // Set: 0-9 . , - (and U+2212) + : ~ % ° / space, and U+2013 "no value".

    inline constexpr float XL = 46.0f, L = 36.0f, M = 26.0f, S = 20.0f, XS = 16.0f;

    float strokeFor (float capHeight);

    // Ink width of the text at this cap height (the last glyph's trailing gap excluded).
    float width (const juce::String&, float capHeight, bool tabular = false);

    // Draws with the digits' bottom (baseline) at `baseline`; returns the ink's right edge.
    float draw (juce::Graphics&, const juce::String&, float x, float baseline, float capHeight, juce::Colour,
                bool tabular = false);

    // Right-aligned to `right`.
    void drawRight (juce::Graphics&, const juce::String&, float right, float baseline, float capHeight, juce::Colour,
                    bool tabular = false);

    // A value and its unit ("4.0 s", "-12 db"): the unit is set in ui.light at
    // 40% of the cap height, baseline-aligned, after a small gap.
    juce::Font unitFont (float capHeight);
    float widthWithUnit (const juce::String& value, const juce::String& unit, float capHeight);
    float drawWithUnit (juce::Graphics&, const juce::String& value, const juce::String& unit, float x, float baseline,
                        float capHeight, juce::Colour valueColour, juce::Colour unitColour);
    void drawWithUnitRight (juce::Graphics&, const juce::String& value, const juce::String& unit, float right, float baseline,
                            float capHeight, juce::Colour valueColour, juce::Colour unitColour);

    // The glyphs' unit-space paths, for tests and the gallery.
    bool hasGlyph (juce::juce_wchar);
}
