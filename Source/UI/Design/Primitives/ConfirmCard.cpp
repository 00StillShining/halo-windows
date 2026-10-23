#include "UI/Design/Primitives/ConfirmCard.h"
#include "UI/Design/Paint.h"
#include "UI/Design/Primitives/Notice.h"

namespace halo
{
    namespace
    {
        constexpr float cardWidth = 480.0f, headerH = 34.0f, pad = 20.0f, keyH = 40.0f;
        const juce::Colour ink { 0xff000005 };

        juce::String dot() { return juce::String::fromUTF8 (" \xc2\xb7 "); }
    }

    // A card key: cancel (grey outline) or confirm (filled crimson).
    class ConfirmCard::Key : public juce::Button
    {
    public:
        Key (const juce::String& text, bool confirmKey) : juce::Button (text), confirm (confirmKey)
        {
            setWantsKeyboardFocus (true);
            setMouseCursor (juce::MouseCursor::PointingHandCursor);
        }

        void paintButton (juce::Graphics& g, bool highlighted, bool down) override
        {
            const auto& t = theme();
            auto r = getLocalBounds().toFloat();
            const auto font = HaloFonts::face (HaloFonts::Face::interSemiBold, 13.5f);
            if (confirm)
            {
                if (isEnabled())
                {
                    auto fill = t.honest.danger;
                    if (down) fill = fill.darker (0.2f);
                    else if (highlighted) fill = fill.brighter (0.1f);
                    g.setColour (fill);
                    g.fillRoundedRectangle (r, 4.0f);
                    paint::text (g, getButtonText(), r, font, juce::Colours::white, juce::Justification::centred);
                }
                else
                {
                    // Dead: a dim outline, never a live-looking key.
                    g.setColour (t.scr.paperShade);
                    g.drawRoundedRectangle (r.reduced (0.75f), 4.0f, 1.5f);
                    paint::text (g, getButtonText(), r, font, juce::Colour (0xff6E7173), juce::Justification::centred);
                }
            }
            else
            {
                if (highlighted || down)
                {
                    g.setColour (t.scr.paperHi);
                    g.fillRoundedRectangle (r, 4.0f);
                }
                g.setColour (juce::Colour (0xff6E7173));
                g.drawRoundedRectangle (r.reduced (0.75f), 4.0f, 1.5f);
                paint::text (g, getButtonText(), r, font, ink, juce::Justification::centred);
            }
            if (hasKeyboardFocus (false))
            {
                g.setColour (ink);
                g.drawRoundedRectangle (r.expanded (2.5f), 6.0f, 1.5f);
            }
        }

    private:
        const bool confirm;
    };

    ConfirmCard::ConfirmCard (WriteGate& g, WriteSpec s) : gate (g), writeSpec (std::move (s))
    {
        setWantsKeyboardFocus (true);
        setAlwaysOnTop (true);
        cancelKey = std::make_unique<Key> ("cancel", false);
        confirmKey = std::make_unique<Key> (writeSpec.confirmLabel, true);
        cancelKey->onClick = [this] { pressCancel(); };
        confirmKey->onClick = [this] { pressConfirm(); };
        addAndMakeVisible (*cancelKey);
        addAndMakeVisible (*confirmKey);
        // Tab order: cancel first (it has default focus), then confirm.
        cancelKey->setExplicitFocusOrder (1);
        confirmKey->setExplicitFocusOrder (2);
        refreshGate();
        startTimer (500); // 2 Hz live gate
    }

    ConfirmCard::~ConfirmCard()
    {
        stopTimer();
    }

    void ConfirmCard::refreshGate()
    {
        const bool open = gate.available();
        const auto why = gate.reason();
        if (open != gateOpen || why != reasonText)
        {
            gateOpen = open;
            reasonText = why;
            confirmKey->setEnabled (open);
            if (! open)
                focusOnConfirm = false;
            resized();
            repaint();
        }
    }

    bool ConfirmCard::confirmHasFocus() const { return focusOnConfirm && confirmKey->isEnabled(); }

    void ConfirmCard::focusConfirm()
    {
        if (! confirmKey->isEnabled())
            return;
        focusOnConfirm = true;
        if (isShowing())
            confirmKey->grabKeyboardFocus();
        repaint();
    }

    void ConfirmCard::pressConfirm()
    {
        refreshGate();
        if (! gateOpen)
            return;
        gate.confirmed(); // may delete this card
    }

    void ConfirmCard::pressCancel()
    {
        gate.closeCard(); // deletes this card
    }

    bool ConfirmCard::keyPressed (const juce::KeyPress& k)
    {
        if (k.isKeyCode (juce::KeyPress::escapeKey))
        {
            pressCancel();
            return true;
        }
        if (k.isKeyCode (juce::KeyPress::returnKey))
        {
            // Enter confirms only once Tab has moved focus to the confirm key.
            if (confirmHasFocus())
                pressConfirm();
            else
                pressCancel();
            return true;
        }
        if (k.isKeyCode (juce::KeyPress::tabKey))
        {
            if (confirmHasFocus() || ! confirmKey->isEnabled())
            {
                focusOnConfirm = false;
                if (isShowing())
                    cancelKey->grabKeyboardFocus();
                repaint();
            }
            else
            {
                focusConfirm();
            }
            return true;
        }
        return true; // modal: nothing else gets through
    }

    void ConfirmCard::mouseUp (const juce::MouseEvent&)
    {
        // A click on the scrim does nothing: the card wants a decision.
    }

    float ConfirmCard::contentHeight (float width) const
    {
        float h = headerH + pad;
        h += 48.0f + 10.0f;                                                  // pictogram row
        h += paint::paragraphHeight (writeSpec.body, HaloFonts::font (Role::body), width) + 14.0f;
        h += 20.0f + 8.0f;                                                   // backup line
        if (writeSpec.estimateSeconds > 0.0)
            h += 22.0f;
        if (! gateOpen)
            h += notice::height (NoticeData::blocker (reasonText), width) + 12.0f;
        h += 12.0f + keyH + pad;
        return h;
    }

    juce::Rectangle<float> ConfirmCard::cardBounds() const
    {
        const float h = contentHeight (cardWidth - 2.0f * pad);
        return juce::Rectangle<float> (cardWidth, h).withCentre (getLocalBounds().toFloat().getCentre());
    }

    void ConfirmCard::resized()
    {
        if (getParentComponent() != nullptr && getBounds() != getParentComponent()->getLocalBounds())
            setBounds (getParentComponent()->getLocalBounds());
        const auto card = cardBounds();
        auto keys = card.reduced (pad).removeFromBottom (keyH);
        const auto confirmW = juce::jmax (110.0f, paint::textWidth (HaloFonts::face (HaloFonts::Face::interSemiBold, 13.5f), writeSpec.confirmLabel) + 40.0f);
        confirmKey->setBounds (keys.removeFromRight (confirmW).toNearestInt());
        keys.removeFromRight (12.0f);
        cancelKey->setBounds (keys.removeFromRight (110.0f).toNearestInt());
    }

    void ConfirmCard::paint (juce::Graphics& g)
    {
        const auto& t = theme();
        g.fillAll (t.scr.bg.withAlpha (0.70f)); // scrim
        const auto card = cardBounds();
        paint::hardShadow (g, card, 6.0f);
        g.setColour (t.scr.paper);
        g.fillRoundedRectangle (card, 6.0f);

        // Crimson header with white tag text.
        auto header = card.withHeight (headerH);
        {
            juce::Path p;
            p.addRoundedRectangle (header.getX(), header.getY(), header.getWidth(), header.getHeight(), 6.0f, 6.0f, true, true, false, false);
            g.setColour (t.honest.danger);
            g.fillPath (p);
        }
        pictograms::draw (g, writeSpec.destructive ? Pict::triangle : Pict::upload,
                          { header.getX() + pad - 2.0f, header.getCentreY() - 8.0f, 16.0f, 16.0f }, juce::Colours::white, 1.5f);
        paint::text (g, writeSpec.title, header.withTrimmedLeft (pad + 24.0f), HaloFonts::font (Role::tag, 11.0f), juce::Colours::white);

        auto body = card.withTrimmedTop (headerH).reduced (pad, 0.0f);
        body.removeFromTop (pad);

        // Pictogram row: from -> ep-40 -> to.
        auto row = body.removeFromTop (48.0f);
        {
            const auto font = HaloFonts::font (Role::print, 10.5f);
            Pict from = Pict::file, to = Pict::device;
            juce::String middle = "ep" + juce::String::charToString ((juce::juce_wchar) 0x2013) + "40";
            switch (writeSpec.kind)
            {
                case WriteSpec::Kind::send:        from = Pict::file;   to = Pict::stop;  break;
                case WriteSpec::Kind::sendToPads:  from = Pict::chop;   to = Pict::pads;  break;
                case WriteSpec::Kind::assign:      from = Pict::file;   to = Pict::pads;  break;
                case WriteSpec::Kind::deleteSound: from = Pict::device; to = Pict::cross; break;
            }
            const Pict icons[] { from, Pict::arrowRight, Pict::device, Pict::arrowRight, to };
            const juce::String captions[] { writeSpec.from, {}, middle, {}, writeSpec.to };
            float x = row.getX();
            for (int i = 0; i < 5; ++i)
            {
                const bool arrow = i == 1 || i == 3;
                const float w = arrow ? 34.0f : 96.0f;
                auto slot = juce::Rectangle<float> (x, row.getY(), w, row.getHeight());
                pictograms::draw (g, icons[i], slot.withHeight (24.0f).withSizeKeepingCentre (arrow ? 16.0f : 22.0f, arrow ? 16.0f : 22.0f),
                                  i == 4 && writeSpec.destructive ? t.honest.danger : ink, 1.5f);
                if (captions[i].isNotEmpty())
                    paint::text (g, paint::middleEllipsis (captions[i], font, w), slot.withTrimmedTop (28.0f).withHeight (14.0f), font, ink,
                                 juce::Justification::centredTop);
                x += w;
            }
        }
        body.removeFromTop (10.0f);

        // Today's copy, verbatim.
        const auto bodyFont = HaloFonts::font (Role::body);
        const float textH = paint::paragraphHeight (writeSpec.body, bodyFont, body.getWidth());
        paint::paragraph (g, writeSpec.body, body.removeFromTop (textH), bodyFont, ink);
        body.removeFromTop (14.0f);

        // The backup this write relies on: always present.
        auto backup = body.removeFromTop (20.0f);
        const auto line = gate.backupLine();
        pictograms::draw (g, line.isNotEmpty() ? Pict::lockOpen : Pict::lockClosed, backup.removeFromLeft (16.0f).withSizeKeepingCentre (15.0f, 15.0f),
                          ink, 1.5f);
        backup.removeFromLeft (8.0f);
        paint::text (g, line.isNotEmpty() ? line : juce::String ("no complete backup: writes are locked"), backup,
                     HaloFonts::font (Role::bodyStrong), ink);
        body.removeFromTop (8.0f);

        // Time estimate as a dot timeline: one dot per 10 s.
        if (writeSpec.estimateSeconds > 0.0)
        {
            auto est = body.removeFromTop (22.0f);
            const int dots = juce::jlimit (1, 36, (int) std::ceil (writeSpec.estimateSeconds / 10.0));
            float x = est.getX();
            g.setColour (ink);
            for (int i = 0; i < dots; ++i, x += 8.0f)
                g.fillEllipse (x, est.getCentreY() - 2.5f, 5.0f, 5.0f);
            paint::text (g, writeSpec.estimateText + dot() + "about 40 kb/s", est.withTrimmedLeft (x - est.getX() + 6.0f),
                         HaloFonts::font (Role::ui), juce::Colour (0xff383E43));
        }

        // A closed gate prints its reason as a blocker inside the card.
        if (! gateOpen)
        {
            const auto n = NoticeData::blocker (reasonText);
            const float nh = notice::height (n, body.getWidth());
            notice::paint (g, n, body.removeFromTop (nh), true);
        }
    }
}
