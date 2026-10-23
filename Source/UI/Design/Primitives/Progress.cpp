#include "UI/Design/Primitives/Progress.h"
#include "UI/Design/Numerals.h"
#include "UI/Design/Paint.h"

namespace halo::progress
{
    void reel (juce::Graphics& g, juce::Point<float> c, float r, double angle, float wound, juce::Colour line)
    {
        const auto& t = theme();
        // The wound tape as a disc that grows with the take-up.
        g.setColour (t.scr.tapeWound);
        const float wr = r * (0.38f + 0.58f * juce::jlimit (0.0f, 1.0f, wound));
        g.fillEllipse (juce::Rectangle<float> (wr * 2.0f, wr * 2.0f).withCentre (c));
        g.setColour (line);
        g.drawEllipse (juce::Rectangle<float> (r * 2.0f, r * 2.0f).withCentre (c), 1.5f);
        g.drawEllipse (juce::Rectangle<float> (r * 0.6f, r * 0.6f).withCentre (c), 1.5f);
        for (int k = 0; k < 3; ++k)
        {
            const float a = (float) angle + juce::MathConstants<float>::twoPi * (float) k / 3.0f;
            g.drawLine ({ c.getPointOnCircumference (r * 0.3f, a), c.getPointOnCircumference (r * 0.86f, a) }, 1.5f);
        }
    }

    void paint (juce::Graphics& g, juce::Rectangle<float> r, const State& s)
    {
        const auto& t = theme();
        const auto top = r.removeFromTop (20.0f);
        paint::tag (g, top.getTopLeft(), s.title, paint::TagStyle::filled, t.scr.ink, juce::Colour (0xff000005));

        // Reels at the right, turning with the transfer.
        auto reels = r.removeFromRight (juce::jmin (120.0f, r.getHeight() * 2.4f));
        const float rr = juce::jmin (reels.getHeight() * 0.42f, reels.getWidth() * 0.22f);
        const float f = (float) juce::jmax (0.0, s.fraction);
        reel (g, { reels.getX() + rr + 4.0f, reels.getCentreY() }, rr, s.reelAngle, 1.0f - f, t.scr.ink);
        reel (g, { reels.getRight() - rr - 4.0f, reels.getCentreY() }, rr, s.reelAngle, f, t.scr.ink);

        r.removeFromTop (6.0f);
        const float cap = juce::jmin (numerals::L, r.getHeight() - 22.0f);
        if (s.fraction >= 0.0)
            numerals::drawWithUnit (g, juce::String (juce::roundToInt (s.fraction * 100.0)), "%", r.getX(), r.getY() + cap, cap, t.scr.ink, t.scr.dim);
        paint::text (g, s.eta, { r.getX() + 100.0f, r.getY(), r.getWidth() - 100.0f, cap }, HaloFonts::font (Role::uiLight), t.scr.dim,
                     juce::Justification::bottomLeft);
        const auto dots = juce::Rectangle<float> (r.getX(), r.getY() + cap + 10.0f, r.getWidth() - 8.0f, 8.0f);
        const int total = 40;
        paint::dotTimeline (g, dots, total, 0, s.fraction >= 0.0 ? juce::roundToInt (s.fraction * total) : 0);
    }

    void cassette (juce::Graphics& g, juce::Rectangle<float> r, juce::Colour line, const juce::String& title, double hubAngle)
    {
        const float w = r.getWidth(), h = r.getHeight();
        const float stroke = juce::jmax (1.2f, w / 260.0f);
        g.setColour (line);
        g.drawRoundedRectangle (r.reduced (stroke * 0.5f), h * 0.05f, stroke);

        // Screws in the corners.
        for (auto c : { r.getTopLeft().translated (w * 0.04f, h * 0.06f), r.getTopRight().translated (-w * 0.04f, h * 0.06f),
                        r.getBottomLeft().translated (w * 0.04f, -h * 0.06f), r.getBottomRight().translated (-w * 0.04f, -h * 0.06f) })
        {
            const float sr = h * 0.022f;
            g.drawEllipse (juce::Rectangle<float> (sr * 2.0f, sr * 2.0f).withCentre (c), stroke * 0.7f);
            g.drawLine (c.x - sr * 0.6f, c.y, c.x + sr * 0.6f, c.y, stroke * 0.6f);
        }

        // The label: a rounded panel with ruled lines, a title and side A.
        auto label = juce::Rectangle<float> (r.getX() + w * 0.09f, r.getY() + h * 0.1f, w * 0.82f, h * 0.6f);
        g.drawRoundedRectangle (label, h * 0.03f, stroke * 0.8f);
        auto side = label.withWidth (h * 0.12f).withHeight (h * 0.12f).translated (h * 0.03f, h * 0.03f);
        g.drawRect (side, stroke * 0.7f);
        paint::text (g, "A", side, HaloFonts::font (Role::label, juce::jmax (9.0f, h * 0.07f)), line, juce::Justification::centred);
        paint::text (g, title, { side.getRight() + h * 0.04f, side.getY(), label.getWidth() * 0.7f, side.getHeight() },
                     HaloFonts::font (Role::label, juce::jmax (9.0f, h * 0.055f)), line, juce::Justification::centredLeft);
        g.setColour (line.withAlpha (0.4f));
        for (int i = 0; i < 2; ++i)
        {
            const float y = side.getBottom() + h * 0.045f * (float) (i + 1);
            g.drawLine (side.getRight() + h * 0.04f, y, label.getRight() - h * 0.05f, y, stroke * 0.5f);
        }

        // The window with the two hubs and the tape between them.
        g.setColour (line);
        auto window = juce::Rectangle<float> (w * 0.52f, h * 0.22f).withCentre ({ label.getCentreX(), label.getBottom() - h * 0.17f });
        g.drawRoundedRectangle (window, window.getHeight() * 0.5f, stroke * 0.8f);
        const float hr = window.getHeight() * 0.36f;
        for (float dx : { -window.getWidth() * 0.3f, window.getWidth() * 0.3f })
        {
            const auto c = window.getCentre().translated (dx, 0.0f);
            g.drawEllipse (juce::Rectangle<float> (hr * 2.0f, hr * 2.0f).withCentre (c), stroke * 0.8f);
            for (int k = 0; k < 6; ++k)
            {
                const float a = (float) hubAngle + juce::MathConstants<float>::twoPi * (float) k / 6.0f;
                g.drawLine ({ c.getPointOnCircumference (hr * 0.45f, a), c.getPointOnCircumference (hr, a) }, stroke * 0.7f);
            }
        }
        g.setColour (line.withAlpha (0.55f));
        g.drawLine (window.getCentreX() - window.getWidth() * 0.3f, window.getCentreY() + hr, window.getCentreX() + window.getWidth() * 0.3f,
                    window.getCentreY() + hr, stroke * 0.7f);

        // The head opening: a trapezoid with two guide holes.
        g.setColour (line);
        juce::Path head;
        const float bottom = r.getBottom() - stroke;
        head.startNewSubPath (r.getX() + w * 0.2f, bottom);
        head.lineTo (r.getX() + w * 0.25f, bottom - h * 0.16f);
        head.lineTo (r.getRight() - w * 0.25f, bottom - h * 0.16f);
        head.lineTo (r.getRight() - w * 0.2f, bottom);
        g.strokePath (head, juce::PathStrokeType (stroke * 0.8f));
        for (float fx : { 0.36f, 0.64f })
            g.drawEllipse (juce::Rectangle<float> (h * 0.05f, h * 0.05f).withCentre ({ r.getX() + w * fx, bottom - h * 0.08f }), stroke * 0.7f);
    }
}
