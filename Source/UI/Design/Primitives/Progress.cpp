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
}
