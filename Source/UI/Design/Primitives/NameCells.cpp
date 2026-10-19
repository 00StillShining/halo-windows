#include "UI/Design/Primitives/NameCells.h"
#include "UI/Design/Paint.h"

namespace halo
{
    namespace
    {
        constexpr float gapRatio = 0.18f;  // gap between cells, relative to cell height
        constexpr float widthRatio = 0.86f; // cell width relative to height
    }

    NameCells::NameCells()
    {
        setWantsKeyboardFocus (true);
        setMouseCursor (juce::MouseCursor::IBeamCursor);
    }

    juce::String NameCells::sanitise (const juce::String& s)
    {
        return s.toUpperCase().retainCharacters ("ABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789").substring (0, maxLength);
    }

    int NameCells::widthForHeight (int height)
    {
        const float h = (float) height;
        return (int) std::ceil (h * widthRatio * (float) maxLength + h * gapRatio * (float) (maxLength - 1));
    }

    void NameCells::setText (const juce::String& s)
    {
        const auto clean = sanitise (s);
        if (clean == text)
            return;
        text = clean;
        repaint();
    }

    void NameCells::paint (juce::Graphics& g)
    {
        const auto& t = theme();
        const float scale = paint::physicalScale (g);
        const float h = (float) getHeight();
        const float cellW = std::floor (h * widthRatio), gap = std::floor (h * gapRatio);

        // n x 11 physical px per em: each design pixel is exactly n device pixels.
        const int n = scale >= 1.5f ? 3 : 2;
        const float em = (float) n * 11.0f / scale;
        const auto font = HaloFonts::face (HaloFonts::Face::departureMono, em);
        const float designPx = (float) n / scale;   // one design pixel, in logical units
        const float capH = 8.0f * designPx, advance = 7.0f * designPx;

        // Snap against the window, not this component: at 125% a component can
        // start on a fractional device pixel.
        const auto origin = getTopLevelComponent() != nullptr
                          ? getTopLevelComponent()->getLocalPoint (this, juce::Point<float>())
                          : juce::Point<float>();
        auto snapX = [&] (float v) { return std::round ((v + origin.x) * scale) / scale - origin.x; };
        auto snapY = [&] (float v) { return std::round ((v + origin.y) * scale) / scale - origin.y; };

        const bool focused = hasKeyboardFocus (false);
        for (int i = 0; i < maxLength; ++i)
        {
            const auto cell = juce::Rectangle<float> ((float) i * (cellW + gap), 0.0f, cellW, h);
            const bool isCursor = focused && i == juce::jmin (text.length(), maxLength - 1);
            g.setColour (t.scr.panel);
            g.fillRoundedRectangle (cell, 3.0f);
            g.setColour (isCursor ? t.scr.ink : t.scr.frame);
            g.drawRoundedRectangle (cell.reduced (isCursor ? 0.75f : 0.5f), 3.0f, isCursor ? 1.5f : 1.0f);

            if (i < text.length())
            {
                // The glyph box is 1 design pixel of side bearing + 5 of ink + 1;
                // centre the advance box, then snap its origin to device pixels.
                const float x = snapX (cell.getCentreX() - advance * 0.5f);
                const float baseline = snapY (cell.getCentreY() + capH * 0.5f);
                paint::textAt (g, text.substring (i, i + 1), x, baseline, font, t.scr.ink);
            }
        }
    }

    void NameCells::mouseDown (const juce::MouseEvent&)
    {
        grabKeyboardFocus();
    }

    bool NameCells::keyPressed (const juce::KeyPress& k)
    {
        if (k.isKeyCode (juce::KeyPress::backspaceKey))
        {
            if (text.isNotEmpty())
            {
                text = text.dropLastCharacters (1);
                repaint();
                if (onChange) onChange (text);
            }
            return true;
        }
        if (k.isKeyCode (juce::KeyPress::returnKey) || k.isKeyCode (juce::KeyPress::escapeKey))
        {
            giveAwayKeyboardFocus();
            return true;
        }
        const auto c = k.getTextCharacter();
        if (! k.getModifiers().isCtrlDown() && ! k.getModifiers().isAltDown() && juce::CharacterFunctions::isLetterOrDigit (c) && c < 128)
        {
            if (text.length() < maxLength)
            {
                text = sanitise (text + juce::String::charToString (c));
                repaint();
                if (onChange) onChange (text);
            }
            return true;
        }
        return false;
    }
}
