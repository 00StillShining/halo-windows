#include "UI/Design/Numerals.h"
#include "UI/Design/Fonts.h"

#include <map>

namespace halo::numerals
{
    namespace
    {
        constexpr float gridHeight = 16.0f;
        constexpr float gap = 4.2f; // between glyph boxes, in grid units

        struct Glyph
        {
            juce::Path path;
            float advance = 14.2f;
        };

        // Path data in grid units (y down, 0..16), from hybrid-play.svg / hybrid-chop.svg.
        const std::map<juce::juce_wchar, Glyph>& glyphs()
        {
            static const std::map<juce::juce_wchar, Glyph> table = []
            {
                auto p = [] (const char* svg) { return juce::Drawable::parseSVGPath (svg); };
                std::map<juce::juce_wchar, Glyph> t;
                t['0'] = { p ("M0,5 A5,5 0 0 1 10,5 L10,11 A5,5 0 0 1 0,11 Z"), 14.2f };
                t['1'] = { p ("M1,0 L1,16"), 6.2f };
                t['2'] = { p ("M0.4,4.6 A4.8,4.6 0 0 1 10,4.6 C10,9 0.2,11.5 0.2,16 L10,16"), 14.2f };
                t['3'] = { p ("M0.5,0 L9.6,0 L4.2,6.4 A4.9,4.8 0 1 1 0.2,13.6"), 14.2f };
                t['4'] = { p ("M7.6,16 L7.6,0 L0,11.2 L10,11.2"), 14.2f };
                t['5'] = { p ("M9.6,0 L0.6,0 L0.6,6.8 L5.4,6.8 A4.6,4.6 0 0 1 5.4,16 L0,16"), 14.2f };
                t['6'] = { p ("M8.4,0 L1.4,8.6 M0,11 A5,5 0 1 0 10,11 A5,5 0 1 0 0,11 Z"), 14.2f };
                t['7'] = { p ("M0,0 L10,0 L3,16"), 14.2f };
                t['8'] = { p ("M1.4,3.6 A3.6,3.6 0 1 1 8.6,3.6 A3.6,3.6 0 1 1 1.4,3.6 Z M0,11.6 A5,4.4 0 1 0 10,11.6 A5,4.4 0 1 0 0,11.6 Z"), 14.2f };
                t['9'] = { p ("M1.6,16 L8.6,7.4 M0,5 A5,5 0 1 0 10,5 A5,5 0 1 0 0,5 Z"), 14.2f };
                t['.'] = { p ("M1,15.2 L1,16"), 6.2f };
                t[','] = t['.'];
                t['-'] = { p ("M0,9 L7,9"), 11.2f };
                t[0x2212] = t['-'];
                t[0x2013] = { p ("M0,9 L10,9"), 14.2f };   // no value
                t['+'] = { p ("M0,9 L8,9 M4,5 L4,13"), 12.2f };
                t[':'] = { p ("M1,5 L1,5.6 M1,12 L1,12.6"), 6.2f };
                t['~'] = { p ("M0,10 C2,6 4,6 4.5,8.5 C5,11 7,11 9,7"), 13.2f };
                t['%'] = { p ("M9,0 L1,16 M0.2,3 A2,2.6 0 1 0 4.2,3 A2,2.6 0 1 0 0.2,3 Z M5.8,13 A2,2.6 0 1 0 9.8,13 A2,2.6 0 1 0 5.8,13 Z"), 14.2f };
                t[0x00B0] = { p ("M0.4,2.6 A2.2,2.2 0 1 0 4.8,2.6 A2.2,2.2 0 1 0 0.4,2.6 Z"), 9.0f };
                t['/'] = { p ("M7,0 L1,16"), 11.2f };
                t[' '] = { juce::Path(), 6.0f };
                return t;
            }();
            return table;
        }

        const Glyph* find (juce::juce_wchar c)
        {
            const auto& t = glyphs();
            const auto it = t.find (c);
            return it != t.end() ? &it->second : nullptr;
        }

        float advanceOf (const Glyph& g, juce::juce_wchar c, bool tabular)
        {
            return tabular && c >= '0' && c <= '9' ? 14.2f : g.advance;
        }

        // Tabular '1' sits centred in a full digit box.
        float offsetOf (juce::juce_wchar c, bool tabular)
        {
            return tabular && c == '1' ? 4.0f : 0.0f;
        }
    }

    float strokeFor (float capHeight)
    {
        return juce::jmap (juce::jlimit (26.0f, 36.0f, capHeight), 26.0f, 36.0f, 1.5f, 1.75f);
    }

    bool hasGlyph (juce::juce_wchar c) { return find (c) != nullptr; }

    float width (const juce::String& text, float capHeight, bool tabular)
    {
        const float s = capHeight / gridHeight;
        float units = 0.0f;
        bool any = false;
        for (auto c : text)
            if (const auto* g = find (c))
            {
                units += advanceOf (*g, c, tabular);
                any = true;
            }
        return any ? (units - gap) * s : 0.0f;
    }

    float draw (juce::Graphics& g, const juce::String& text, float x, float baseline, float capHeight, juce::Colour colour,
                bool tabular)
    {
        const float s = capHeight / gridHeight;
        const juce::PathStrokeType stroke (strokeFor (capHeight) / s, juce::PathStrokeType::curved, juce::PathStrokeType::rounded);
        g.setColour (colour);
        float pen = 0.0f;
        for (auto c : text)
        {
            const auto* glyph = find (c);
            if (glyph == nullptr)
                continue;
            if (! glyph->path.isEmpty())
                g.strokePath (glyph->path, stroke,
                              juce::AffineTransform::translation (pen + offsetOf (c, tabular), 0.0f)
                                  .scaled (s)
                                  .translated (x, baseline - capHeight));
            pen += advanceOf (*glyph, c, tabular);
        }
        return x + juce::jmax (0.0f, pen - gap) * s;
    }

    void drawRight (juce::Graphics& g, const juce::String& text, float right, float baseline, float capHeight, juce::Colour colour,
                    bool tabular)
    {
        draw (g, text, right - width (text, capHeight, tabular), baseline, capHeight, colour, tabular);
    }

    juce::Font unitFont (float capHeight)
    {
        return HaloFonts::font (Role::uiLight, juce::jmax (10.5f, std::round (capHeight * 0.4f * 2.0f) / 2.0f));
    }

    namespace
    {
        float unitGap (float capHeight) { return juce::jmax (3.0f, capHeight * 0.14f); }
    }

    float widthWithUnit (const juce::String& value, const juce::String& unit, float capHeight)
    {
        const float w = width (value, capHeight);
        if (unit.isEmpty())
            return w;
        return w + unitGap (capHeight) + juce::GlyphArrangement::getStringWidth (unitFont (capHeight), unit);
    }

    float drawWithUnit (juce::Graphics& g, const juce::String& value, const juce::String& unit, float x, float baseline,
                        float capHeight, juce::Colour valueColour, juce::Colour unitColour)
    {
        float right = draw (g, value, x, baseline, capHeight, valueColour);
        if (unit.isEmpty())
            return right;
        const auto font = unitFont (capHeight);
        juce::GlyphArrangement ga;
        ga.addLineOfText (font, unit, right + unitGap (capHeight), baseline);
        g.setColour (unitColour);
        ga.draw (g);
        return right + unitGap (capHeight) + juce::GlyphArrangement::getStringWidth (font, unit);
    }

    void drawWithUnitRight (juce::Graphics& g, const juce::String& value, const juce::String& unit, float right, float baseline,
                            float capHeight, juce::Colour valueColour, juce::Colour unitColour)
    {
        drawWithUnit (g, value, unit, right - widthWithUnit (value, unit, capHeight), baseline, capHeight, valueColour, unitColour);
    }
}
