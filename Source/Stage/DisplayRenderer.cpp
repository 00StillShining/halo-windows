#include "Stage/DisplayRenderer.h"

#include "Stage/LcdSegments.h"

#include <HaloAssets.h>

namespace halo
{
    namespace
    {
        // Icon rectangles in the atlas, by LcdIcon.
        const juce::Rectangle<int> iconRects[] {
            { 204, 224, 113, 56 }, { 376, 224, 99, 56 }, { 539, 224, 113, 56 }, { 652, 224, 124, 56 }, { 761, 224, 114, 56 },  // mode words (below row 3's icons)
            { 490, 171, 46, 50 },   // play
            { 652, 58, 52, 52 },    // BPM
            { 39, 55, 52, 61 }, { 96, 55, 52, 61 }, { 151, 55, 52, 61 }, { 207, 55, 52, 61 },  // groups
            { 596, 164, 49, 59 },   // numpad
        };
        static_assert (std::size (iconRects) == (size_t) LcdIcon::count);

        juce::Path polygon (int first, int count)
        {
            juce::Path p;
            for (int i = 0; i < count; ++i)
            {
                const auto& q = lcd::segmentPoints[(size_t) (first + i)];
                if (i == 0) p.startNewSubPath (q.x, q.y);
                else p.lineTo (q.x, q.y);
            }
            p.closeSubPath();
            return p;
        }

        // A soft glow under lit segments, standing in for Core Graphics' shadow blur.
        void fillWithGlow (juce::Graphics& g, const juce::Path& path, juce::Colour fill, juce::Colour glow, float blur)
        {
            juce::DropShadow (glow, (int) blur, {}).drawForPath (g, path);
            g.setColour (fill);
            g.fillPath (path);
        }
    }

    DisplayRenderer::DisplayRenderer()
    {
        atlas = juce::ImageCache::getFromMemory (HaloAssets::halo_screen_indicators_png,
                                                 HaloAssets::halo_screen_indicators_pngSize);
        if (! atlas.isValid())
            return;

        // Remove the flat background green once, leaving icons that can glow
        // without rectangular crop edges (CIColorCube step on the Mac).
        indicators = juce::Image (juce::Image::ARGB, atlas.getWidth(), atlas.getHeight(), true);
        {
            const juce::Image::BitmapData src (atlas, juce::Image::BitmapData::readOnly);
            juce::Image::BitmapData dst (indicators, juce::Image::BitmapData::writeOnly);
            const float bg[] { 46.0f / 255.0f, 170.0f / 255.0f, 130.0f / 255.0f };
            for (int y = 0; y < atlas.getHeight(); ++y)
                for (int x = 0; x < atlas.getWidth(); ++x)
                {
                    const auto c = src.getPixelColour (x, y);
                    const float dr = c.getFloatRed() - bg[0], dg = c.getFloatGreen() - bg[1], db = c.getFloatBlue() - bg[2];
                    const float distance = std::sqrt (dr * dr + dg * dg + db * db);
                    const float alpha = juce::jlimit (0.0f, 1.0f, (distance - 0.035f) / 0.12f);
                    dst.setPixelColour (x, y, c.withAlpha (alpha * c.getFloatAlpha()));
                }
        }

        // Each icon's crisp crop and blurred halo, once.
        for (size_t i = 0; i < (size_t) LcdIcon::count; ++i)
        {
            const auto rect = iconRects[i];
            iconCrops[i] = indicators.getClippedImage (rect).createCopy();
            juce::Image halo (juce::Image::ARGB, rect.getWidth() + 8, rect.getHeight() + 8, true);
            {
                juce::Graphics hg (halo);
                hg.drawImageAt (iconCrops[i], 4, 4);
            }
            juce::ImageConvolutionKernel blur (5);
            blur.createGaussianBlur (2.0f);
            blur.applyToImage (halo, halo, halo.getBounds());
            iconHalos[i] = halo;
        }
    }

    juce::Image DisplayRenderer::render (const LcdState& lcd) const
    {
        juce::Image image (juce::Image::ARGB, width, height, true);
        juce::Graphics g (image);
        // The backlight: live 0.35 is the authored brightness; waiting and
        // preview are visibly dimmer (provenance on the device itself, D4).
        const float level = juce::jlimit (0.0f, 1.0f, lcd.backlight / LcdState::liveBacklight);
        g.fillAll (palette.background.withMultipliedBrightness (0.35f + 0.65f * level));

        if (atlas.isValid())
        {
            const auto& sheet = palette.authoredAtlas ? atlas : indicators;
            if (palette.tintIcons)
            {
                // Every icon as a flat shape in the off colour (alpha from the atlas).
                g.setColour (palette.iconOff.withMultipliedAlpha (0.6f + 0.4f * level));
                g.drawImageAt (sheet, 0, 0, true);
            }
            else
            {
                g.setOpacity (palette.atlasOpacity * (0.5f + 0.5f * level));
                g.drawImageAt (sheet, 0, 0);
            }
            g.setOpacity (1.0f);
            for (size_t i = 0; i < (size_t) LcdIcon::count; ++i)
            {
                if (! lcd.lit[i])
                    continue;
                const auto rect = iconRects[i];
                if (palette.tintIcons)
                {
                    g.setColour (palette.iconOn.withAlpha (0.45f * (0.6f + 0.4f * level)));
                    g.drawImageAt (iconHalos[i], rect.getX() - 4, rect.getY() - 4, true);
                    g.setColour (palette.iconOn.withAlpha (0.7f + 0.3f * level));
                    g.drawImageAt (iconCrops[i], rect.getX(), rect.getY(), true);
                }
                else
                {
                    g.setOpacity (0.55f * (0.6f + 0.4f * level));
                    g.drawImageAt (iconHalos[i], rect.getX() - 4, rect.getY() - 4);
                    g.setOpacity (0.7f + 0.3f * level);
                    g.drawImageAt (iconCrops[i], rect.getX(), rect.getY());
                }
                g.setOpacity (1.0f);
            }
        }

        drawDigits (g, lcd);
        drawMeters (g, lcd);
        return image;
    }

    void DisplayRenderer::drawDigits (juce::Graphics& g, const LcdState& lcd) const
    {
        const juce::Rectangle<float> panel (447, 64, 183, 93);
        g.setColour (palette.digitWell);
        g.fillRect (panel);

        const float brightness = 0.6f + 0.4f * juce::jlimit (0.0f, 1.0f, lcd.backlight / LcdState::liveBacklight);
        for (int d = 0; d < lcd::digitCount; ++d)
        {
            const auto mask = lcdGlyph (lcd.text[(size_t) d]);
            for (int s = 0; s < lcd::segmentCount; ++s)
            {
                const auto& r = lcd::segmentRanges[(size_t) (d * lcd::segmentCount + s)];
                const auto path = polygon (r[0], r[1]);
                if ((mask >> s) & 1)
                    fillWithGlow (g, path, palette.segmentOn.withAlpha (brightness), palette.glow, 5.0f);
                else
                {
                    g.setColour (palette.segmentOff);
                    g.fillPath (path);
                }
            }
        }
        g.setColour (palette.segmentOff);
        for (const auto& r : lcd::decimalPointRanges)
            g.fillPath (polygon (r[0], r[1]));
    }

    void DisplayRenderer::drawMeters (juce::Graphics& g, const LcdState& lcd) const
    {
        g.setColour (palette.digitWell);
        g.fillRect (989, 58, 35, 220);

        const float bankY[] { 63, 119, 174, 230 };
        for (int bank = 0; bank < 4; ++bank)
            for (int segment = 0; segment < 3; ++segment)
            {
                const juce::Rectangle<float> r (994.0f, bankY[bank] + (float) ((2 - segment) * 15), 30, 11);
                if (lcd.meters[(size_t) bank] >= (float) (segment + 1) / 3.0f)
                {
                    juce::Path p;
                    p.addRectangle (r);
                    fillWithGlow (g, p, palette.segmentOn.withAlpha (0.94f), palette.glow, 4.0f);
                }
                else
                {
                    g.setColour (palette.segmentOff);
                    g.fillRect (r);
                }
            }
    }
}
