#include "Stage/DisplayRenderer.h"

#include <HaloAssets.h>

namespace halo
{
    namespace
    {
        const juce::Colour screenGreen  = juce::Colour::fromFloatRGBA (0.045f, 0.37f, 0.25f, 1.0f);
        const juce::Colour digitWell    = juce::Colour::fromFloatRGBA (0.025f, 0.20f, 0.13f, 0.96f);
        const juce::Colour segmentGhost = juce::Colour::fromFloatRGBA (0.20f, 0.39f, 0.30f, 0.72f);
        const juce::Colour glow         = juce::Colour::fromFloatRGBA (0.76f, 1.0f, 0.82f, 0.75f);

        juce::Colour segmentLit (float alpha) { return juce::Colour::fromFloatRGBA (0.90f, 0.98f, 0.91f, alpha); }

        const juce::Rectangle<int> modeRects[] {
            { 204, 210, 113, 70 }, { 376, 210, 99, 70 }, { 539, 210, 113, 70 },
            { 652, 210, 124, 70 }, { 761, 210, 114, 70 },
        };
        const juce::Rectangle<int> groupRects[] {
            { 39, 55, 52, 61 }, { 96, 55, 52, 61 }, { 151, 55, 52, 61 }, { 207, 55, 52, 61 },
        };

        // A soft glow under lit elements, standing in for Core Graphics' shadow blur.
        void fillWithGlow (juce::Graphics& g, const juce::Path& path, juce::Colour fill, float blur)
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

    juce::Image DisplayRenderer::render (const DisplayState& state) const
    {
        juce::Image image (juce::Image::ARGB, width, height, true);
        juce::Graphics g (image);
        g.fillAll (screenGreen);

        if (atlas.isValid())
        {
            g.setOpacity (state.feedMode == FeedMode::waiting ? 0.16f : 0.27f);
            g.drawImageAt (atlas, 0, 0);
            g.setOpacity (1.0f);

            highlight (g, modeRects[(int) state.mode]);
            if (state.isPlaying)
                highlight (g, { 485, 165, 48, 56 });
            if (state.clockPulse)
                highlight (g, { 650, 56, 69, 66 });
            if (state.activeGroup)
                highlight (g, groupRects[*state.activeGroup]);
            if (state.activePadIndex)
                highlight (g, { 596, 164, 49, 59 });
        }

        drawDigits (g, state.value, state.feedMode == FeedMode::waiting ? 0.64f : 1.0f);
        drawPadActivity (g, state.activePadIndex, state.velocity);
        drawMeters (g, state.leftMeter, state.rightMeter);
        drawFeedBeacon (g, state.feedMode, state.clockPulse);
        return image;
    }

    void DisplayRenderer::highlight (juce::Graphics& g, juce::Rectangle<int> rect) const
    {
        const auto icons = indicators.isValid() ? indicators : atlas;
        juce::Graphics::ScopedSaveState save (g);
        g.reduceClipRegion (rect.expanded (4));

        // A blurred bright copy behind, then the crisp icon on top.
        const auto crop = icons.getClippedImage (rect);
        juce::Image halo (juce::Image::ARGB, rect.getWidth() + 8, rect.getHeight() + 8, true);
        {
            juce::Graphics hg (halo);
            hg.drawImageAt (crop, 4, 4);
        }
        juce::ImageConvolutionKernel blur (5);
        blur.createGaussianBlur (2.0f);
        blur.applyToImage (halo, halo, halo.getBounds());
        g.setOpacity (0.55f);
        g.drawImageAt (halo, rect.getX() - 4, rect.getY() - 4);
        g.setOpacity (1.0f);
        g.drawImageAt (crop, rect.getX(), rect.getY());
    }

    void DisplayRenderer::drawDigits (juce::Graphics& g, int value, float brightness) const
    {
        const juce::Rectangle<float> panel (447, 64, 183, 93);
        g.setColour (digitWell);
        g.fillRect (panel);
        g.setColour (segmentGhost);
        g.drawRect (panel.reduced (1), 2.0f);

        static constexpr std::array<std::uint8_t, 10> masks {
            0b0111111, 0b0000110, 0b1011011, 0b1001111, 0b1100110,
            0b1101101, 0b1111101, 0b0000111, 0b1111111, 0b1101111,
        };
        const int digits[] { value / 100, (value / 10) % 10, value % 10 };

        for (int index = 0; index < 3; ++index)
        {
            const float ox = 457.0f + (float) index * 55.0f, oy = 72.0f;
            // Segment order: top, upper-right, lower-right, bottom, lower-left, upper-left, middle.
            const juce::Rectangle<float> segments[] {
                { ox + 7, oy, 30, 7 },       { ox + 37, oy + 6, 7, 29 }, { ox + 37, oy + 39, 7, 29 },
                { ox + 7, oy + 67, 30, 7 },  { ox, oy + 39, 7, 29 },     { ox, oy + 6, 7, 29 },
                { ox + 7, oy + 33, 30, 7 },
            };
            const auto mask = masks[(std::size_t) juce::jlimit (0, 9, digits[index])];
            for (int s = 0; s < 7; ++s)
            {
                juce::Path p;
                p.addRoundedRectangle (segments[s], 2.0f);
                if ((mask >> s) & 1)
                    fillWithGlow (g, p, segmentLit (brightness), 5.0f);
                else
                {
                    g.setColour (segmentGhost);
                    g.fillPath (p);
                }
            }
        }
    }

    void DisplayRenderer::drawPadActivity (juce::Graphics& g, std::optional<int> pad, float velocity) const
    {
        for (int index = 0; index < 12; ++index)
        {
            const juce::Rectangle<float> r (601.0f + (float) (index % 3) * 11.0f, 168.0f + (float) (index / 3) * 12.0f, 8, 9);
            if (pad && *pad == index)
            {
                juce::Path p;
                p.addRectangle (r);
                fillWithGlow (g, p, segmentLit (0.65f + velocity * 0.35f), 6.0f);
            }
            else
            {
                g.setColour (segmentGhost);
                g.fillRect (r);
            }
        }
    }

    void DisplayRenderer::drawMeters (juce::Graphics& g, float left, float right) const
    {
        g.setColour (digitWell);
        g.fillRect (989, 58, 35, 220);

        const float levels[] { left, right, left * 0.78f, right * 0.78f };
        const float bankY[] { 63, 119, 174, 230 };
        for (int bank = 0; bank < 4; ++bank)
            for (int segment = 0; segment < 3; ++segment)
            {
                const juce::Rectangle<float> r (994.0f, bankY[bank] + (float) ((2 - segment) * 15), 30, 11);
                if (levels[bank] >= (float) (segment + 1) / 3.0f)
                {
                    juce::Path p;
                    p.addRectangle (r);
                    fillWithGlow (g, p, segmentLit (0.94f), 4.0f);
                }
                else
                {
                    g.setColour (segmentGhost);
                    g.fillRect (r);
                }
            }
    }

    void DisplayRenderer::drawFeedBeacon (juce::Graphics& g, FeedMode mode, bool pulse) const
    {
        // Halo's provenance mark: one dot = preview, two = waiting, three = live.
        const int count = mode == FeedMode::preview ? 1 : mode == FeedMode::waiting ? 2 : 3;
        for (int index = 0; index < 3; ++index)
        {
            g.setColour (index < count ? segmentLit (pulse ? 1.0f : 0.76f) : segmentGhost);
            g.fillEllipse (878.0f + (float) index * 10.0f, 274.0f, 6, 6);
        }
    }
}
