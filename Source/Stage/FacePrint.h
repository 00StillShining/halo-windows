#pragma once

#include <juce_graphics/juce_graphics.h>

namespace halo
{
    // The silkscreen printed on the EP-40's face plate: the sound-parameter names
    // beside each pad lamp (LEVEL, PITCH, TIME ...) and the LIVE bracket over
    // SOUND / MAIN. Printed ink is drawn into a texture the stage projects onto
    // the plate's top face, so it stays crisp at any zoom without text meshes
    // (DD-006f) and costs nothing per frame.
    //
    // Neutral typeface only, never TE's own lettering (DD-006e). Positions use
    // the Blender model's normalised face coordinates: u across the width (0 left),
    // v along the depth (0 near edge, 1 far edge). The image's top row is the far
    // edge. Colours are linear and premultiplied, ready to blend as albedo.
    struct FacePrint
    {
        static constexpr float widthMm = 176.0f, depthMm = 240.0f;
        static constexpr float pxPerMm = 10.0f;

        static juce::Image render()
        {
            const int w = juce::roundToInt (widthMm * pxPerMm), h = juce::roundToInt (depthMm * pxPerMm);
            juce::Image image (juce::Image::ARGB, w, h, true);
            juce::Graphics g (image);

            const auto ink = juce::Colour::fromFloatRGBA (0.030f, 0.050f, 0.042f, 0.92f);
            auto at = [] (float u, float v) { return juce::Point<float> (u * widthMm * pxPerMm, (1.0f - v) * depthMm * pxPerMm); };
            auto mm = [] (float millimetres) { return millimetres * pxPerMm; };

            juce::Font font (juce::FontOptions (mm (2.55f), juce::Font::bold));
            font.setExtraKerningFactor (0.10f);
            g.setFont (font);

            // One lamp above each numeric pad (status lamps sit mid-gap, at the
            // pad's centre); the parameter name reads to its right.
            constexpr float columns[] { 0.367f, 0.505f, 0.643f };
            constexpr float lampRows[] { 0.395f + 0.048f, 0.395f - 0.052f, 0.295f - 0.052f, 0.196f - 0.052f };
            const char* names[4][3] {
                { "LEVEL", "PITCH", "TIME" },
                { "LPF", "HPF", "\xe2\x86\x92" "FX" },
                { "ATK", "REL", "PAN" },
                { "TUNE", "VEL", "MOD" },
            };
            g.setColour (ink);
            for (int row = 0; row < 4; ++row)
                for (int col = 0; col < 3; ++col)
                {
                    const auto p = at (columns[col], lampRows[row]);
                    g.drawText (juce::String::fromUTF8 (names[row][col]),
                                juce::Rectangle<float> (p.x + mm (2.1f), p.y - mm (1.6f), mm (12.0f), mm (3.2f)),
                                juce::Justification::centredLeft, false);
                }

            // LIVE bracket spanning SOUND and MAIN, just under the display.
            {
                const float v = 0.5405f, left = 0.229f - 0.044f, right = 0.367f + 0.044f;
                const auto a = at (left, v), b = at (right, v);
                const float stroke = mm (0.32f), tick = mm (1.5f), gap = mm (5.6f);
                const float mid = (a.x + b.x) * 0.5f;
                g.fillRect (juce::Rectangle<float> (a.x, a.y - stroke * 0.5f, mid - gap - a.x, stroke));
                g.fillRect (juce::Rectangle<float> (mid + gap, a.y - stroke * 0.5f, b.x - mid - gap, stroke));
                g.fillRect (juce::Rectangle<float> (a.x, a.y - stroke * 0.5f, stroke, tick));
                g.fillRect (juce::Rectangle<float> (b.x - stroke, a.y - stroke * 0.5f, stroke, tick));
                g.drawText ("LIVE", juce::Rectangle<float> (mid - gap, a.y - mm (1.6f), gap * 2.0f, mm (3.2f)),
                            juce::Justification::centred, false);
            }
            return image;
        }
    };
}
