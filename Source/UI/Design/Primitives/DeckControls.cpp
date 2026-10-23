#include "UI/Design/Primitives/DeckControls.h"
#include "UI/Design/Paint.h"

namespace halo
{
    namespace
    {
        juce::String enDash() { return juce::String::charToString ((juce::juce_wchar) 0x2013); }
    }

    //==========================================================================
    DeckKey::DeckKey (const juce::String& l, Pict p, const juce::String& s, Style st)
        : juce::Button (l), legendText (l), shortcutText (s), icon (p), style (st)
    {
        setMouseCursor (juce::MouseCursor::PointingHandCursor);
        setWantsKeyboardFocus (false);
    }

    void DeckKey::setActive (bool a)
    {
        if (a == active)
            return;
        active = a;
        repaint();
    }

    void DeckKey::setLed (Led l)
    {
        if (l == led)
            return;
        led = l;
        repaint();
    }

    void DeckKey::paintButton (juce::Graphics& g, bool highlighted, bool down)
    {
        const auto& t = theme();
        const auto& d = t.deck;
        const float cs = (float) capSize;
        const auto area = getLocalBounds().toFloat();
        auto cap = juce::Rectangle<float> (cs, cs).withCentre ({ area.getCentreX(), cs * 0.5f });
        const bool pressed = down || (active && style != Style::rec);
        const bool enabled = isEnabled();
        // The active key's icon and legend: white on graphite; black ink on
        // the aluminium field deck, where white would vanish.
        const bool field = t.skin == Theme::Skin::field;
        const auto activeInk = field ? juce::Colour (0xff000005) : d.ledOn;

        if (style == Style::rec)
        {
            // TP-7 record key: a round cap with the one bright red lamp.
            auto disc = cap.reduced (cs * 0.04f);
            g.setColour (d.keyLip);
            g.fillEllipse (disc.translated (0.0f, 2.0f));
            g.setColour (pressed ? d.keyDown : (highlighted && enabled ? d.key.brighter (0.08f) : d.key));
            g.fillEllipse (disc.translated (0.0f, pressed ? 1.0f : 0.0f));
            g.setColour (d.keyLip.withAlpha (0.8f));
            g.drawEllipse (disc.reduced (0.5f).translated (0.0f, pressed ? 1.0f : 0.0f), 1.0f);
            const auto c = disc.getCentre().translated (0.0f, pressed ? 1.0f : 0.0f);
            const float lampD = cs * 0.34f;
            if (led == Led::crimson || (led == Led::blink && blinkOn))
            {
                paint::deckLed (g, c, t.honest.danger, lampD);
            }
            else
            {
                // Idle REC is not red: a dark lamp (plan section 4.2).
                g.setColour (t.honest.danger.withMultipliedBrightness (enabled ? 0.38f : 0.22f));
                g.fillEllipse (juce::Rectangle<float> (lampD, lampD).withCentre (c));
            }
        }
        else
        {
            // Lip, then the cap with 1 px of press travel.
            g.setColour (d.keyLip);
            g.fillRoundedRectangle (cap.translated (0.0f, 2.0f), 6.0f);
            auto face = cap.translated (0.0f, pressed ? 1.0f : 0.0f);
            juce::Colour fill = style == Style::shift ? d.keyShift : d.key;
            if (pressed)
                fill = style == Style::shift ? d.keyShift.darker (0.12f) : d.keyDown;
            else if (highlighted && enabled)
                fill = fill.brighter (style == Style::shift ? 0.04f : 0.08f);
            g.setColour (fill);
            g.fillRoundedRectangle (face, 6.0f);
            g.setColour (juce::Colours::white.withAlpha (0.06f)); // 1 px top highlight
            g.fillRect (face.reduced (6.0f, 0.0f).removeFromTop (1.0f));

            if (style == Style::shift)
            {
                paint::text (g, "shift", face, HaloFonts::face (HaloFonts::Face::interMedium, juce::jmax (11.0f, cs * 0.25f)),
                             d.keyShiftText, juce::Justification::centred);
            }
            else
            {
                const float iconSize = juce::jmin (18.0f, cs * 0.42f);
                pictograms::draw (g, icon, juce::Rectangle<float> (iconSize, iconSize).withCentre (face.getCentre()),
                                  active ? activeInk : (enabled ? d.printCap.brighter (0.25f) : d.printDim), 1.5f);
                // ^n micro-print (chassis print, exempt from the 10.5 px floor;
                // the soft-key hint repeats it as legible text).
                if (shortcutText.isNotEmpty() && cs >= 38.0f)
                    paint::text (g, shortcutText, face.reduced (5.0f, 3.0f), HaloFonts::face (HaloFonts::Face::jetBrainsMono, 7.5f),
                                 d.printCap, juce::Justification::bottomRight);
            }

            // LED top-left.
            const auto ledCentre = juce::Point<float> (face.getX() + 7.5f, face.getY() + 7.5f);
            switch (led)
            {
                case Led::on:
                    paint::deckLed (g, ledCentre, d.ledOn, 4.0f);
                    if (field) // a white LED needs its bezel on a light cap
                        paint::hollowLamp (g, ledCentre, d.printDim, 6.5f, 1.0f);
                    break;
                case Led::dim:     paint::lamp (g, ledCentre, d.ledOn.withAlpha (0.45f), 4.0f); break;
                case Led::crimson: paint::deckLed (g, ledCentre, t.honest.danger, 4.0f); break;
                case Led::hollow:  paint::hollowLamp (g, ledCentre, d.printCap, 5.0f, 1.2f); break;
                case Led::blink:   if (blinkOn) paint::deckLed (g, ledCentre, d.ledOn, 4.0f);
                                   else paint::lamp (g, ledCentre, d.ledOffDeck, 4.0f); break;
                case Led::off:
                default:           paint::lamp (g, ledCentre, d.ledOffDeck, 4.0f); break;
            }
        }

        if (showLegend)
        {
            auto legendArea = area.withTop (cs + 4.0f);
            paint::text (g, legendText, legendArea, active ? HaloFonts::font (Role::bodyStrong, 11.0f) : HaloFonts::font (Role::print),
                         active ? activeInk : d.print, juce::Justification::centredTop);
        }
    }

    //==========================================================================
    DeckEncoder::DeckEncoder (EncoderBus& b, int i) : bus (b), index (i)
    {
        bus.addListener (this);
        setMouseCursor (juce::MouseCursor::UpDownResizeCursor);
        setRepaintsOnMouseActivity (false);
    }

    DeckEncoder::~DeckEncoder()
    {
        bus.removeListener (this);
    }

    void DeckEncoder::paintKnob (juce::Graphics& g, juce::Point<float> c, float dia, int enc, double norm, bool live,
                                 bool hovered, bool focused)
    {
        const auto& t = theme();
        const auto& e = t.encoder (enc);
        const float r = dia * 0.5f;
        auto circle = [&c] (float radius) { return juce::Rectangle<float> (radius * 2.0f, radius * 2.0f).withCentre (c); };

        // 3 px hard under-edge (16% black), skirt, knurl.
        g.setColour (juce::Colours::black.withAlpha (0.55f));
        g.fillEllipse (circle (r).translated (0.0f, 2.5f));
        g.setColour (juce::Colour (0xff1E1F22));
        g.fillEllipse (circle (r - 1.0f));
        juce::Path knurl;
        constexpr int ticks = 48;
        for (int k = 0; k < ticks; ++k)
        {
            const float a = juce::MathConstants<float>::twoPi * (float) k / (float) ticks;
            const auto p0 = c.getPointOnCircumference (r * 0.86f, a), p1 = c.getPointOnCircumference (r * 0.98f, a);
            knurl.startNewSubPath (p0);
            knurl.lineTo (p1);
        }
        g.setColour (juce::Colour (0xff3A3B3F));
        g.strokePath (knurl, juce::PathStrokeType (1.1f));

        // Cap in the encoder colour, a 1 px top highlight ring.
        const float capR = r * 0.77f;
        g.setColour (live ? e.cap : e.skirt.withMultipliedSaturation (0.5f).withMultipliedBrightness (0.6f));
        g.fillEllipse (circle (capR));
        g.setColour (juce::Colours::white.withAlpha (live ? 0.18f : 0.06f));
        g.drawEllipse (circle (capR - 0.5f), 1.0f);

        // Pointer: off-white (dark on the light grey cap), no easing.
        const float angle = (float) encoder::pointerAngle (norm);
        const auto p0 = c.getPointOnCircumference (capR * 0.30f, angle);
        const auto p1 = c.getPointOnCircumference (capR * 0.86f, angle);
        g.setColour (live ? (enc == 2 ? juce::Colour (0xff15171A) : juce::Colour (0xffF2F2EC)) : juce::Colour (0xff6E7173));
        g.drawLine ({ p0, p1 }, juce::jmax (2.0f, dia * 0.054f));

        if (hovered || focused)
        {
            g.setColour (e.line);
            g.drawEllipse (circle (r + 3.0f), 1.6f);
        }
    }

    void DeckEncoder::paint (juce::Graphics& g)
    {
        const auto& t = theme();
        const auto* b = bus.binding (index);
        const bool live = bus.isLive (index);
        const float dia = (float) diameter;
        const auto centre = juce::Point<float> ((float) getWidth() * 0.5f, dia * 0.5f + 4.0f);
        paintKnob (g, centre, dia, index, b != nullptr && b->getNorm ? b->getNorm() : 0.5, live,
                   bus.hovered() == index, bus.focused() == index);

        if (showName)
        {
            auto nameArea = juce::Rectangle<float> (0.0f, dia + 12.0f, (float) getWidth(), 16.0f);
            juce::String name = b != nullptr && live ? b->label.toLowerCase() : enDash();
            if (bus.shiftActive() && b != nullptr && b->shiftLabel.isNotEmpty())
                name = b->shiftLabel.toLowerCase();
            const auto font = HaloFonts::face (HaloFonts::Face::interMedium, 11.0f);
            const float w = paint::textWidth (font, name);
            const float x0 = nameArea.getCentreX() - (w + 10.0f) * 0.5f;
            paint::lamp (g, { x0 + 3.0f, nameArea.getCentreY() }, live ? t.encoder (index).line : t.deck.printDim, 5.0f);
            paint::text (g, name, nameArea.withX (x0 + 10.0f).withWidth (w + 4.0f), font, live ? t.deck.printCap.brighter (0.2f) : t.deck.print,
                         juce::Justification::centredLeft);
        }
    }

    void DeckEncoder::mouseEnter (const juce::MouseEvent&) { bus.setHovered (index); }
    void DeckEncoder::mouseExit (const juce::MouseEvent&)  { if (bus.hovered() == index) bus.setHovered (-1); }

    void DeckEncoder::mouseDown (const juce::MouseEvent&)
    {
        dragged = false;
        bus.dragBegin (index);
    }

    void DeckEncoder::mouseDrag (const juce::MouseEvent& e)
    {
        if (e.getDistanceFromDragStart() > 3)
            dragged = true;
        if (dragged)
            bus.dragTo (index, (double) -e.getDistanceFromDragStartY(), e.mods.isAltDown() || e.mods.isRightButtonDown());
    }

    void DeckEncoder::mouseUp (const juce::MouseEvent& e)
    {
        bus.dragEnd (index);
        if (! dragged && e.mods.isLeftButtonDown() && e.getNumberOfClicks() < 2)
            bus.press (index);
    }

    void DeckEncoder::mouseDoubleClick (const juce::MouseEvent&)
    {
        bus.resetToDefault (index);
    }

    void DeckEncoder::mouseWheelMove (const juce::MouseEvent& e, const juce::MouseWheelDetails& w)
    {
        bus.wheel (index, e, w);
    }
}
