#include "UI/Design/Primitives/PadGrid.h"
#include "Core/EncoderMath.h"
#include "Core/Ep40MidiMapping.h"
#include "UI/Design/Numerals.h"
#include "UI/Design/Paint.h"
#include "UI/Design/Pictograms.h"

namespace halo
{
    PadGrid::PadGrid()
    {
        setOpaque (false);
        setMouseCursor (juce::MouseCursor::PointingHandCursor);
    }

    int PadGrid::rowHeightFor (int regionHeight, int g)
    {
        return juce::jlimit (38, 100, (regionHeight - 3 * g) / 4); // 38: CHOP at the smallest sizes (its canvas keeps 120 px)
    }

    void PadGrid::setPads (const std::array<Pad, 12>& p)
    {
        bool same = true;
        for (size_t i = 0; i < 12 && same; ++i)
        {
            const auto& a = pads[i];
            const auto& b = p[i];
            same = a.numeral == b.numeral && a.name == b.name && a.corner == b.corner && a.empty == b.empty
                && a.start == b.start && a.selected == b.selected && a.trimmed == b.trimmed;
        }
        if (same)
            return;
        pads = p;
        repaint();
    }

    void PadGrid::setArmed (bool a)
    {
        if (a == armed)
            return;
        armed = a;
        repaint();
    }

    void PadGrid::flash (int i)
    {
        flashing = i;
        flashUntil = juce::Time::getMillisecondCounterHiRes() * 0.001 + 0.09;
        repaint();
        juce::Timer::callAfterDelay (100, [safe = juce::Component::SafePointer<PadGrid> (this)]
        {
            if (safe != nullptr)
                safe->repaint();
        });
    }

    juce::Rectangle<float> PadGrid::padBounds (int i) const
    {
        const float rowH = (float) rowHeightFor (getHeight(), gap);
        const float w = std::floor (((float) getWidth() - 2.0f * gap) / 3.0f);
        return { (float) (i % 3) * (w + gap), (float) (i / 3) * (rowH + gap), w, rowH };
    }

    void PadGrid::paint (juce::Graphics& g)
    {
        const auto& t = theme();
        const double now = juce::Time::getMillisecondCounterHiRes() * 0.001;
        const auto legendFont = HaloFonts::font (Role::mono, 10.5f);
        for (int i = 0; i < 12; ++i)
        {
            const auto& p = pads[(size_t) i];
            const auto r = padBounds (i);
            const bool row2 = (i / 3) % 2 == 1;
            const bool flash = i == flashing && now < flashUntil;
            const bool selected = p.selected || flash;

            if (p.empty && ! selected)
            {
                g.setColour (t.scr.frame);
                g.drawRoundedRectangle (r.reduced (0.6f), 3.0f, 1.2f);
            }
            else
            {
                g.setColour (juce::Colours::black);
                g.fillRoundedRectangle (r, 3.0f);
                auto fill = selected ? t.scr.ramp[7] : (row2 ? t.scr.stepB : t.scr.stepA);
                if (i == hovered && ! selected)
                    fill = fill.brighter (0.2f);
                g.setColour (fill);
                g.fillRoundedRectangle (r.withTrimmedBottom (2.0f), 3.0f);
            }
            const auto ink = selected ? juce::Colour (0xff000005) : t.scr.ink;

            // Device legend, top-left inside an unfilled corner.
            juce::String legend (ep40::gridLabels[(size_t) i].data());
            legend = legend == "ENTER" ? "ent" : legend.toLowerCase();
            paint::text (g, legend, { r.getX() + 6.0f, r.getY() + 3.0f, 30.0f, 13.0f }, legendFont,
                         selected ? juce::Colour (0xff45474D) : t.scr.dim, juce::Justification::topLeft);
            if (p.corner.isNotEmpty())
                paint::text (g, p.corner, { r.getRight() - 46.0f, r.getY() + 3.0f, 40.0f, 13.0f }, legendFont,
                             selected ? juce::Colour (0xff45474D) : t.scr.dim, juce::Justification::topRight);

            // Payload.
            if (p.empty)
            {
                paint::text (g, "empty", r, HaloFonts::font (Role::ui), t.scr.dim, juce::Justification::centred);
            }
            else if (p.numeral.isNotEmpty())
            {
                const float capH = r.getHeight() >= 58.0f ? numerals::S : numerals::XS;
                const float w = numerals::width (p.numeral, capH);
                numerals::draw (g, p.numeral, r.getCentreX() - w * 0.5f, r.getCentreY() + capH * 0.5f, capH, ink);
            }
            else if (p.name.isNotEmpty())
            {
                paint::paragraph (g, p.name, r.reduced (8.0f, 4.0f).withTrimmedTop (12.0f), HaloFonts::font (Role::ui, 13.0f),
                                  ink, 1.0f, juce::Justification::centredLeft);
            }

            if (p.selected)
            {
                // Short rows with a name: the play mark moves right, clear of the text.
                const bool right = p.name.isNotEmpty() && r.getHeight() < 70.0f;
                pictograms::draw (g, Pict::play, { right ? r.getRight() - 17.0f : r.getX() + 6.0f, r.getBottom() - 17.0f, 11.0f, 11.0f }, ink, 1.4f, true);
            }

            if (p.start)
            {
                const auto led = juce::Point<float> (r.getRight() - 10.0f, r.getY() + 10.0f);
                g.setColour ((selected ? juce::Colours::black : t.scr.hot).withAlpha (0.18f));
                g.fillEllipse (juce::Rectangle<float> (13.0f, 13.0f).withCentre (led));
                paint::lamp (g, led, selected ? juce::Colours::black : t.scr.hot, 6.0f);
                g.setColour (t.scr.ink);
                g.drawRoundedRectangle (r.expanded (1.0f), 4.0f, 2.0f);
            }

            if (p.trimmed)
            {
                const auto chip = juce::Rectangle<float> (r.getRight() - 28.0f, r.getBottom() - 12.0f, 20.0f, 5.0f);
                paint::hatch (g, chip, t.encoder (3).line, 3.0f, 1.0f);
                g.setColour (t.encoder (3).line);
                g.drawRect (chip, 1.0f);
            }

            if (armed)
            {
                paint::dashedRect (g, r.reduced (1.5f), t.scr.hot, 1.5f, 5.0f, 3.0f, 3.0f);
                const auto plus = juce::Point<float> (r.getRight() - 13.0f, r.getBottom() - 14.0f);
                g.setColour (t.scr.hot);
                g.drawLine (plus.x - 5.0f, plus.y, plus.x + 5.0f, plus.y, 1.6f);
                g.drawLine (plus.x, plus.y - 5.0f, plus.x, plus.y + 5.0f, 1.6f);
            }
        }
    }

    void PadGrid::mouseMove (const juce::MouseEvent& e)
    {
        int h = -1;
        for (int i = 0; i < 12; ++i)
            if (padBounds (i).contains (e.position))
                h = i;
        if (h != hovered)
        {
            hovered = h;
            repaint();
        }
    }

    void PadGrid::mouseExit (const juce::MouseEvent&)
    {
        hovered = -1;
        repaint();
    }

    void PadGrid::mouseUp (const juce::MouseEvent& e)
    {
        if (! e.mouseWasClicked())
            return;
        for (int i = 0; i < 12; ++i)
            if (padBounds (i).contains (e.position) && onPadClicked)
            {
                onPadClicked (i);
                return;
            }
    }

    void PadGrid::mouseWheelMove (const juce::MouseEvent&, const juce::MouseWheelDetails& w)
    {
        if (onWheel == nullptr)
            return;
        wheelAcc += w.deltaY * (w.isReversed ? -1.0f : 1.0f);
        const float notch = (float) encoder::kNotch;
        while (std::abs (wheelAcc) >= notch * 0.85f)
        {
            const int dir = wheelAcc > 0 ? 1 : -1;
            wheelAcc -= (float) dir * notch;
            onWheel (dir);
        }
    }
}
