#include "App/MainComponent.h"
#include "App/DevLog.h"
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

        for (auto* c : std::initializer_list<juce::Component*> { &statusStrip, &playPage, &deck, &lane, &softKeys })
            addAndMakeVisible (c);
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
            else if (model.mode() == AppMode::backups)
                backupsPage.paintLane (g, r, frame.density != layout::Density::regular);
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
        addChildComponent (backupsPage);
        rackPage.onFocusChanged = [this] { boundMode.reset(); updateBindings(); };
        // trim <-> shape latched on the page: the encoders swap pages.
        editPage.onPageChanged = [this] { boundMode.reset(); updateBindings(); };
        model.addListener (this);
        setWantsKeyboardFocus (true);
       #if HALO_HAS_STAGE_LOOK
        // The stage sits on the black glass (plan section 4.3): the dark look,
        // its backdrop exactly the glass so the two read as one surface.
        // HALO_STAGE_LOOK still picks a look for testing.
        if (juce::SystemStats::getEnvironmentVariable ("HALO_STAGE_LOOK", {}).isEmpty())
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
        // U12: the stage renders only in PLAY (hidden, its GL context detaches),
        // and hides under a confirm card (H6: nothing JUCE paints sits over it).
        stage.setVisible (play && ! cardOpen);
        for (auto m : { AppMode::play, AppMode::load, AppMode::edit, AppMode::capture, AppMode::rack, AppMode::backups, AppMode::chop })
            pageFor (m)->setVisible (m == mode);
        statusStrip.setMonitorOwnedByEncoder (play);
        updateBindings();
        // Each page takes the keys while it shows (not under a confirm card).
        if (auto* page = pageFor (mode); page != nullptr && ! play && ! cardOpen && ! page->hasKeyboardFocus (true))
            page->grabKeyboardFocus();
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
        else if (mode == AppMode::backups)
            bus.setBindings (backupsPage.bindings());
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
        else if (model.mode() == AppMode::backups)
            hint = "shelf picks a cassette" + dot + "the other cells only read";
        softKeys.setHint (hint);
    }

    void MainComponent::timerCallback()
    {
       #if JUCE_DEBUG
        // HALO_UI_STATS=1: every 10 s, the MotionClock frames ticked and the
        // backup folder scans run on the message thread (both 0 when idle).
        static const bool stats = juce::SystemStats::getEnvironmentVariable ("HALO_UI_STATS", {}) == "1";
        if (stats && ++statsTicks % 20 == 0)
        {
            const auto frames = motion.framesTicked();
            const int scans = model.backup().messageThreadScans();
            DevLog::line ("HALO_UI_STATS mode=" + juce::String (keymap::modeWord (model.mode())) + " vblank_frames_10s=" + juce::String (frames - statsFrames)
                          + " backup_scans_on_message_thread_10s=" + juce::String (scans - statsScans));
            statsFrames = frames;
            statsScans = scans;
        }
       #endif
        for (auto* c : cells)
            c->repaint();
        deck.refreshEncoders();
        softKeys.repaint();
        lane.repaint();
    }

    juce::Component* MainComponent::pageFor (AppMode m) noexcept
    {
        switch (m)
        {
            case AppMode::play:    return &playPage;
            case AppMode::load:    return &loadPage;
            case AppMode::edit:    return &editPage;
            case AppMode::capture: return &capturePage;
            case AppMode::rack:    return &rackPage;
            case AppMode::backups: return &backupsPage;
            case AppMode::chop:    return &chopPage;
            default:               return nullptr;
        }
    }

    void MainComponent::layoutBody()
    {
        const auto body = toInt (frame.body);
        switch (model.mode())
        {
            case AppMode::chop:    chopPage.setBounds (body);    chopPage.setFrame (frame);    return;
            case AppMode::edit:    editPage.setBounds (body);    editPage.setFrame (frame);    return;
            case AppMode::load:    loadPage.setBounds (body);    loadPage.setFrame (frame);    return;
            case AppMode::capture: capturePage.setBounds (body); capturePage.setFrame (frame); return;
            case AppMode::rack:    rackPage.setBounds (body);    rackPage.setFrame (frame);    return;
            case AppMode::backups: backupsPage.setBounds (body); backupsPage.setFrame (frame); return;
            case AppMode::play:
            default:
                break;
        }
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
