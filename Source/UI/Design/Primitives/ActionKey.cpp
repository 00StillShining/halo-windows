#include "UI/Design/Primitives/ActionKey.h"
#include "UI/Design/Paint.h"

namespace halo
{
    namespace
    {
        constexpr float padX = 16.0f, iconBox = 16.0f, iconGap = 10.0f, ledRoom = 18.0f, radius = 4.0f;

        juce::Font keyFont() { return HaloFonts::face (HaloFonts::Face::interMedium, 13.5f); }

        juce::String dot() { return juce::String::fromUTF8 (" \xc2\xb7 "); }
        juce::String enDash() { return juce::String::charToString ((juce::juce_wchar) 0x2013); }
    }

    ActionKey::ActionKey (const juce::String& l, Pict i, Variant v) : juce::Button (l), label (l), icon (i), variant (v)
    {
        setWantsKeyboardFocus (true);
        setMouseCursor (juce::MouseCursor::PointingHandCursor);
        setVariant (v);
    }

    void ActionKey::setLabel (const juce::String& l)
    {
        if (l == label)
            return;
        label = l;
        setButtonText (l);
        repaint();
    }

    void ActionKey::setIcon (Pict i)
    {
        icon = i;
        repaint();
    }

    void ActionKey::setVariant (Variant v)
    {
        variant = v;
        // Locked and busy keys look dead and can't be pressed.
        setEnabled (v != Variant::lockedWrite && v != Variant::busy);
        setMouseCursor (isEnabled() ? juce::MouseCursor::PointingHandCursor : juce::MouseCursor::NormalCursor);
        repaint();
    }

    juce::String ActionKey::captionFor (Variant v)
    {
        switch (v)
        {
            case Variant::deviceWrite: return "writes to the ep" + enDash() + "40" + dot() + "asks first";
            case Variant::lockedWrite: return "back up the ep" + enDash() + "40 first (backups, ctrl 6)";
            case Variant::busy:        return "busy: sending to the ep" + enDash() + "40";
            case Variant::neutral:
            case Variant::primary:
            case Variant::destructive:
            default:                   return {};
        }
    }

    int ActionKey::preferredWidth() const
    {
        float w = std::ceil (paint::textWidth (keyFont(), label)) + 2.0f * padX;
        if (icon != Pict::none || variant == Variant::destructive)
            w += iconBox + iconGap;
        if (variant == Variant::deviceWrite || variant == Variant::lockedWrite)
            w += ledRoom;
        return (int) w;
    }

    void ActionKey::paintButton (juce::Graphics& g, bool highlighted, bool down)
    {
        const auto& t = theme();
        auto r = getLocalBounds().toFloat();
        const bool enabled = isEnabled();

        juce::Colour fill, textColour = t.scr.ink, iconColour = t.scr.ink;
        bool outlined = false;
        juce::Colour outline;
        switch (variant)
        {
            case Variant::primary:
            case Variant::deviceWrite:
                fill = t.scr.paper;
                textColour = iconColour = juce::Colour (0xff000005);
                break;
            case Variant::lockedWrite:
            case Variant::busy:
                outlined = true;
                outline = t.scr.frame;
                textColour = iconColour = t.scr.ramp[5];
                break;
            case Variant::destructive:
                outlined = true;
                outline = t.honest.danger;
                iconColour = t.honest.danger;
                break;
            case Variant::neutral:
            default:
                fill = t.scr.neutralKey;
                break;
        }
        if (! enabled && variant != Variant::lockedWrite && variant != Variant::busy)
        {
            // Plainly disabled: dead outline, dim label.
            outlined = true;
            outline = t.scr.frame;
            fill = {};
            textColour = iconColour = t.scr.ramp[5];
        }

        if (! outlined)
        {
            auto f = fill;
            if (down)
                f = f.darker (0.18f);
            else if (highlighted)
                f = f.brighter (variant == Variant::neutral ? 0.22f : 0.06f);
            g.setColour (f);
            g.fillRoundedRectangle (r, radius);
        }
        else
        {
            if (highlighted && enabled)
            {
                g.setColour (t.scr.panel);
                g.fillRoundedRectangle (r, radius);
            }
            g.setColour (outline);
            g.drawRoundedRectangle (r.reduced (0.75f), radius, 1.5f);
        }

        auto content = r.reduced (padX, 0.0f);
        if (variant == Variant::deviceWrite)
        {
            auto led = content.removeFromLeft (ledRoom);
            paint::deckLed (g, { led.getX() + 4.0f, led.getCentreY() }, t.honest.danger, 7.0f);
        }
        else if (variant == Variant::lockedWrite)
        {
            auto lock = content.removeFromLeft (ledRoom);
            pictograms::draw (g, Pict::lockClosed, lock.withSizeKeepingCentre (14.0f, 14.0f).withX (lock.getX() - 2.0f), t.scr.ramp[5], 1.4f);
        }
        const auto drawIcon = variant == Variant::destructive ? Pict::triangle : icon;
        if (drawIcon != Pict::none)
        {
            auto ib = content.removeFromLeft (iconBox);
            pictograms::draw (g, drawIcon, ib.withSizeKeepingCentre (iconBox, iconBox), iconColour, 1.5f);
            content.removeFromLeft (iconGap);
        }
        paint::text (g, label, content, keyFont(), textColour, juce::Justification::centredLeft);

        if (hasKeyboardFocus (false))
        {
            g.setColour (t.scr.hot);
            g.drawRoundedRectangle (r.reduced (0.75f), radius, 1.5f);
        }
    }
}
