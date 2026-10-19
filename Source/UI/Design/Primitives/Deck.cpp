#include "UI/Design/Primitives/Deck.h"
#include "UI/Design/Paint.h"
#include "UI/Design/Primitives/Gauges.h"

namespace halo
{
    namespace
    {
        const juce::String dot = juce::String::fromUTF8 (" \xc2\xb7 ");
        const juce::String enDash = juce::String::charToString ((juce::juce_wchar) 0x2013);

        Pict pictFor (AppMode m)
        {
            switch (m)
            {
                case AppMode::play:    return Pict::play;
                case AppMode::load:    return Pict::load;
                case AppMode::edit:    return Pict::edit;
                case AppMode::chop:    return Pict::chop;
                case AppMode::capture: return Pict::capture;
                case AppMode::rack:    return Pict::rack;
                case AppMode::backups: return Pict::backup;
            }
            return Pict::none;
        }
    }

    Deck::Deck (AppModel& m, EncoderBus& b) : model (m), bus (b)
    {
        setOpaque (false);
        for (auto mode : keymap::deckOrder())
        {
            auto* key = modeKeys.add (new DeckKey (keymap::modeWord (mode), pictFor (mode), juce::String (keymap::shortcutDigit (mode))));
            key->onClick = [this, mode] { model.setMode (mode); };
            key->setTooltip (juce::String (keymap::modeWord (mode)) + "  (ctrl " + juce::String (keymap::shortcutDigit (mode)) + ")");
            addAndMakeVisible (key);
        }
        shiftKey.onClick = [this] { bus.toggleShiftLatch(); };
        shiftKey.setTooltip ("shift layer: hold Shift, or click to latch it for one press");
        recKey.onClick = [this] { model.toggleRecording(); };
        addAndMakeVisible (shiftKey);
        addAndMakeVisible (recKey);
        for (int i = 0; i < 4; ++i)
            addAndMakeVisible (encoders.add (new DeckEncoder (bus, i)));

        model.addListener (this);
        bus.addListener (this);
        updateLeds();
        updateShift();
        startTimer (250);
    }

    Deck::~Deck()
    {
        model.removeListener (this);
        bus.removeListener (this);
    }

    juce::Rectangle<int> Deck::local (const layout::Rect& r) const
    {
        return juce::Rectangle<float> (r.x - frame.deck.x, r.y - frame.deck.y, r.w, r.h).toNearestInt();
    }

    void Deck::setFrame (const layout::Frame& f)
    {
        frame = f;
        for (int i = 0; i < modeKeys.size(); ++i)
        {
            auto* k = modeKeys[i];
            k->setCapSize ((int) f.keyCap);
            k->setShowLegend (f.keyLegends);
            k->setBounds (local (f.modeKeys[(size_t) i]));
        }
        shiftKey.setCapSize ((int) f.shiftKey.w);
        shiftKey.setShowLegend (f.keyLegends);
        shiftKey.setBounds (local (f.shiftKey));
        recKey.setCapSize ((int) f.recKey.w);
        recKey.setShowLegend (f.keyLegends);
        recKey.setBounds (local (f.recKey));
        for (int i = 0; i < 4; ++i)
        {
            encoders[i]->setDiameter ((int) f.encoderDiameter);
            encoders[i]->setShowName (f.encoderNames);
            encoders[i]->setBounds (local (f.encoders[(size_t) i]));
        }
        repaint();
    }

    juce::String Deck::recReason() const
    {
        return model.monitor().isRunning() ? juce::String() : juce::String ("rec needs monitor on (ctrl m)");
    }

    void Deck::updateShift()
    {
        shiftKey.setActive (bus.shiftActive());
        shiftKey.setLed (bus.shiftLatchedOnly() ? DeckKey::Led::on : DeckKey::Led::off);
        // The shift key draws pressed while the layer is on.
        shiftKey.setToggleState (bus.shiftActive(), juce::dontSendNotification);
    }

    void Deck::updateLeds()
    {
        const auto mode = model.mode();
        const auto& lib = model.device();
        auto& engine = model.monitor();
        const auto& order = keymap::deckOrder();
        for (int i = 0; i < modeKeys.size(); ++i)
        {
            const auto m = order[(size_t) i];
            auto* k = modeKeys[i];
            k->setActive (m == mode);
            DeckKey::Led led = DeckKey::Led::off;
            switch (m)
            {
                case AppMode::load:
                    led = lib.state() == Ep40Library::State::syncing ? DeckKey::Led::blink
                        : lib.state() == Ep40Library::State::failed ? DeckKey::Led::crimson : DeckKey::Led::off;
                    break;
                case AppMode::edit:
                    led = model.editor().hasSource() ? DeckKey::Led::dim : DeckKey::Led::off;
                    break;
                case AppMode::chop:
                    led = model.editor().chopOpen && ! model.editor().slices.cuts().empty() ? DeckKey::Led::dim : DeckKey::Led::off;
                    break;
                case AppMode::capture:
                    led = engine.isRecording() || model.grabber().isBusy() ? DeckKey::Led::crimson : DeckKey::Led::off;
                    break;
                case AppMode::rack:
                    led = model.rack().parameters().engaged.load() ? DeckKey::Led::on : DeckKey::Led::off;
                    break;
                case AppMode::backups:
                    led = model.backup().isRunning() ? DeckKey::Led::blink
                        : model.writesUnlocked() ? DeckKey::Led::on : DeckKey::Led::hollow;
                    break;
                case AppMode::play:
                default:
                    break;
            }
            if (m == mode && led != DeckKey::Led::crimson && led != DeckKey::Led::blink)
                led = DeckKey::Led::on;
            k->setLed (led);
        }
        const bool canRecord = engine.isRunning();
        recKey.setEnabled (canRecord || engine.isRecording());
        recKey.setLed (engine.isRecording() ? DeckKey::Led::blink : DeckKey::Led::off);
        recKey.setTooltip (canRecord ? juce::String ("record (ctrl r)") : recReason());
        const bool live = engine.isRunning();
        if (live != metersLive)
        {
            metersLive = live;
            startTimer (live ? 33 : 250);
            repaint (local (frame.vu));
        }
    }

    void Deck::timerCallback()
    {
        // Blinks at 2 Hz whatever the meter rate.
        const int ticksPerBlink = metersLive ? 8 : 1;
        if (++tick >= ticksPerBlink)
        {
            tick = 0;
            blink = ! blink;
            for (auto* k : modeKeys)
                k->setBlinkPhase (blink);
            recKey.setBlinkPhase (blink);
            updateLeds();
        }
        if (metersLive && frame.showVu)
        {
            levels = model.monitor().levels();
            repaint (local (frame.vu));
        }
    }

    void Deck::paint (juce::Graphics& g)
    {
        const auto& t = theme();
        const auto& d = t.deck;
        const auto print = HaloFonts::font (Role::printMicro);

        // Speaker grille: a staggered 7 px dot field.
        if (frame.showGrille)
        {
            const auto r = local (frame.grille).toFloat();
            g.setColour (d.grille);
            int row = 0;
            for (float y = r.getY() + 2.0f; y < r.getBottom() - 1.0f; y += 7.0f, ++row)
                for (float x = r.getX() + (row % 2 ? 5.5f : 2.0f); x < r.getRight() - 1.0f; x += 7.0f)
                    g.fillEllipse (x - 1.25f, y - 1.25f, 2.5f, 2.5f);
        }

        if (! frame.modeCaption.empty())
        {
            paint::text (g, "mode", local (frame.modeCaption).toFloat(), print, d.print);
            paint::text (g, "encoders" + dot + "wheel turn" + dot + "alt fine" + dot + "ctrl coarse" + dot + "click press",
                         local (frame.encoderCaption).toFloat(), print, d.print, juce::Justification::centred);
        }

        // VU window: L/R ladders, tap `out` (the same meters as PLAY).
        if (frame.showVu)
        {
            const auto r = local (frame.vu).toFloat();
            g.setColour (d.grille);
            g.fillRoundedRectangle (r, 4.0f);
            g.setColour (d.edge);
            g.drawRoundedRectangle (r.reduced (0.5f), 4.0f, 1.0f);
            auto inner = r.reduced (10.0f, 8.0f);
            const auto shown = metersLive ? levels : StereoLevels {};
            auto litFor = [] (float db) { return std::isfinite (db) ? juce::jlimit (0, 12, (int) std::floor ((db + 40.0f) / 40.0f * 12.0f)) : 0; };
            auto col = inner.withWidth (12.0f);
            gauges::ladder (g, col, 12, litFor (shown.peakL), t.scr.ink, true, d.ledOffDeck);
            gauges::ladder (g, col.translated (18.0f, 0.0f), 12, litFor (shown.peakR), t.scr.ink, true, d.ledOffDeck);
            paint::text (g, "l  r", { col.getX() - 1.0f, r.getBottom() - 1.0f, 40.0f, 12.0f }, print, d.print);
            paint::text (g, "out", { inner.getRight() - 24.0f, inner.getY(), 24.0f, 12.0f }, print, d.print, juce::Justification::topRight);
            paint::text (g, metersLive ? "live" : "off", { inner.getRight() - 30.0f, inner.getBottom() - 12.0f, 30.0f, 12.0f }, print,
                         d.print, juce::Justification::bottomRight);
        }
        if (! frame.nameplate.empty())
            paint::text (g, "halo" + dot + "hl" + enDash + "40", local (frame.nameplate).toFloat(), print, d.print,
                         juce::Justification::centredRight);
    }
}
