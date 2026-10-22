#pragma once

#include <juce_graphics/juce_graphics.h>

namespace halo
{
    // The silkscreen printed on the EP-40's deck: the parameter names beside the
    // LEDs (LEVEL, PITCH, TIME ...), VOLUME, the LIVE and BPM / METRONOME
    // brackets, GAIN and SWING, and the big X and Y. Ink is drawn into a texture
    // the stage projects onto the plate's top face, so it stays crisp at any
    // zoom without text meshes and costs nothing per frame. Until the face SDF
    // atlas (plan M7) replaces it, a neutral system sans stands in for TE's face.
    //
    // Positions are TE's reference millimetres (docs/redesign/riddim-3d-plan.md
    // section 2.6): x from the left edge, y from the port edge toward the player,
    // which is also the image's row order (the top row is the port edge).
    // Colours are linear and premultiplied, ready to blend as albedo.
    struct FacePrint
    {
        static constexpr float widthMm = 176.0f, depthMm = 240.0f;
        static constexpr float pxPerMm = 10.0f;

        static juce::Image render()
        {
            const int w = juce::roundToInt (widthMm * pxPerMm), h = juce::roundToInt (depthMm * pxPerMm);
            juce::Image image (juce::Image::ARGB, w, h, true);
            juce::Graphics g (image);
            g.addTransform (juce::AffineTransform::scale (pxPerMm));

            // Linear albedo of TE's inks (plan section 2.5).
            const auto green = juce::Colour::fromFloatRGBA (0.0f, 0.0802f, 0.0423f, 1.0f);   // #00503A
            const auto orange = juce::Colour::fromFloatRGBA (1.0f, 0.1384f, 0.0123f, 1.0f);  // #FF681D

            const auto small = font (2.1f, false);
            constexpr float stroke = 0.28f;

            auto text = [&] (const juce::String& s, float x, float y, juce::Justification j, juce::Colour c, const juce::Font& f)
            {
                g.setColour (c);
                g.setFont (f);
                const float width = 40.0f, height = 6.0f;
                float left = x;
                if (j.testFlags (juce::Justification::horizontallyCentred)) left = x - width * 0.5f;
                if (j.testFlags (juce::Justification::right)) left = x - width;
                g.drawText (s, juce::Rectangle<float> (left, y - height * 0.5f, width, height), j, false);
            };
            auto line = [&] (float x0, float y0, float x1, float y1, juce::Colour c)
            {
                g.setColour (c);
                g.fillRect (juce::Rectangle<float> (juce::Point<float> (std::min (x0, x1) - stroke * 0.5f, std::min (y0, y1) - stroke * 0.5f),
                                                    juce::Point<float> (std::max (x0, x1) + stroke * 0.5f, std::max (y0, y1) + stroke * 0.5f)));
            };
            const auto left = juce::Justification::centredLeft, centred = juce::Justification::centred;

            // Parameter names, each just right of its LED (LEDs at x 59.9 / 84.0 / 108.0).
            constexpr float ledX[] { 59.9f, 84.0f, 108.0f };
            constexpr float ledY[] { 132.0f, 156.0f, 179.9f, 204.1f };
            const char* names[4][3] {
                { "LEVEL", "PITCH", "TIME" },
                { "LPF", "HPF", "\xe2\x86\x92" "FX" },
                { "ATK", "REL", "PAN" },
                { "TUNE", "VEL", "MOD" },
            };
            for (int row = 0; row < 4; ++row)
                for (int col = 0; col < 3; ++col)
                    text (juce::String::fromUTF8 (names[row][col]), ledX[col] + 2.4f, ledY[row], left, green, small);

            // Bracket row at y 108, just under the display.
            constexpr float rowY = 108.0f, tick = 2.6f; // just in front of the step (y 104.4)
            text ("VOLUME", 16.2f, rowY, centred, green, small);

            // LIVE over SOUND and MAIN.
            line (40.0f, rowY, 48.6f, rowY, green);
            line (56.0f, rowY, 64.4f, rowY, green);
            line (40.0f, rowY, 40.0f, rowY + tick, green);
            line (64.4f, rowY, 64.4f, rowY + tick, green);
            text ("LIVE", 52.3f, rowY, centred, green, small);

            // TEMPO -> BPM -> METRONOME.
            line (88.0f, rowY, 88.0f, rowY + tick, green);
            line (88.0f, rowY, 131.8f, rowY, green);
            text ("BPM", 136.4f, rowY, centred, green, small);
            line (141.0f, rowY, 146.8f, rowY, green);
            text ("METRONOME", 160.2f, rowY, centred, green, small);

            // GAIN: orange, reading upward, bracketing the X knob and SAMPLE.
            {
                constexpr float x = 124.2f, top = 114.6f, bottom = 141.5f, mid = 129.0f, gap = 4.3f;
                line (x, top, x, mid - gap, orange);
                line (x, mid + gap, x, bottom, orange);
                line (x, top, x + 2.0f, top, orange);
                line (x, bottom, x + 2.0f, bottom, orange);
                juce::Graphics::ScopedSaveState save (g);
                g.addTransform (juce::AffineTransform::rotation (-juce::MathConstants<float>::halfPi, x, mid));
                text ("GAIN", x, mid, centred, orange, small);
            }
            // SWING: green, reading downward, bracketing the Y knob and TIMING.
            {
                constexpr float x = 172.3f, top = 114.6f, bottom = 141.5f, mid = 128.0f, gap = 5.0f;
                line (x, top, x, mid - gap, green);
                line (x, mid + gap, x, bottom, green);
                line (x, top, x - 2.0f, top, green);
                line (x, bottom, x - 2.0f, bottom, green);
                juce::Graphics::ScopedSaveState save (g);
                g.addTransform (juce::AffineTransform::rotation (juce::MathConstants<float>::halfPi, x, mid));
                text ("SWING", x, mid, centred, green, small);
            }

            // The big X and Y under the knobs.
            const auto big = font (5.3f, true);
            text ("X", 135.6f, 131.6f, centred, orange, big);
            text ("Y", 159.6f, 131.6f, centred, green, big);
            return image;
        }

    private:
        // A widely spaced sans whose capitals are `capMm` tall.
        static juce::Font font (float capMm, bool bold)
        {
            // Measure the ink of an "H" (not the glyph cell) to size by cap height.
            juce::Font f (juce::FontOptions ("Segoe UI", 100.0f, bold ? juce::Font::bold : juce::Font::plain));
            juce::GlyphArrangement glyphs;
            glyphs.addLineOfText (f, "H", 0.0f, 0.0f);
            juce::Path ink;
            glyphs.createPath (ink);
            const float cap = ink.getBounds().getHeight();
            f = f.withHeight (cap > 0.0f ? 100.0f * capMm / cap : capMm * 1.4f);
            return f.withExtraKerningFactor (bold ? 0.0f : 0.12f);
        }
    };
}
