#include "UI/Design/Paint.h"

namespace halo::paint
{
    float physicalScale (const juce::Graphics& g)
    {
        return juce::jmax (0.5f, g.getInternalContext().getPhysicalPixelScaleFactor());
    }

    float snap (float v, float scale, bool centre)
    {
        const float p = std::floor (v * scale);
        return (centre ? p + 0.5f : p) / scale;
    }

    void hairlineH (juce::Graphics& g, float x0, float x1, float y, juce::Colour c)
    {
        const float s = physicalScale (g);
        const float top = std::floor (y * s) / s;
        g.setColour (c);
        g.fillRect (juce::Rectangle<float>::leftTopRightBottom (snap (x0, s), top, snap (x1, s), top + 1.0f / s));
    }

    void hairlineV (juce::Graphics& g, float x, float y0, float y1, juce::Colour c)
    {
        const float s = physicalScale (g);
        const float left = std::floor (x * s) / s;
        g.setColour (c);
        g.fillRect (juce::Rectangle<float>::leftTopRightBottom (left, snap (y0, s), left + 1.0f / s, snap (y1, s)));
    }

    // ---- Text ------------------------------------------------------------------
    float textWidth (const juce::Font& f, const juce::String& s)
    {
        return juce::GlyphArrangement::getStringWidth (f, s);
    }

    void text (juce::Graphics& g, const juce::String& s, juce::Rectangle<float> r, const juce::Font& f, juce::Colour c,
               juce::Justification j)
    {
        g.setFont (f);
        g.setColour (c);
        g.drawText (s, r, j, false);
    }

    float textAt (juce::Graphics& g, const juce::String& s, float x, float baseline, const juce::Font& f, juce::Colour c)
    {
        juce::GlyphArrangement ga;
        ga.addLineOfText (f, s, x, baseline);
        g.setColour (c);
        ga.draw (g);
        return x + textWidth (f, s);
    }

    juce::String middleEllipsis (const juce::String& s, const juce::Font& f, float maxWidth)
    {
        if (textWidth (f, s) <= maxWidth || s.length() < 4)
            return s;
        const juce::String dots = juce::String::charToString ((juce::juce_wchar) 0x2026);
        for (int keep = s.length() - 1; keep > 2; --keep)
        {
            const int head = (keep + 1) / 2, tail = keep / 2;
            const auto candidate = s.substring (0, head).trimEnd() + dots + s.substring (s.length() - tail).trimStart();
            if (textWidth (f, candidate) <= maxWidth)
                return candidate;
        }
        return s.substring (0, 1) + dots;
    }

    namespace
    {
        juce::TextLayout layoutFor (const juce::String& s, const juce::Font& f, juce::Colour c, float width,
                                    float lineSpacing, juce::Justification j)
        {
            juce::AttributedString a;
            a.setJustification (j);
            a.setLineSpacing (lineSpacing);
            a.setWordWrap (juce::AttributedString::byWord);
            a.append (s, f, c);
            juce::TextLayout layout;
            layout.createLayout (a, juce::jmax (1.0f, width));
            return layout;
        }
    }

    float paragraphHeight (const juce::String& s, const juce::Font& f, float width, float lineSpacing)
    {
        return std::ceil (layoutFor (s, f, juce::Colours::white, width, lineSpacing, juce::Justification::topLeft).getHeight());
    }

    void paragraph (juce::Graphics& g, const juce::String& s, juce::Rectangle<float> r, const juce::Font& f, juce::Colour c,
                    float lineSpacing, juce::Justification j)
    {
        layoutFor (s, f, c, r.getWidth(), lineSpacing, j).draw (g, r);
    }

    // ---- Lamps -----------------------------------------------------------------
    void lamp (juce::Graphics& g, juce::Point<float> c, juce::Colour colour, float d)
    {
        g.setColour (colour);
        g.fillEllipse (juce::Rectangle<float> (d, d).withCentre (c));
    }

    void hollowLamp (juce::Graphics& g, juce::Point<float> c, juce::Colour colour, float d, float stroke)
    {
        g.setColour (colour);
        g.drawEllipse (juce::Rectangle<float> (d - stroke, d - stroke).withCentre (c), stroke);
    }

    void deckLed (juce::Graphics& g, juce::Point<float> c, juce::Colour colour, float d)
    {
        g.setColour (colour.withAlpha (0.35f));
        g.fillEllipse (juce::Rectangle<float> (d * 2.2f, d * 2.2f).withCentre (c));
        g.setColour (colour);
        g.fillEllipse (juce::Rectangle<float> (d, d).withCentre (c));
    }

    // ---- Tags ------------------------------------------------------------------
    namespace
    {
        constexpr float tagPad = 8.0f, stubWidth = 10.0f, stubGap = 2.0f, lampRoom = 13.0f;
    }

    float tagWidth (const juce::String& s, TagStyle style)
    {
        float w = std::ceil (textWidth (HaloFonts::font (Role::tag), s)) + 2.0f * tagPad;
        if (style == TagStyle::mock)
            w += stubWidth + stubGap;
        if (style == TagStyle::preview)
            w += lampRoom;
        return w;
    }

    juce::Rectangle<float> tag (juce::Graphics& g, juce::Point<float> topLeft, const juce::String& s, TagStyle style,
                                juce::Colour fill, juce::Colour textColour, float height)
    {
        const auto& t = theme();
        const auto font = HaloFonts::font (Role::tag);
        const float w = tagWidth (s, style);
        auto r = juce::Rectangle<float> (topLeft.x, topLeft.y, w, height);
        auto body = r;
        constexpr float radius = 2.5f;

        switch (style)
        {
            case TagStyle::mock:
            {
                hazard (g, body.removeFromLeft (stubWidth), 7.0f);
                body.removeFromLeft (stubGap);
                g.setColour (t.honest.mock);
                g.fillRoundedRectangle (body, radius);
                text (g, s, body, font, juce::Colour (0xff000005), juce::Justification::centred);
                break;
            }
            case TagStyle::preview:
            {
                g.setColour (t.honest.preview);
                g.fillRoundedRectangle (body, radius);
                auto lampArea = body.removeFromLeft (lampRoom + 2.0f);
                hollowLamp (g, { lampArea.getRight() - 6.0f, body.getCentreY() }, juce::Colour (0xff000005), 7.0f, 1.4f);
                text (g, s, body.withTrimmedRight (2.0f), font, juce::Colour (0xff000005), juce::Justification::centred);
                break;
            }
            case TagStyle::danger:
                g.setColour (t.honest.danger);
                g.fillRoundedRectangle (body, radius);
                text (g, s, body, font, juce::Colours::white, juce::Justification::centred);
                break;
            case TagStyle::outline:
            {
                const auto edge = fill.isTransparent() ? t.scr.dim : fill;
                g.setColour (edge);
                g.drawRoundedRectangle (body.reduced (0.6f), radius, 1.2f);
                text (g, s, body, font, textColour.isTransparent() ? edge : textColour, juce::Justification::centred);
                break;
            }
            case TagStyle::hatched:
            {
                const auto edge = fill.isTransparent() ? t.scr.dim : fill;
                {
                    juce::Graphics::ScopedSaveState save (g);
                    juce::Path clip;
                    clip.addRoundedRectangle (body, radius);
                    g.reduceClipRegion (clip);
                    hatch (g, body, t.scr.hatch.withAlpha (0.55f));
                }
                g.setColour (edge);
                g.drawRoundedRectangle (body.reduced (0.6f), radius, 1.2f);
                text (g, s, body, font, textColour.isTransparent() ? t.scr.ink : textColour, juce::Justification::centred);
                break;
            }
            case TagStyle::filled:
            default:
                g.setColour (fill.isTransparent() ? t.scr.ink : fill);
                g.fillRoundedRectangle (body, radius);
                text (g, s, body, font, textColour.isTransparent() ? juce::Colour (0xff000005) : textColour,
                      juce::Justification::centred);
                break;
        }
        return r;
    }

    // ---- Textures --------------------------------------------------------------
    void hatch (juce::Graphics& g, juce::Rectangle<float> r, juce::Colour c, float pitch, float thickness)
    {
        if (r.isEmpty())
            return;
        juce::Graphics::ScopedSaveState save (g);
        juce::Path clip;
        clip.addRectangle (r);
        g.reduceClipRegion (clip);
        g.setColour (c);
        // Anchored to absolute coordinates, so neighbouring hatches line up.
        const float h = r.getHeight();
        // Lines satisfy x + y = k * pitch.
        const float start = std::floor ((r.getX() - h + r.getBottom()) / pitch) * pitch - r.getBottom();
        juce::Path lines;
        for (float x = start; x < r.getRight() + pitch; x += pitch)
        {
            lines.startNewSubPath (x, r.getBottom());
            lines.lineTo (x + h, r.getY());
        }
        g.strokePath (lines, juce::PathStrokeType (thickness));
    }

    void hazard (juce::Graphics& g, juce::Rectangle<float> r, float pitch)
    {
        if (r.isEmpty())
            return;
        juce::Graphics::ScopedSaveState save (g);
        juce::Path clip;
        clip.addRectangle (r);
        g.reduceClipRegion (clip);
        g.setColour (juce::Colour (0xff000005));
        g.fillRect (r);
        g.setColour (theme().honest.mock);
        const float h = r.getHeight();
        const float half = pitch * 0.5f;
        juce::Path stripes;
        const float start = std::floor ((r.getX() - h + r.getBottom()) / pitch) * pitch - r.getBottom();
        for (float x = start; x < r.getRight() + pitch; x += pitch)
        {
            stripes.startNewSubPath (x, r.getBottom());
            stripes.lineTo (x + half, r.getBottom());
            stripes.lineTo (x + half + h, r.getY());
            stripes.lineTo (x + h, r.getY());
            stripes.closeSubPath();
        }
        g.fillPath (stripes);
    }

    void dashedRect (juce::Graphics& g, juce::Rectangle<float> r, juce::Colour c, float thickness, float on, float off, float radius)
    {
        juce::Path outline;
        if (radius > 0.0f)
            outline.addRoundedRectangle (r, radius);
        else
            outline.addRectangle (r);
        juce::Path dashed;
        const float pattern[] { on, off };
        juce::PathStrokeType (thickness).createDashedStroke (dashed, outline, pattern, 2);
        g.setColour (c);
        g.fillPath (dashed);
    }

    void hardShadow (juce::Graphics& g, juce::Rectangle<float> r, float radius, juce::Colour c)
    {
        g.setColour (c);
        g.fillRoundedRectangle (r.translated (3.0f, 3.0f), radius);
    }

    // ---- Dot timeline ----------------------------------------------------------
    void dotTimeline (juce::Graphics& g, juce::Rectangle<float> r, int total, int used, int current, bool dangerFree, float d)
    {
        if (total <= 0 || r.isEmpty())
            return;
        const auto& t = theme();
        const float pitch = r.getWidth() / (float) total;
        d = juce::jmin (d, pitch * 0.72f);
        const float cy = r.getCentreY();
        juce::RectangleList<float> filled;
        juce::Path hollow;
        for (int i = 0; i < total; ++i)
        {
            const auto dot = juce::Rectangle<float> (d, d).withCentre ({ r.getX() + pitch * ((float) i + 0.5f), cy });
            if (i < used)
                filled.add (dot);
            else if (i >= used + current)
                hollow.addEllipse (dot.reduced (0.5f));
        }
        g.setColour (t.scr.ramp[3]);
        for (const auto& dot : filled)
            g.fillEllipse (dot);
        g.setColour (t.scr.hot);
        for (int i = used; i < juce::jmin (total, used + current); ++i)
            g.fillEllipse (juce::Rectangle<float> (d, d).withCentre ({ r.getX() + pitch * ((float) i + 0.5f), cy }));
        g.setColour (dangerFree ? t.honest.danger : t.scr.ramp[3]);
        g.strokePath (hollow, juce::PathStrokeType (1.0f));
    }

    // ---- Keys ------------------------------------------------------------------
    void stepKey (juce::Graphics& g, juce::Rectangle<float> r, const juce::String& s, KeyState state, bool alternate,
                  bool hovered, bool focused)
    {
        const auto& t = theme();
        constexpr float radius = 3.0f;
        const auto font = HaloFonts::face (HaloFonts::Face::interMedium, 12.5f);
        if (state == KeyState::disabled)
        {
            g.setColour (t.scr.frame);
            g.drawRoundedRectangle (r.reduced (0.6f), radius, 1.2f);
            text (g, s, r, font, t.scr.ramp[5], juce::Justification::centred);
        }
        else
        {
            // 2 px black lip under the cap.
            g.setColour (juce::Colours::black);
            g.fillRoundedRectangle (r, radius);
            const auto cap = r.withTrimmedBottom (2.0f);
            auto fill = state == KeyState::selected ? t.scr.ramp[7] : (alternate ? t.scr.stepB : t.scr.stepA);
            if (hovered && state != KeyState::selected)
                fill = fill.brighter (0.25f);
            g.setColour (fill);
            g.fillRoundedRectangle (cap, radius);
            text (g, s, cap, font, state == KeyState::selected ? juce::Colour (0xff000005) : t.scr.ink, juce::Justification::centred);
        }
        if (focused)
        {
            g.setColour (t.scr.hot);
            g.drawRoundedRectangle (r.expanded (1.5f), radius + 1.5f, 1.5f);
        }
    }

    void pill (juce::Graphics& g, juce::Rectangle<float> r, const juce::String& s, int encoder, bool available,
               bool hovered, bool pressed)
    {
        const auto& t = theme();
        const auto& e = t.encoder (encoder);
        const float radius = r.getHeight() * 0.5f;
        const auto font = HaloFonts::face (HaloFonts::Face::interMedium, 11.5f);
        if (available)
        {
            auto fill = e.fill;
            if (pressed)
                fill = fill.darker (0.25f);
            else if (hovered)
                fill = fill.brighter (0.12f);
            g.setColour (fill);
            g.fillRoundedRectangle (r, radius);
            text (g, s, r, font, e.onFill, juce::Justification::centred);
        }
        else
        {
            g.setColour (t.scr.frame);
            g.drawRoundedRectangle (r.reduced (0.6f), radius, 1.2f);
            text (g, s, r, font, t.scr.dim, juce::Justification::centred);
        }
    }
}
