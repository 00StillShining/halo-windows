#include "App/MainComponent.h"
#include "App/DevPose.h"
#include "UI/Pages/PlayEncoders.h"
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
              deviceWrites.get() })
    {
        setOpaque (true);
        setLookAndFeel (&lookAndFeel);
        // Desktop windows (popup menus, alert windows, tooltips) get it too.
        juce::LookAndFeel::setDefaultLookAndFeel (&lookAndFeel);

        for (auto* c : std::initializer_list<juce::Component*> { &statusStrip, &playPage, &plate, &deck, &lane, &softKeys })
            addAndMakeVisible (c);
        for (auto* p : std::initializer_list<juce::Component*> { &backupsPanel })
            addChildComponent (p);
        lane.setContent ([this] (juce::Graphics& g, juce::Rectangle<float> r)
        {
            if (model.mode() == AppMode::play)
                playPage.paintLane (g, r, frame.density != layout::Density::regular);
            else if (model.mode() == AppMode::chop)
                chopPage.paintLane (g, r, frame.density != layout::Density::regular);
            else if (model.mode() == AppMode::edit)
                editPage.paintLane (g, r, frame.density != layout::Density::regular);
            else if (model.mode() == AppMode::load)
                loadPage.paintLane (g, r, frame.density != layout::Density::regular);
            else if (model.mode() == AppMode::capture)
                capturePage.paintLane (g, r, frame.density != layout::Density::regular);
            else if (model.mode() == AppMode::rack)
                rackPage.paintLane (g, r, frame.density != layout::Density::regular);
        });
        playPage.onLaneChanged = [this] { lane.repaint(); };
        // One camera, one readout: drags on the stage move the ORBIT cell too.
        stage.onCameraChanged = [this] { for (auto* cell : cells) cell->repaint(); };
        for (int i = 0; i < 4; ++i)
            addAndMakeVisible (cells.add (new ParamCell (bus, i)));
        deck.onHint = [this] (const juce::String& h) { deckHint = h; updateHint(); };
        addChildComponent (chopPage);
        addChildComponent (editPage);
        addChildComponent (loadPage);
        addChildComponent (capturePage);
        addChildComponent (rackPage);
        rackPage.onFocusChanged = [this] { boundMode.reset(); updateBindings(); };
        // trim <-> shape latched on the page: the encoders swap pages.
        editPage.onPageChanged = [this] { boundMode.reset(); updateBindings(); };
        model.addListener (this);
        setWantsKeyboardFocus (true);
       #if HALO_HAS_STAGE_LOOK
        // The stage sits on the black glass (plan section 4.3): the dark look,
        // its backdrop exactly the glass so the two read as one surface.
        // HALO_STAGE_LOOK still picks a look for testing; legacy keeps paper.
        if (! theme().legacyLook && juce::SystemStats::getEnvironmentVariable ("HALO_STAGE_LOOK", {}).isEmpty())
        {
            auto look = StageLook::dark();
            look.backdrop = look.floor = theme().scr.bg;
            stage.setLook (look);
        }
       #endif
        setSize (HaloMetrics::windowDefaultWidth, HaloMetrics::windowDefaultHeight);

       #if JUCE_DEBUG
        // Debug-only capture hooks, so UI states can be screenshotted without
        // clicking: HALO_RAIL=open|closed, HALO_MODE=1...7 (Ctrl+n order).
        const auto railHook = juce::SystemStats::getEnvironmentVariable ("HALO_RAIL", {});
        playPage.setRailOpen (railHook != "closed");
        if (const auto mode = keymap::modeForCtrlDigit (juce::SystemStats::getEnvironmentVariable ("HALO_MODE", "1").getIntValue()))
            model.setMode (*mode);
        // HALO_UI_POSE=card:n,shift:1 pins the popup card or the shift layer for shots.
        if (const int card = DevPose::get ("card"); card >= 1 && card <= 4)
            bus.pinCard (card - 1);
        if (DevPose::get ("shift") == 1)
            bus.pinShift (true);
       #endif
        updateMode();
        startTimer (500); // cells and pills follow cached model state at 2 Hz
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
        playPage.setVisible (play);
        chopPage.setVisible (chop);
        const bool edit = mode == AppMode::edit;
        editPage.setVisible (edit);
        const bool load = mode == AppMode::load;
        loadPage.setVisible (load);
        const bool capture = mode == AppMode::capture;
        capturePage.setVisible (capture);
        const bool rackMode = mode == AppMode::rack;
        rackPage.setVisible (rackMode);
        for (auto m : { AppMode::backups })
            legacyPanel (m)->setVisible (m == mode);
        plate.setVisible (! play && ! chop && ! edit && ! load && ! capture && ! rackMode);
        plate.setMode (mode);
        statusStrip.setMonitorOwnedByEncoder (play);
        updateBindings();
        if (chop && ! chopPage.hasKeyboardFocus (true) && ! cardOpen)
            chopPage.grabKeyboardFocus();
        if (edit && ! editPage.hasKeyboardFocus (true) && ! cardOpen)
            editPage.grabKeyboardFocus();
        if (load && ! loadPage.hasKeyboardFocus (true) && ! cardOpen)
            loadPage.grabKeyboardFocus();
        if (capture && ! capturePage.hasKeyboardFocus (true) && ! cardOpen)
            capturePage.grabKeyboardFocus();
        if (rackMode && ! rackPage.hasKeyboardFocus (true) && ! cardOpen)
            rackPage.grabKeyboardFocus();
        if (mode != shownMode)
        {
            shownMode = mode;
            layoutBody();
        }
        repaint (toInt (frame.body));
    }

    void MainComponent::updateBindings()
    {
        const auto mode = model.mode();
        if (mode == boundMode)
            return;
        boundMode = mode;
        // Pages not rebuilt yet show dead cells until they land (honest: their
        // controls are the docked panel's, unchanged).
        if (mode == AppMode::play)
            bus.setBindings (playBindings (model, stage));
        else if (mode == AppMode::chop)
            bus.setBindings (chopPage.bindings());
        else if (mode == AppMode::edit)
            bus.setBindings (editPage.bindings (bus));
        else if (mode == AppMode::load)
            bus.setBindings (loadPage.bindings());
        else if (mode == AppMode::capture)
            bus.setBindings (capturePage.bindings());
        else if (mode == AppMode::rack)
            bus.setBindings (rackPage.bindings());
        else
            bus.setBindings ({});
        updateHint();
    }

    void MainComponent::updateHint()
    {
        juce::String hint;
        if (deckHint.isNotEmpty())
            hint = deckHint;
        else if (model.mode() == AppMode::play)
            hint = "drag: orbit" + dot + "wheel on stage: zoom" + dot + "double-click: reset";
        else if (model.mode() == AppMode::chop)
            hint = "shift + slice: nudge its cut" + dot + "shift + sensitivity press: clear cuts";
        else if (model.mode() == AppMode::edit)
            hint = "hold shift: gain, channels, quality, normalize" + dot + "click shape to latch it";
        else if (model.mode() == AppMode::load)
            hint = "category, sound, project, pad" + dot + "pad press assigns (asks first)";
        else if (model.mode() == AppMode::capture)
            hint = "item, filter, grab length, level" + dot + "level press records";
        else if (model.mode() == AppMode::rack)
            hint = "the encoders follow the focused module" + dot + "shift + echo tone: wow";
        else
            hint = "encoders arrive with this page" + dot + "use the panel on the right";
        softKeys.setHint (hint);
    }

    void MainComponent::timerCallback()
    {
        for (auto* c : cells)
            c->repaint();
        deck.refreshEncoders();
        softKeys.repaint();
        lane.repaint();
    }

    juce::Component* MainComponent::legacyPanel (AppMode m) noexcept
    {
        switch (m)
        {
            case AppMode::backups: return &backupsPanel;
            case AppMode::play:
            case AppMode::chop:
            case AppMode::edit:
            case AppMode::load:
            case AppMode::capture:
            case AppMode::rack:
            default:               return nullptr;
        }
    }

    void MainComponent::layoutBody()
    {
        const auto body = toInt (frame.body);
        const auto mode = model.mode();
        dock = {};
        if (keymap::showsChop (mode))
        {
            chopPage.setBounds (body);
            chopPage.setFrame (frame);
            return;
        }
        if (mode == AppMode::edit)
        {
            editPage.setBounds (body);
            editPage.setFrame (frame);
            return;
        }
        if (mode == AppMode::load)
        {
            loadPage.setBounds (body);
            loadPage.setFrame (frame);
            return;
        }
        if (mode == AppMode::capture)
        {
            capturePage.setBounds (body);
            capturePage.setFrame (frame);
            return;
        }
        if (mode == AppMode::rack)
        {
            rackPage.setBounds (body);
            rackPage.setFrame (frame);
            return;
        }
        if (keymap::showsStage (mode))
        {
            playPage.setBounds (body);
            playPage.setFrame (frame);
           #if JUCE_DEBUG
            // H6: no JUCE child may overlap the GL child window.
            if (stage.isVisible())
            {
                const auto glRect = stage.getBounds() + playPage.stageFrame().getPosition() + playPage.getPosition();
                for (auto* c : getChildren())
                    if (c != &playPage && c->isVisible() && c != &tooltips)
                        jassert (! c->getBounds().intersects (glRect));
            }
           #endif
            return;
        }
        // Today's panels keep their width (37% of the window, 520-640; RACK
        // 420), docked at the right of the body beside the honest plate.
        auto rest = body;
        const int width = mode == AppMode::rack ? 420 : juce::jlimit (520, 640, (int) ((float) getWidth() * 0.37f));
        dock = rest.removeFromRight (juce::jmin (width, body.getWidth() - 320));
        if (auto* panel = legacyPanel (mode))
            panel->setBounds (dock.withTrimmedLeft (1));
        rest.removeFromRight (8);
        plate.setBounds (rest);
        repaint (dock);
    }

    void MainComponent::resized()
    {
        frame = layout::frame ((float) getWidth(), (float) getHeight(), physicalScale());
        statusStrip.setBounds (toInt (frame.strip));
        deck.setBounds (toInt (frame.deck));
        deck.setFrame (frame);
        lane.setBounds (toInt (frame.lane));
        lane.setCompact (frame.density != layout::Density::regular);
        const auto size = frame.density == layout::Density::regular ? CellSize::regular
                        : frame.density == layout::Density::compact ? CellSize::compact : CellSize::ultra;
        for (int i = 0; i < 4; ++i)
        {
            cells[i]->setSize (size);
            cells[i]->setBounds (toInt (frame.cells[(size_t) i]));
        }
        softKeys.setBounds (toInt (frame.softKeys));
        std::array<float, 4> centres {};
        for (size_t i = 0; i < 4; ++i)
            centres[i] = frame.encoderX[i] - frame.softKeys.x;
        softKeys.setCentres (centres);
        softKeys.setPillSize (frame.pillW, frame.pillH);
        softKeys.setHintWidth (frame.lane.w);
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
        // The legacy dock's own ground (the old rail drew it).
        if (! dock.isEmpty())
        {
            g.setColour (legacyColours().paperHigh);
            g.fillRect (dock);
            paint::hairlineV (g, (float) dock.getX(), (float) dock.getY(), (float) dock.getBottom(), t.scr.rule);
        }
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
            // Esc: leave CHOP to EDIT; in PLAY, collapse or open the rail.
            if (model.mode() == AppMode::chop)
            {
                model.editor().closeChop();
                return true;
            }
            if (model.mode() == AppMode::play)
            {
                playPage.setRailOpen (! playPage.isRailOpen());
                return true;
            }
        }
        return false;
    }
}
