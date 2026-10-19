#include "UI/Design/Primitives/Gauges.h"
#include "UI/Design/Paint.h"

namespace halo
{
    namespace gauges
    {
        void tickDial (juce::Graphics& g, juce::Rectangle<float> r, double norm, juce::Colour line)
        {
            // A semicircle of ticks standing on its pivot, inside the rect.
            const auto& t = theme();
            const float radius = juce::jmin (r.getWidth() * 0.5f, r.getHeight() - 3.0f);
            const auto c = juce::Point<float> (r.getCentreX(), r.getY() + radius + 1.0f);
            constexpr int ticks = 21;
            const float a0 = -juce::MathConstants<float>::halfPi, a1 = juce::MathConstants<float>::halfPi;
            for (int i = 0; i < ticks; ++i)
            {
                const float f = (float) i / (float) (ticks - 1);
                const float a = a0 + (a1 - a0) * f;
                const bool major = i % 5 == 0;
                const auto p0 = c.getPointOnCircumference (radius * (major ? 0.60f : 0.72f), a);
                const auto p1 = c.getPointOnCircumference (radius, a);
                g.setColour (f <= (float) norm + 1.0e-4f ? line : t.scr.frame);
                g.drawLine ({ p0, p1 }, 1.2f);
            }
            const float a = a0 + (a1 - a0) * (float) juce::jlimit (0.0, 1.0, norm);
            g.setColour (line);
            g.drawLine ({ c, c.getPointOnCircumference (radius * 0.52f, a) }, 1.5f);
            g.fillEllipse (juce::Rectangle<float> (4.0f, 4.0f).withCentre (c));
        }

        void dotRow (juce::Graphics& g, juce::Rectangle<float> r, int count, int lit, juce::Colour line)
        {
            if (count <= 0)
                return;
            const float pitch = juce::jmin (9.0f, r.getWidth() / (float) count);
            const float d = juce::jmin (5.5f, pitch * 0.7f);
            const float x0 = r.getRight() - pitch * (float) count;
            for (int i = 0; i < count; ++i)
            {
                const auto dot = juce::Rectangle<float> (d, d).withCentre ({ x0 + pitch * ((float) i + 0.5f), r.getCentreY() });
                if (i < lit)
                {
                    g.setColour (line);
                    g.fillEllipse (dot);
                }
                else
                {
                    g.setColour (theme().scr.frame);
                    g.drawEllipse (dot.reduced (0.5f), 1.0f);
                }
            }
        }

        void ruler (juce::Graphics& g, juce::Rectangle<float> r, double norm, juce::Colour line, double limitNorm,
                    const juce::String& lowLabel, const juce::String& highLabel)
        {
            const auto& t = theme();
            const bool labels = lowLabel.isNotEmpty() || highLabel.isNotEmpty();
            auto bar = labels ? r.withTrimmedBottom (12.0f) : r;
            const float base = bar.getBottom() - 2.0f;
            constexpr int ticks = 39;
            for (int i = 0; i < ticks; ++i)
            {
                const float f = (float) i / (float) (ticks - 1);
                const float x = bar.getX() + bar.getWidth() * f;
                const bool major = i % 2 == 0;
                const float h = major ? bar.getHeight() * 0.55f : bar.getHeight() * 0.35f;
                g.setColour (f <= (float) norm + 1.0e-4f ? line : t.scr.frame);
                g.fillRect (juce::Rectangle<float> (x - 0.6f, base - h, 1.2f, h));
            }
            if (limitNorm >= 0.0)
            {
                const float x = bar.getX() + bar.getWidth() * (float) limitNorm;
                g.setColour (t.honest.danger);
                g.fillRect (juce::Rectangle<float> (x - 1.0f, bar.getY(), 2.0f, base - bar.getY()));
            }
            // Triangle pointer under the value.
            const float px = bar.getX() + bar.getWidth() * (float) juce::jlimit (0.0, 1.0, norm);
            juce::Path tri;
            tri.addTriangle (px - 4.0f, base + 5.0f, px + 4.0f, base + 5.0f, px, base + 0.5f);
            g.setColour (line);
            g.fillPath (tri);
            if (labels)
            {
                const auto f = HaloFonts::font (Role::print, 10.5f);
                paint::text (g, lowLabel, { r.getX(), r.getBottom() - 12.0f, 40.0f, 12.0f }, f, t.scr.dim);
                paint::text (g, highLabel, { r.getRight() - 40.0f, r.getBottom() - 12.0f, 40.0f, 12.0f }, f, t.scr.dim,
                             juce::Justification::centredRight);
            }
        }

        void timeline (juce::Graphics& g, juce::Rectangle<float> r, double norm, int ticks, juce::Colour line)
        {
            const auto& t = theme();
            const float y = r.getCentreY();
            paint::hairlineH (g, r.getX(), r.getRight(), y, t.scr.mute);
            for (int i = 0; i <= ticks; ++i)
            {
                const float x = r.getX() + r.getWidth() * (float) i / (float) juce::jmax (1, ticks);
                g.setColour (t.scr.mute);
                g.fillRect (juce::Rectangle<float> (x - 0.5f, y - 4.0f, 1.0f, 8.0f));
            }
            const float x = r.getX() + r.getWidth() * (float) juce::jlimit (0.0, 1.0, norm);
            g.setColour (line);
            g.fillEllipse (juce::Rectangle<float> (9.0f, 9.0f).withCentre ({ x, y }));
        }

        void ladder (juce::Graphics& g, juce::Rectangle<float> r, int segments, int lit, juce::Colour litColour, bool vertical,
                     juce::Colour unlit)
        {
            if (segments <= 0)
                return;
            const auto& t = theme();
            const auto off = unlit.isTransparent() ? t.scr.ledOff : unlit;
            const float pitch = (vertical ? r.getHeight() : r.getWidth()) / (float) segments;
            const float thick = juce::jmax (1.5f, juce::jmin (2.5f, pitch * 0.45f));
            for (int i = 0; i < segments; ++i)
            {
                g.setColour (i < lit ? litColour : off);
                if (vertical)
                    g.fillRect (juce::Rectangle<float> (r.getX(), r.getBottom() - pitch * ((float) i + 0.5f) - thick * 0.5f, r.getWidth(), thick));
                else
                    g.fillRect (juce::Rectangle<float> (r.getX() + pitch * ((float) i + 0.5f) - thick * 0.5f, r.getY(), thick, r.getHeight()));
            }
        }

        void tapeLane (juce::Graphics& g, juce::Rectangle<float> r, double history, double buffered, double window, juce::Colour wc)
        {
            const auto& t = theme();
            auto lane = r.withSizeKeepingCentre (r.getWidth(), 12.0f);
            g.setColour (t.scr.tapeEmpty);
            g.fillRoundedRectangle (lane, 2.0f);
            const auto xFor = [&] (double secondsAgo)
            {
                return lane.getRight() - lane.getWidth() * (float) juce::jlimit (0.0, 1.0, secondsAgo / juce::jmax (1.0, history));
            };
            g.setColour (t.scr.ramp[2]);
            g.fillRoundedRectangle (lane.withLeft (xFor (buffered)), 2.0f);
            if (window > 0.0)
            {
                const float x0 = xFor (window), x1 = lane.getRight();
                g.setColour (wc.withAlpha (0.55f));
                g.fillRect (lane.withLeft (x0).withRight (x1).reduced (0.0f, 3.0f));
                // bracket: outlined start circle, filled end circle
                g.setColour (wc);
                g.drawLine (x0, lane.getY() - 5.0f, x1, lane.getY() - 5.0f, 1.5f);
                g.drawEllipse (juce::Rectangle<float> (7.0f, 7.0f).withCentre ({ x0, lane.getY() - 5.0f }), 1.5f);
                g.fillEllipse (juce::Rectangle<float> (7.0f, 7.0f).withCentre ({ x1, lane.getY() - 5.0f }));
            }
            g.setColour (t.scr.hot);
            g.fillRect (juce::Rectangle<float> (lane.getRight() - 1.0f, lane.getY() - 2.0f, 2.0f, lane.getHeight() + 4.0f));
        }
    }

    //==========================================================================
    LevelArc::LevelArc (const juce::String& ch, const juce::String& tp) : channel (ch), tap (tp)
    {
        setOpaque (false);
        setInterceptsMouseClicks (false, false);
    }

    LevelArc::~LevelArc() = default;

    void LevelArc::setRunning (bool r)
    {
        running = r;
        if (! r)
            target = -100.0f;
        if (auto* clock = MotionClock::instance())
            clock->wake (this);
        repaint();
    }

    void LevelArc::setLevel (float peakDb, bool limitHit)
    {
        target = running ? peakDb : -100.0f;
        if (limitHit)
            limitHold = 1.5;
        if (auto* clock = MotionClock::instance())
            clock->wake (this);
        else
        {
            needle.value = target; // no clock (tests): snap
            repaint();
        }
    }

    bool LevelArc::tick (double dt)
    {
        const float before = needle.value;
        const bool moving = needle.step (target, dt);
        const bool lampWasLit = limitHold > 0.0;
        limitHold = juce::jmax (0.0, limitHold - dt);
        if (std::abs (needle.value - before) > 0.02f || lampWasLit != (limitHold > 0.0))
            repaint();
        return moving || limitHold > 0.0;
    }

    void LevelArc::paint (juce::Graphics& g)
    {
        const auto& t = theme();
        const auto r = getLocalBounds().toFloat();
        const float radius = juce::jmin (r.getWidth() * 0.5f - 6.0f, r.getHeight() - 30.0f);
        const auto pivot = juce::Point<float> (r.getCentreX(), r.getY() + radius + 14.0f);
        constexpr float sweep = 1.05f; // radians each side of vertical
        auto angleFor = [] (float db) { return -sweep + 2.0f * sweep * juce::jlimit (0.0f, 1.0f, (db + 40.0f) / 40.0f); };

        // Ghosted big channel letter.
        paint::text (g, channel, juce::Rectangle<float> (radius, radius).withCentre (pivot.translated (0.0f, -radius * 0.45f)),
                     HaloFonts::font (Role::uiLight, radius * 0.7f), t.scr.ink.withAlpha (0.07f), juce::Justification::centred);

        // Scale: ticks every 6 dB from 0, labels at 0 / 6 / 12 / 24 / 40.
        for (int db = 0; db >= -36; db -= 6)
        {
            const float a = angleFor ((float) db);
            const auto p0 = pivot.getPointOnCircumference (radius - (db % 12 == 0 ? 8.0f : 5.0f), a);
            const auto p1 = pivot.getPointOnCircumference (radius, a);
            g.setColour (t.scr.mute);
            g.drawLine ({ p0, p1 }, 1.2f);
        }
        const auto labelFont = HaloFonts::font (Role::print, 10.5f);
        for (int db : { 0, -6, -12, -24, -40 })
        {
            const auto p = pivot.getPointOnCircumference (radius + 10.0f, angleFor ((float) db));
            paint::text (g, juce::String (-db), juce::Rectangle<float> (24.0f, 12.0f).withCentre (p), labelFont, t.scr.dim,
                         juce::Justification::centred);
        }
        juce::Path scale;
        scale.addCentredArc (pivot.x, pivot.y, radius, radius, 0.0f, -sweep, angleFor (-1.0f), true);
        g.setColour (t.scr.mute);
        g.strokePath (scale, juce::PathStrokeType (1.0f));
        juce::Path limit;
        limit.addCentredArc (pivot.x, pivot.y, radius - 2.0f, radius - 2.0f, 0.0f, angleFor (-1.0f), sweep, true);
        g.setColour (t.honest.danger);
        g.strokePath (limit, juce::PathStrokeType (5.0f, juce::PathStrokeType::curved, juce::PathStrokeType::butt));

        // Needle (rests at the floor when the route is off).
        const float a = angleFor (needle.value);
        g.setColour (running ? t.scr.hot : t.scr.frame);
        g.drawLine ({ pivot, pivot.getPointOnCircumference (radius + 3.0f, a) }, 1.5f);
        g.fillEllipse (juce::Rectangle<float> (6.0f, 6.0f).withCentre (pivot));

        // Tap name under the pivot; limit lamp.
        paint::text (g, tap, { pivot.x - 30.0f, pivot.y + 6.0f, 60.0f, 13.0f }, labelFont, t.scr.dim, juce::Justification::centred);
        const auto lampC = juce::Point<float> (r.getX() + 8.0f, r.getY() + 8.0f);
        paint::lamp (g, lampC, limitHold > 0.0 ? t.honest.danger : t.scr.ledOff, 7.0f);
        paint::text (g, "limit", { lampC.x + 7.0f, lampC.y - 7.0f, 40.0f, 14.0f }, labelFont, t.scr.dim);
    }
}
