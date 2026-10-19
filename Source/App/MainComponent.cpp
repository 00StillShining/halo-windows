#include "App/MainComponent.h"
#include "UI/Design/Paint.h"
#include "UI/Design/Pictograms.h"

namespace halo
{
    namespace
    {
        const juce::String dot = juce::String::fromUTF8 (" \xc2\xb7 ");

        juce::Rectangle<int> toInt (const layout::Rect& r)
        {
            return juce::Rectangle<float> (r.x, r.y, r.w, r.h).toNearestInt();
        }

        // Which milestone rebuilds each legacy page (plan section 6).
        const char* arrivesIn (AppMode m)
        {
            switch (m)
            {
                case AppMode::edit:    return "R7";
                case AppMode::load:    return "R8";
                case AppMode::capture: return "R9";
                case AppMode::rack:    return "R10";
                case AppMode::backups: return "R11";
                case AppMode::play:
                case AppMode::chop:
                default:               return "";
            }
        }
    }

    MainComponent::MainComponent (AppModel& m)
        : model (m),
          writeGate (*this, WriteGate::Environment {
              [this] { return model.writesUnlocked(); },
              [this] { return model.canTalkToDevice(); },
              [this] { const auto b = model.latestBackup(); return b ? Ep40Backup::describe (*b) : juce::String(); },
              [this] { model.backup().forceRescan(); },
              [this] (bool hide) { cardOpen = hide; updateMode(); },
              deviceWrites.get() }),
          rail (m, writeGate)
    {
        setOpaque (true);
        setLookAndFeel (&lookAndFeel);
        // Desktop windows (popup menus, alert windows, tooltips) get it too.
        juce::LookAndFeel::setDefaultLookAndFeel (&lookAndFeel);

        for (auto* c : std::initializer_list<juce::Component*> { &statusStrip, &stageFrame, &rail, &plate, &deck })
            addAndMakeVisible (c);
        addChildComponent (chopSurface);
        rail.onPreferredWidthChanged = [this] { layoutBody(); };
        model.addListener (this);
        setWantsKeyboardFocus (true);
        setSize (HaloMetrics::windowDefaultWidth, HaloMetrics::windowDefaultHeight);

       #if JUCE_DEBUG
        // Debug-only capture hooks, so UI states can be screenshotted without
        // clicking: HALO_RAIL=open|closed, HALO_MODE=1...7 (Ctrl+n order).
        const auto railHook = juce::SystemStats::getEnvironmentVariable ("HALO_RAIL", {});
        rail.setExpanded (railHook != "closed");
        if (const auto mode = keymap::modeForCtrlDigit (juce::SystemStats::getEnvironmentVariable ("HALO_MODE", "1").getIntValue()))
            model.setMode (*mode);
       #else
        rail.setExpanded (true);
       #endif
        updateMode();
    }

    MainComponent::~MainComponent()
    {
        model.removeListener (this);
        juce::LookAndFeel::setDefaultLookAndFeel (nullptr);
        setLookAndFeel (nullptr);
    }

    float MainComponent::physicalScale() const
    {
        if (auto* peer = getPeer())
            return (float) peer->getPlatformScaleFactor() * juce::Desktop::getInstance().getGlobalScaleFactor();
        return juce::Desktop::getInstance().getGlobalScaleFactor();
    }

    void MainComponent::updateMode()
    {
        const auto mode = model.mode();
        const bool play = keymap::showsStage (mode);
        const bool chop = keymap::showsChop (mode);
        // U12: the stage renders only in PLAY (hidden, its GL context detaches),
        // and hides under a confirm card (H6: nothing JUCE paints sits over it).
        stage.setVisible (play && ! cardOpen);
        stageFrame.setVisible (play);
        chopSurface.setVisible (chop);
        rail.setVisible (! chop);
        plate.setVisible (! play && ! chop);
        plate.setMode (mode);
        if (chop && ! chopSurface.hasKeyboardFocus (true) && ! cardOpen)
            chopSurface.grabKeyboardFocus();
        if (mode != shownMode)
        {
            shownMode = mode;
            layoutBody();
        }
        repaint (toInt (frame.body));
    }

    void MainComponent::layoutBody()
    {
        const auto body = toInt (frame.body);
        const auto mode = model.mode();
        if (keymap::showsChop (mode))
        {
            chopSurface.setBounds (body);
            return;
        }
        // Today's panels keep their rail width, docked at the right of the body.
        auto rest = body;
        const int railW = juce::jmin (rail.preferredWidth (getWidth()), body.getWidth() - 320);
        rail.setBounds (rest.removeFromRight (railW));
        rest.removeFromRight (8);
        if (keymap::showsStage (mode))
            stageFrame.setBounds (rest);
        else
            plate.setBounds (rest);
       #if JUCE_DEBUG
        // H6: no JUCE child may overlap the GL child window.
        if (stage.isVisible())
        {
            const auto gl = stageFrame.getBoundsInParent().getPosition() + stageFrame.stageBounds().getPosition();
            const auto glRect = stageFrame.stageBounds().withPosition (gl);
            for (auto* c : getChildren())
                if (c != &stageFrame && c->isVisible() && c != &tooltips)
                    jassert (! c->getBounds().intersects (glRect));
        }
       #endif
    }

    void MainComponent::resized()
    {
        frame = layout::frame ((float) getWidth(), (float) getHeight(), physicalScale());
        statusStrip.setBounds (toInt (frame.strip));
        deck.setBounds (toInt (frame.deck));
        deck.setFrame (frame);
        layoutBody();
    }

    void MainComponent::paint (juce::Graphics& g)
    {
        const auto& t = theme();
        // The chassis: graphite, the only gradient in the app.
        g.setGradientFill (juce::ColourGradient::vertical (t.deck.top, 0.0f, t.deck.bottom, (float) getHeight()));
        g.fillAll();
        g.setColour (t.deck.edge);
        g.drawRect (getLocalBounds(), 1);

        // The glass: 12 px radius, 1 px bezel.
        const auto glass = juce::Rectangle<float> (frame.glass.x, frame.glass.y, frame.glass.w, frame.glass.h);
        g.setColour (t.deck.bezel);
        g.fillRoundedRectangle (glass.expanded (1.0f), 13.0f);
        g.setColour (t.scr.bg);
        g.fillRoundedRectangle (glass, 12.0f);
    }

    void MainComponent::LegacyPlate::paint (juce::Graphics& g)
    {
        const auto& t = theme();
        auto r = getLocalBounds().toFloat();
        g.setColour (t.scr.panel);
        g.fillRoundedRectangle (r, 4.0f);
        paint::hatch (g, r, t.scr.rule, 9.0f, 1.0f);
        auto centre = r.withSizeKeepingCentre (juce::jmin (r.getWidth() - 40.0f, 360.0f), 120.0f);
        const Pict icons[] { Pict::play, Pict::load, Pict::edit, Pict::capture, Pict::rack, Pict::backup, Pict::chop };
        pictograms::draw (g, icons[(size_t) mode], centre.removeFromTop (40.0f).withSizeKeepingCentre (32.0f, 32.0f), t.scr.mute, 1.5f);
        centre.removeFromTop (10.0f);
        paint::text (g, juce::String (keymap::modeWord (mode)).toUpperCase(), centre.removeFromTop (20.0f), HaloFonts::font (Role::label),
                     t.scr.dim, juce::Justification::centred);
        paint::text (g, juce::String ("page not rebuilt yet") + dot + "arrives in " + arrivesIn (mode), centre.removeFromTop (20.0f),
                     HaloFonts::font (Role::ui), t.scr.dim, juce::Justification::centred);
        paint::text (g, "today's controls are on the right, unchanged", centre.removeFromTop (20.0f), HaloFonts::font (Role::ui),
                     t.scr.dim, juce::Justification::centred);
    }

    void MainComponent::modifierKeysChanged (const juce::ModifierKeys& mods)
    {
        bus.setShiftHeld (mods.isShiftDown());
    }

    bool MainComponent::keyPressed (const juce::KeyPress& key)
    {
        // Ctrl+1...7 switch modes (1-6 unchanged; 7 = CHOP); Ctrl+M monitor;
        // Ctrl+R record; Ctrl+G grab.
        const auto mods = key.getModifiers();
        if (mods.isCtrlDown())
        {
            const int code = key.getKeyCode();
            if (code == 'M') { model.toggleMonitor(); return true; }
            if (code == 'R') { model.toggleRecording(); return true; }
            if (code == 'G') { model.grabNow(); return true; }
            if (const auto mode = keymap::modeForCtrlDigit (code - '0'))
            {
                model.setMode (*mode);
                return true;
            }
        }
        if (bus.keyPressed (key))
            return true;
        if (key.isKeyCode (juce::KeyPress::escapeKey))
        {
            // Esc: leave CHOP to EDIT, else collapse the rail.
            if (model.mode() == AppMode::chop)
            {
                model.editor().closeChop();
                return true;
            }
        }
        return false;
    }
}
