#include "UI/Design/Primitives/ParamStrip.h"
#include "UI/Design/Numerals.h"
#include "UI/Design/Paint.h"
#include "UI/Design/Primitives/DeckControls.h"

namespace halo
{
    namespace
    {
        juce::String enDash() { return juce::String::charToString ((juce::juce_wchar) 0x2013); }
    }

    //==========================================================================
    ParamCell::ParamCell (EncoderBus& b, int i) : bus (b), index (i)
    {
        setOpaque (true);
        bus.addListener (this);
        setMouseCursor (juce::MouseCursor::UpDownResizeCursor);
    }

    ParamCell::~ParamCell()
    {
        bus.removeListener (this);
    }

    void ParamCell::paint (juce::Graphics& g)
    {
        const auto& t = theme();
        const auto& e = t.encoder (index);
        const auto* b = bus.binding (index);
        const bool live = bus.isLive (index);
        const bool hot = live && (bus.isTouched (index) || bus.hovered() == index || bus.focused() == index);
        const bool shift = bus.shiftActive();
        const auto r = getLocalBounds().toFloat();
        const float w = r.getWidth(), h = r.getHeight();

        g.fillAll (t.scr.bg);
        g.setColour (t.scr.panel);
        g.fillRoundedRectangle (r, 4.0f);

        // Top bar: the encoder's colour, hot while touched or hovered.
        g.setColour (! live ? t.scr.frame : hot ? t.scr.hot : e.line);
        g.fillRect (r.withHeight (3.0f));

        const bool regular = size == CellSize::regular;
        const float labelY = regular ? 12.0f : 10.0f;
        const float capH = regular ? numerals::M : numerals::S;
        const float baseline = regular ? 64.0f : size == CellSize::compact ? 52.0f : 47.0f;

        // Label (or its shift-layer swap). Touched: it inverts to a filled tag.
        juce::String label = b != nullptr ? b->label : juce::String (enDash());
        bool labelDim = ! live;
        if (shift && b != nullptr)
        {
            if (b->shiftLabel.isNotEmpty())
                label = b->shiftLabel;
            else
                labelDim = true;
        }
        const auto labelFont = HaloFonts::font (Role::label);
        float labelRight;
        if (hot && ! labelDim)
        {
            const auto tagRect = paint::tag (g, { 8.0f, labelY - 3.0f }, label.toUpperCase(), paint::TagStyle::filled, e.fill, e.onFill, 18.0f);
            labelRight = tagRect.getRight();
        }
        else
        {
            const auto lampColour = live ? e.line : t.scr.frame;
            paint::lamp (g, { 14.0f, labelY + 6.0f }, lampColour, 7.0f);
            paint::text (g, label.toUpperCase(), { 23.0f, labelY, w - 30.0f, 12.0f }, labelFont, labelDim ? t.scr.dim : e.line);
            labelRight = 23.0f + paint::textWidth (labelFont, label.toUpperCase());
        }

        // One tag beside the label at most (the overflow rule); a narrow cell
        // shortens it ("AUTO-TRIM" -> "TRIM").
        if (b != nullptr && b->tag != nullptr && live)
        {
            auto tagText = b->tag();
            if (tagText.isNotEmpty())
            {
                if (paint::tagWidth (tagText) > w - labelRight - 16.0f && tagText.containsChar ('-'))
                    tagText = tagText.fromLastOccurrenceOf ("-", false, false);
                const bool on = b->tagOn == nullptr || b->tagOn();
                const float tw = paint::tagWidth (tagText);
                if (tw <= w - labelRight - 12.0f)
                    paint::tag (g, { w - 8.0f - tw, labelY - 4.0f }, tagText,
                                on ? paint::TagStyle::filled : paint::TagStyle::outline,
                                on ? e.fill : t.scr.frame, on ? e.onFill : t.scr.dim, 18.0f);
            }
        }

        if (! live)
        {
            numerals::draw (g, enDash(), 14.0f, baseline, capH, t.scr.frame);
            if (b != nullptr && b->reason != nullptr)
            {
                const auto reason = b->reason();
                const float x = 14.0f + numerals::width (enDash(), capH) + 14.0f;
                paint::text (g, reason, { x, baseline - capH, w - x - 10.0f, capH }, HaloFonts::font (Role::ui, 11.5f), t.scr.dim,
                             juce::Justification::centredLeft);
            }
            return;
        }

        // Value and unit.
        const auto value = b->valueText();
        const float valueRight = numerals::drawWithUnit (g, value, b->unitText(), 14.0f, baseline, capH, e.line, e.line);

        // Gauge to the right of the value, and its caption.
        const float gx = juce::jmax (valueRight + 12.0f, w * 0.50f);
        const float gaugeTop = labelY + (regular ? 20.0f : 16.0f);
        auto gaugeRect = juce::Rectangle<float> (gx, gaugeTop, w - 10.0f - gx, baseline - gaugeTop + 2.0f);
        if (b->gauge != nullptr && gaugeRect.getWidth() > 24.0f)
            b->gauge (g, gaugeRect, e.line, true);

        juce::String caption = b->caption != nullptr ? b->caption() : juce::String();
        if (bus.showsNoShift (index))
            caption = "no shift";
        if (caption.isNotEmpty() && size != CellSize::ultra)
            paint::text (g, caption, { 10.0f, baseline + 4.0f, w - 20.0f, 12.0f }, HaloFonts::font (Role::print, 10.5f), t.scr.dim,
                         juce::Justification::centredRight);

        // 2 px normalised value bar along the bottom.
        const double norm = b->getNorm != nullptr ? juce::jlimit (0.0, 1.0, b->getNorm()) : 0.0;
        g.setColour (t.scr.rule);
        g.fillRect (juce::Rectangle<float> (0.0f, h - 2.0f, w, 2.0f));
        g.setColour (e.line);
        g.fillRect (juce::Rectangle<float> (0.0f, h - 2.0f, w * (float) norm, 2.0f));
    }

    void ParamCell::mouseEnter (const juce::MouseEvent&) { bus.setHovered (index); }
    void ParamCell::mouseExit (const juce::MouseEvent&)  { if (bus.hovered() == index) bus.setHovered (-1); }

    void ParamCell::mouseDown (const juce::MouseEvent&)
    {
        dragged = false;
        bus.dragBegin (index);
    }

    void ParamCell::mouseDrag (const juce::MouseEvent& e)
    {
        if (e.getDistanceFromDragStart() > 3)
            dragged = true;
        if (dragged)
            bus.dragTo (index, (double) -e.getDistanceFromDragStartY(), e.mods.isAltDown() || e.mods.isRightButtonDown());
    }

    void ParamCell::mouseUp (const juce::MouseEvent& e)
    {
        bus.dragEnd (index);
        if (! dragged && e.mods.isLeftButtonDown() && e.getNumberOfClicks() < 2)
            bus.press (index);
    }

    void ParamCell::mouseDoubleClick (const juce::MouseEvent&) { bus.resetToDefault (index); }

    void ParamCell::mouseWheelMove (const juce::MouseEvent& e, const juce::MouseWheelDetails& w) { bus.wheel (index, e, w); }

    //==========================================================================
    void readout::paint (juce::Graphics& g, juce::Rectangle<float> r, int ramp, const juce::String& label,
                         const juce::String& value, bool useNumerals, float capHeight)
    {
        const auto& t = theme();
        ramp = juce::jlimit (0, 7, ramp);
        g.setColour (t.scr.ramp[(size_t) ramp]);
        g.fillRoundedRectangle (r, 4.0f);
        const auto ink = ramp >= 5 ? juce::Colour (0xff000000) : t.scr.hot;
        paint::text (g, label, { r.getX() + 9.0f, r.getY() + 6.0f, r.getWidth() - 18.0f, 14.0f },
                     HaloFonts::face (HaloFonts::Face::interMedium, 11.5f), ink);
        if (useNumerals)
            numerals::drawRight (g, value, r.getRight() - 10.0f, r.getBottom() - 9.0f, capHeight, ink);
        else
            paint::text (g, value, { r.getX() + 9.0f, r.getBottom() - capHeight - 12.0f, r.getWidth() - 19.0f, capHeight + 4.0f },
                         HaloFonts::font (Role::uiLight, capHeight), ink, juce::Justification::bottomRight);
    }

    //==========================================================================
    SoftKeyRow::SoftKeyRow (EncoderBus& b) : bus (b)
    {
        setOpaque (true);
        bus.addListener (this);
    }

    SoftKeyRow::~SoftKeyRow()
    {
        bus.removeListener (this);
    }

    juce::Rectangle<float> SoftKeyRow::pillBounds (int i) const
    {
        return juce::Rectangle<float> (pillW, pillH).withCentre ({ centres[(size_t) i], (float) getHeight() * 0.5f });
    }

    int SoftKeyRow::pillAt (juce::Point<float> p) const
    {
        for (int i = 0; i < 4; ++i)
            if (pillBounds (i).contains (p))
                return i;
        return -1;
    }

    void SoftKeyRow::paint (juce::Graphics& g)
    {
        const auto& t = theme();
        g.fillAll (t.scr.bg);
        if (hint.isNotEmpty())
            paint::text (g, paint::middleEllipsis (hint, HaloFonts::font (Role::ui, 11.0f), hintWidth),
                         { 0.0f, 0.0f, hintWidth, (float) getHeight() }, HaloFonts::font (Role::ui, 11.0f), t.scr.dim);

        const bool shift = bus.shiftActive();
        for (int i = 0; i < 4; ++i)
        {
            const auto* b = bus.binding (i);
            juce::String label;
            bool available = false;
            if (b != nullptr)
            {
                label = shift ? b->shiftPressLabel : b->pressLabel;
                available = bus.isLive (i) && (shift ? b->shiftPress != nullptr : b->press != nullptr);
                if (! available && shift && b->shiftPressLabel.isEmpty())
                    label = {};
            }
            paint::pill (g, pillBounds (i), label, i, available, hoverPill == i, downPill == i);
        }
    }

    void SoftKeyRow::mouseMove (const juce::MouseEvent& e)
    {
        const int p = pillAt (e.position);
        if (p != hoverPill)
        {
            hoverPill = p;
            setMouseCursor (p >= 0 ? juce::MouseCursor::PointingHandCursor : juce::MouseCursor::NormalCursor);
            repaint();
        }
    }

    void SoftKeyRow::mouseExit (const juce::MouseEvent&)
    {
        hoverPill = -1;
        repaint();
    }

    void SoftKeyRow::mouseDown (const juce::MouseEvent& e)
    {
        downPill = pillAt (e.position);
        repaint();
    }

    void SoftKeyRow::mouseUp (const juce::MouseEvent& e)
    {
        const int p = pillAt (e.position);
        if (p >= 0 && p == downPill)
            bus.press (p);
        downPill = -1;
        repaint();
    }

    //==========================================================================
    void popupCard::paint (juce::Graphics& g, juce::Rectangle<float> r, const EncoderBinding& b, int enc, bool compact)
    {
        const auto& t = theme();
        const auto& e = t.encoder (enc);
        const auto ink = juce::Colour (0xff000005);
        constexpr float radius = 6.0f;

        g.setColour (t.scr.paper);
        g.fillRoundedRectangle (r, radius);

        // Header block in the encoder's fill colour, wide enough for its label.
        const auto tagFont = HaloFonts::font (Role::tag);
        const float headerW = juce::jmax (compact ? 92.0f : 112.0f, std::ceil (paint::textWidth (tagFont, b.label.toUpperCase())) + 24.0f);
        auto header = r.withWidth (headerW);
        {
            juce::Path p;
            p.addRoundedRectangle (header.getX(), header.getY(), header.getWidth(), header.getHeight(), radius, radius, true, false, true, false);
            g.setColour (e.fill);
            g.fillPath (p);
        }
        paint::text (g, b.label.toUpperCase(), header.reduced (12.0f, 11.0f).withHeight (14.0f), tagFont, e.onFill);
        if (b.pressLabel.isNotEmpty())
            paint::text (g, "press: " + b.pressLabel, { header.getX() + 12.0f, header.getBottom() - 24.0f, header.getWidth() - 16.0f, 14.0f },
                         HaloFonts::font (Role::ui, 11.0f), e.onFill.withAlpha (0.85f));

        // Black value box with Numerals L.
        const float capH = compact ? numerals::M : numerals::L;
        auto content = r.withTrimmedLeft (headerW + 12.0f).reduced (0.0f, compact ? 9.0f : 11.0f);
        const float boxW = juce::jmin (compact ? 132.0f : 172.0f, content.getWidth() * 0.55f);
        auto box = content.removeFromLeft (boxW);
        g.setColour (ink);
        g.fillRoundedRectangle (box, 4.0f);
        numerals::drawWithUnitRight (g, b.valueText(), b.unitText(), box.getRight() - 12.0f, box.getCentreY() + capH * 0.5f,
                                     capH, t.scr.ink, t.scr.ink);

        // The encoder's cap as a small cylinder, its pointer at the current angle.
        content.removeFromLeft (compact ? 10.0f : 14.0f);
        auto capArea = content.removeFromLeft (compact ? 0.0f : 60.0f);
        if (! compact)
        {
            const float cw = capArea.getWidth() * 0.86f;
            const float rx = cw * 0.5f, ry = rx * 0.42f, bodyH = cw * 0.38f;
            const auto c = juce::Point<float> (capArea.getCentreX(), capArea.getCentreY() - bodyH * 0.35f);
            juce::Path body;
            body.startNewSubPath (c.x - rx, c.y);
            body.lineTo (c.x - rx, c.y + bodyH);
            body.addCentredArc (c.x, c.y + bodyH, rx, ry, 0.0f, -juce::MathConstants<float>::halfPi, juce::MathConstants<float>::halfPi, false);
            body.lineTo (c.x + rx, c.y);
            body.closeSubPath();
            g.setColour (e.cap.darker (0.45f));
            g.fillPath (body);
            // knurl lines on the side
            g.setColour (e.cap.darker (0.7f).withAlpha (0.55f));
            for (int k = 1; k < 12; ++k)
            {
                const float a = juce::MathConstants<float>::pi * (float) k / 12.0f;
                const float x = c.x - rx * std::cos (a);
                g.drawLine (x, c.y + ry * std::sin (a) * 0.2f, x, c.y + bodyH + ry * std::sin (a) - 1.0f, 0.8f);
            }
            g.setColour (e.cap);
            g.fillEllipse (juce::Rectangle<float> (rx * 2.0f, ry * 2.0f).withCentre (c));
            g.setColour (juce::Colours::white.withAlpha (0.25f));
            g.drawEllipse (juce::Rectangle<float> (rx * 2.0f, ry * 2.0f).withCentre (c), 0.8f);
            const float a = (float) encoder::pointerAngle (b.getNorm != nullptr ? b.getNorm() : 0.5);
            const auto tip = juce::Point<float> (c.x + rx * 0.8f * std::sin (a), c.y - ry * 0.8f * std::cos (a));
            g.setColour (enc == 2 ? juce::Colour (0xff15171A) : juce::Colour (0xffF2F2EC));
            g.drawLine ({ c, tip }, 2.2f);
        }

        // Hint in black on the paper.
        content.removeFromLeft (compact ? 0.0f : 12.0f);
        const auto hint = b.hint != nullptr ? b.hint() : juce::String ("wheel turn, alt fine, ctrl coarse");
        paint::paragraph (g, hint, content.reduced (0.0f, 2.0f).withTrimmedRight (10.0f), HaloFonts::font (Role::ui, 11.5f), ink, 2.0f,
                          juce::Justification::centredLeft);
    }
}
