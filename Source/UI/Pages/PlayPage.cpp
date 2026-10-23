#include "UI/Pages/PlayPage.h"
#include "Core/Ep40MidiMapping.h"
#include "Core/GrabMath.h"
#include "UI/Design/Numerals.h"
#include "UI/Design/Paint.h"
#include "UI/Design/Pictograms.h"
#include "UI/Design/Primitives/ParamStrip.h"

namespace halo
{
    namespace
    {
        const juce::String dot = juce::String::fromUTF8 (" \xc2\xb7 ");
        const juce::String enDash = juce::String::charToString ((juce::juce_wchar) 0x2013);
        const juce::String minus = juce::String::charToString ((juce::juce_wchar) 0x2212);
        const juce::String arrow = juce::String::charToString ((juce::juce_wchar) 0x2192);
        const juce::String ep = "ep" + juce::String::charToString ((juce::juce_wchar) 0x2013) + "40";

        juce::String dayStamp (juce::Time t)
        {
            return juce::String (t.getDayOfMonth()) + " " + t.getMonthName (true).toLowerCase() + " " + t.formatted ("%H:%M");
        }

        juce::String signedDb (double db)
        {
            const int v = juce::roundToInt (db);
            return (v < 0 ? minus : juce::String()) + juce::String (std::abs (v));
        }

        // A section title: label left, optional dim text right, hairline under.
        void title (juce::Graphics& g, juce::Rectangle<float> row, const juce::String& left, juce::Colour leftColour,
                    const juce::String& right = {})
        {
            const auto& t = theme();
            paint::text (g, left, row, HaloFonts::font (Role::label), leftColour);
            if (right.isNotEmpty())
                paint::text (g, right, row, HaloFonts::font (Role::ui, 11.5f), t.scr.dim, juce::Justification::centredRight);
        }
    }

    //==========================================================================
    void PlayPage::ToggleKey::paintButton (juce::Graphics& g, bool highlighted, bool)
    {
        const auto& t = theme();
        auto r = getLocalBounds().toFloat();
        const auto state = ! isEnabled() ? paint::KeyState::disabled : isOn ? paint::KeyState::selected : paint::KeyState::normal;
        paint::stepKey (g, r, {}, state, false, highlighted && isEnabled(), false);
        const auto font = HaloFonts::face (HaloFonts::Face::interMedium, 12.5f);
        const auto ink = state == paint::KeyState::selected ? juce::Colour (0xff000005)
                       : state == paint::KeyState::disabled ? t.scr.ramp[5] : t.scr.ink;
        auto content = r.withTrimmedBottom (2.0f);
        if (lamp)
        {
            const float w = paint::textWidth (font, getButtonText()) + 14.0f;
            auto inner = content.withSizeKeepingCentre (w, content.getHeight());
            paint::lamp (g, { inner.getX() + 4.0f, inner.getCentreY() },
                         isOn ? t.honest.danger : (isEnabled() ? t.honest.danger.withMultipliedBrightness (0.55f) : t.scr.frame), 7.0f);
            paint::text (g, getButtonText(), inner.withTrimmedLeft (14.0f), font, ink);
        }
        else
        {
            paint::text (g, getButtonText(), content, font, ink, juce::Justification::centred);
        }
    }

    //==========================================================================
    PlayPage::PlayPage (AppModel& m, juce::Component& stage) : model (m), frameView (m, stage)
    {
        setOpaque (false);
        addAndMakeVisible (frameView);
        for (auto* c : std::initializer_list<juce::Component*> { &arcL, &arcR, &monitorKey, &recKey, &outKey, &grabKey,
                                                                 &showTake, &showGrab, &grabLengths })
            addAndMakeVisible (c);

        monitorKey.onClick = [this] { model.toggleMonitor(); };
        recKey.onClick = [this] { model.toggleRecording(); };
        outKey.onClick = [this] { showOutputMenu(); };
        outKey.setTooltip ("Where the monitor plays. EP-40 outputs are hidden (they would feed back).");
        grabKey.onClick = [this] { model.grabNow(); };
        grabKey.setTooltip ("GRAB (ctrl g): keep the last bars (with a clock) or seconds");
        showTake.onClick = [this] { if (const auto& t = model.lastRecording()) t->file.revealToUser(); };
        showGrab.onClick = [this] { if (const auto& t = model.lastGrab()) t->file.revealToUser(); };
        grabLengths.onSelect = [this] (int i)
        {
            model.setGrabLength (model.grabUsesBars() ? grab::barChoices[(size_t) i] : grab::secondsChoices[(size_t) i]);
        };

        model.addListener (this);
        refreshControls();
        startTimerHz (30);
    }

    PlayPage::~PlayPage()
    {
        model.removeListener (this);
    }

    juce::Rectangle<int> PlayPage::local (const layout::Rect& r) const { return localF (r).toNearestInt(); }

    juce::Rectangle<float> PlayPage::localF (const layout::Rect& r) const
    {
        return { r.x - frame.body.x, r.y - frame.body.y, r.w, r.h };
    }

    void PlayPage::setFrame (const layout::Frame& f)
    {
        frame = f;
        resized();
    }

    void PlayPage::setRailOpen (bool open)
    {
        if (open == railOpen)
            return;
        railOpen = open;
        resized();
        repaint();
    }

    void PlayPage::statusChanged()
    {
        refreshControls();
        repaint();
    }

    void PlayPage::displayChanged (const DisplayState& d)
    {
        const bool live = d.feedMode == FeedMode::live;
        if (live != lastPadLive || live)
        {
            lastPadLive = live;
            repaint (local (play.lastPad));
        }
    }

    void PlayPage::refreshControls()
    {
        auto& engine = model.monitor();
        const bool running = engine.isRunning();
        monitorKey.setOn (running);
        monitorKey.setEnabled (running || engine.ep40InputName().isNotEmpty());
        recKey.setOn (engine.isRecording());
        recKey.setEnabled (running);
        recKey.setTooltip (running ? juce::String ("record (ctrl r)") : juce::String ("rec needs monitor on (ctrl m)"));
        grabKey.setEnabled (running && ! model.grabber().isBusy());

        const bool bars = model.grabUsesBars();
        juce::StringArray items;
        int selected = 1;
        for (int i = 0; i < 3; ++i)
        {
            const int v = bars ? grab::barChoices[(size_t) i] : grab::secondsChoices[(size_t) i];
            items.add (juce::String (v));
            if (v == (bars ? model.grabber().bars : model.grabber().seconds))
                selected = i;
        }
        grabLengths.setItems (items);
        grabLengths.setSelected (selected);

        juce::String out = engine.chosenOutputName().isNotEmpty() ? engine.chosenOutputName() : juce::String ("windows default");
        if (running)
            out = engine.route().outputName;
        outKey.setLabel (paint::middleEllipsis (out.toLowerCase(), HaloFonts::face (HaloFonts::Face::interMedium, 13.5f), 150.0f));
        showTake.setVisible (railOpen && model.lastRecording().has_value());
        showGrab.setVisible (railOpen && model.lastGrab().has_value());
        resized();
    }

    void PlayPage::showOutputMenu()
    {
        auto& engine = model.monitor();
        outputs.clear();
        int hidden = 0;
        for (const auto& name : engine.outputNames())
        {
            if (MonitorEngine::isEp40Name (name)) // never offer the feedback route
                ++hidden;
            else
                outputs.add (name);
        }
        juce::PopupMenu menu;
        const auto fallback = engine.defaultOutputName();
        menu.addItem (1, "Windows default" + (fallback.isNotEmpty() ? " (" + fallback + ")" : juce::String()), true,
                      engine.chosenOutputName().isEmpty());
        for (int i = 0; i < outputs.size(); ++i)
            menu.addItem (i + 2, outputs[i], true, outputs[i] == engine.chosenOutputName());
        if (hidden > 0)
        {
            menu.addSeparator();
            menu.addItem (-1, ep + " outputs hidden (feedback)", false, false);
        }
        menu.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (&outKey),
            [this, safe = juce::Component::SafePointer<PlayPage> (this)] (int result)
            {
                if (safe == nullptr || result <= 0)
                    return;
                model.setMonitorOutput (result == 1 ? juce::String() : outputs[result - 2]);
                refreshControls();
            });
    }

    void PlayPage::timerCallback()
    {
        auto& engine = model.monitor();
        const bool running = engine.isRunning();
        const auto& lv = model.meterLevels();
        arcL.setRunning (running);
        arcR.setRunning (running);
        if (running)
        {
            arcL.setLevel (lv.peakL, model.limitLit (0));
            arcR.setLevel (lv.peakR, model.limitLit (1));
            repaint (local (play.monitor));
            if (! railOpen)
                repaint (spineArea);
        }
        // The beat ruler and the tape lane follow the clock.
        const int step = (model.clockTicks() % 96) / 6;
        if (step != lastStep)
        {
            lastStep = step;
            if (onLaneChanged)
                onLaneChanged();
        }
        if (running)
            repaint (local (play.grab));
        if (engine.isRecording())
            repaint (local (play.monitor));
    }

    void PlayPage::resized()
    {
        if (frame.width <= 0)
            return;
        play = layout::playLayout (frame, railOpen);
        frameView.setBounds (local (play.stage));
        spineArea = railOpen ? juce::Rectangle<int>() : local (play.spine);

        for (auto* c : std::initializer_list<juce::Component*> { &arcL, &arcR, &monitorKey, &recKey, &outKey, &grabKey, &grabLengths })
            c->setVisible (railOpen);
        if (! railOpen)
        {
            showTake.setVisible (false);
            showGrab.setVisible (false);
            return;
        }
        const bool regular = frame.density == layout::Density::regular;

        // MONITOR: header, arcs + LEVEL, keys, info, last take.
        {
            auto r = localF (play.monitor);
            r.removeFromTop (22.0f);
            const float keysH = regular ? 30.0f : 26.0f;
            auto take = r.removeFromBottom (18.0f);
            r.removeFromBottom (2.0f);
            r.removeFromBottom (18.0f); // info line
            r.removeFromBottom (4.0f);
            auto keys = r.removeFromBottom (keysH);
            r.removeFromBottom (4.0f);
            auto arcs = r.removeFromLeft (r.getWidth() * 0.58f);
            arcL.setBounds (arcs.removeFromLeft (arcs.getWidth() * 0.5f).toNearestInt());
            arcR.setBounds (arcs.toNearestInt());
            monitorKey.setBounds (keys.removeFromLeft (92.0f).toNearestInt());
            keys.removeFromLeft (6.0f);
            recKey.setBounds (keys.removeFromLeft (70.0f).toNearestInt());
            keys.removeFromLeft (10.0f);
            outKey.setBounds (keys.toNearestInt());
            showTake.setBounds (take.removeFromRight (48.0f).toNearestInt());
            showTake.setVisible (model.lastRecording().has_value());
        }
        // GRAB: header, chips + grab key, tape lane, last grab.
        {
            auto r = localF (play.grab);
            r.removeFromTop (22.0f);
            auto chips = r.removeFromTop (regular ? 30.0f : 26.0f);
            grabKey.setBounds (chips.removeFromRight (juce::jmin (130.0f, (float) grabKey.preferredWidth())).toNearestInt());
            chips.removeFromRight (12.0f);
            grabLengths.setBounds (chips.removeFromLeft (juce::jmin (170.0f, chips.getWidth())).toNearestInt());
            auto last = r.removeFromBottom (18.0f);
            showGrab.setBounds (last.removeFromRight (48.0f).toNearestInt());
            showGrab.setVisible (model.lastGrab().has_value());
        }
    }

    void PlayPage::mouseUp (const juce::MouseEvent& e)
    {
        if (! railOpen && spineArea.contains (e.getPosition()))
            setRailOpen (true);
        else if (railOpen && local (play.rail).withHeight (22).contains (e.getPosition()) && e.getNumberOfClicks() == 2)
            setRailOpen (false);
    }

    //==========================================================================
    void PlayPage::paint (juce::Graphics& g)
    {
        if (frame.width <= 0)
            return;
        if (! railOpen)
        {
            paintSpine (g, localF (play.spine));
            return;
        }
        const auto& t = theme();
        // Hairline between the stage and the rail, rules between sections.
        paint::hairlineV (g, localF (play.rail).getX() - 5.0f, localF (play.rail).getY(), localF (play.rail).getBottom(), t.scr.rule);
        paintLastPad (g, localF (play.lastPad));
        paintMonitor (g, localF (play.monitor));
        paintGrab (g, localF (play.grab));
        paintRack (g, localF (play.rack));
        paintEvents (g, localF (play.events));
        for (const auto* r : { &play.monitor, &play.grab, &play.rack, &play.events })
            paint::hairlineH (g, localF (*r).getX(), localF (*r).getRight(), localF (*r).getY() - 4.0f, t.scr.rule);
    }

    void PlayPage::paintLastPad (juce::Graphics& g, juce::Rectangle<float> r)
    {
        const auto& t = theme();
        const auto& d = model.display();
        const bool live = d.feedMode == FeedMode::live;
        auto head = r.removeFromTop (18.0f);
        title (g, head, "LAST PAD", t.scr.dim);
        float tx = head.getX() + paint::textWidth (HaloFonts::font (Role::label), "LAST PAD") + 10.0f;
        juce::ignoreUnused (tx);
        if (live)
            paint::text (g, "listening on all 16 channels", head, HaloFonts::font (Role::ui, 11.5f), t.scr.dim, juce::Justification::centredRight);
        else
        {
            paint::hollowLamp (g, { head.getRight() - 84.0f, head.getCentreY() }, t.scr.dim, 7.0f);
            paint::text (g, "no live feed", head, HaloFonts::font (Role::ui, 11.5f), t.scr.dim, juce::Justification::centredRight);
        }
        r.removeFromTop (4.0f);
        const bool held = live && d.activePadIndex && d.activeGroup;
        juce::String pad = enDash, note = enDash, vel = enDash, ch = enDash;
        if (held)
        {
            juce::String key (ep40::gridLabels[(size_t) *d.activePadIndex].data());
            key = key == "ENTER" ? "ent" : key;
            pad = juce::String (ep40::groupLabels[(size_t) *d.activeGroup].data()).toLowerCase() + " " + key;
            note = juce::String (d.value);
            vel = juce::String (juce::roundToInt (d.velocity * 127.0f));
        }
        if (live)
            ch = "all";
        const float gap = 6.0f, w = (r.getWidth() - 3.0f * gap) / 4.0f;
        const bool regular = frame.density == layout::Density::regular;
        const juce::String labels[] { "pad", "note", "velocity", "channel" };
        const juce::String values[] { pad, note, vel, ch };
        for (int i = 0; i < 4; ++i)
        {
            const auto cell = juce::Rectangle<float> (r.getX() + (float) i * (w + gap), r.getY(), w, r.getHeight());
            const bool digits = i == 1 || i == 2;
            // Compact cells are 44 px: a smaller value keeps clear of the label.
            readout::paint (g, cell, 1 + i * 2, labels[i], values[i], digits && held, regular ? 26.0f : 16.0f);
        }
    }

    void PlayPage::paintMonitor (juce::Graphics& g, juce::Rectangle<float> r)
    {
        const auto& t = theme();
        auto& engine = model.monitor();
        const bool running = engine.isRunning();
        auto head = r.removeFromTop (18.0f);
        juce::String facts = "limiter " + minus + "1 dbfs";
        if (running)
        {
            const auto& route = engine.route();
            facts = juce::String (route.sampleRate / 1000.0, 1) + " khz" + dot + juce::String (route.bufferSize) + " fr" + dot + "~"
                  + juce::String (juce::roundToInt (route.latencyMs)) + " ms" + dot + facts;
        }
        title (g, head, "MONITOR", t.scr.dim, facts);
        if (running)
            paint::tag (g, { head.getX() + paint::textWidth (HaloFonts::font (Role::label), "MONITOR") + 10.0f, head.getY() }, "ON",
                        paint::TagStyle::filled, {}, {}, 16.0f);
        r.removeFromTop (4.0f);

        // LEVEL block to the right of the arcs: encoder 1 owns it (blue).
        const auto blue = t.encoder (0).line;
        const bool regular = frame.density == layout::Density::regular;
        auto take = r.removeFromBottom (18.0f);
        r.removeFromBottom (2.0f);
        auto info = r.removeFromBottom (18.0f);
        r.removeFromBottom (4.0f);
        r.removeFromBottom (regular ? 30.0f : 26.0f);
        r.removeFromBottom (4.0f);
        auto level = r.withTrimmedLeft (r.getWidth() * 0.58f + 8.0f);
        auto lrow = level.removeFromTop (14.0f);
        paint::lamp (g, { lrow.getX() + 3.5f, lrow.getCentreY() }, blue, 7.0f);
        paint::text (g, "LEVEL", lrow.withTrimmedLeft (12.0f), HaloFonts::font (Role::label), blue);
        const float cap = regular ? numerals::L : numerals::M;
        const double db = model.monitorGainDb();
        numerals::drawWithUnit (g, signedDb (db), "db", level.getX(), level.getY() + 6.0f + cap, cap, blue, blue);
        gauges::ladder (g, { level.getX(), juce::jmin (level.getBottom() - 8.0f, level.getY() + cap + 14.0f), level.getWidth() - 4.0f, 8.0f }, 20,
                        juce::roundToInt ((db + 60.0) / 60.0 * 20.0), blue);

        // Info line, in priority order (plan section 3.5).
        const auto font = HaloFonts::font (Role::ui, 12.0f);
        auto blockerChip = [&] (const juce::String& text)
        {
            g.setColour (t.honest.danger);
            g.fillRoundedRectangle (info, 3.0f);
            pictograms::draw (g, Pict::triangle, { info.getX() + 5.0f, info.getCentreY() - 6.0f, 12.0f, 12.0f }, juce::Colours::white, 1.3f);
            paint::text (g, paint::middleEllipsis (text, HaloFonts::font (Role::bodyStrong, 12.0f), info.getWidth() - 26.0f),
                         info.withTrimmedLeft (22.0f), HaloFonts::font (Role::bodyStrong, 12.0f), juce::Colours::white);
        };
        const auto dropped = engine.recordingDroppedSamples();
        if (engine.isRecording() && dropped > 0 && engine.route().sampleRate > 0.0)
        {
            blockerChip ("warning: " + juce::String (juce::roundToInt ((double) dropped / engine.route().sampleRate * 1000.0))
                         + " ms of audio was dropped (disk too slow)");
        }
        else if (model.grabMessage().isNotEmpty())
        {
            g.setColour (t.honest.info);
            g.drawRoundedRectangle (info.reduced (0.75f), 4.0f, 1.5f);
            pictograms::draw (g, Pict::info, { info.getX() + 5.0f, info.getCentreY() - 6.0f, 12.0f, 12.0f }, t.honest.info, 1.3f);
            paint::text (g, model.grabMessage().toLowerCase(), info.withTrimmedLeft (22.0f), font, t.scr.ink);
        }
        else if (engine.isRecording())
        {
            const int s = (int) engine.recordingSeconds();
            const auto tag = paint::tag (g, { info.getX(), info.getY() }, "REC " + juce::String (s / 60).paddedLeft ('0', 2) + ":"
                                                + juce::String (s % 60).paddedLeft ('0', 2), paint::TagStyle::danger, {}, {}, 18.0f);
            paint::text (g, "recording the raw " + ep + " input", info.withTrimmedLeft (tag.getWidth() + 8.0f), font, t.scr.ink);
        }
        else if (engine.currentState() == MonitorEngine::State::failed && engine.lastError().isNotEmpty())
        {
            blockerChip (engine.lastError());
        }
        else if (! running && model.monitorStoppedByDisconnect())
        {
            blockerChip ("monitor stopped: " + ep + " disconnected");
        }
        else if (! running && engine.ep40InputName().isEmpty())
        {
            blockerChip ("no " + ep + " audio input found");
        }
        else
        {
            paint::text (g, running ? "monitoring" + dot + "out level " + signedDb (db) + " db" : juce::String ("off"), info, font, t.scr.dim);
        }

        // Last take.
        if (const auto& takeInfo = model.lastRecording())
            paint::text (g, paint::middleEllipsis ("last take" + dot + dayStamp (model.lastRecordingTime()) + dot
                                                       + juce::String (takeInfo->seconds, 1) + " s", font, take.getWidth() - 56.0f),
                         take, font, t.scr.ink);
        else
            paint::text (g, "no takes yet" + dot + "ctrl r records while monitoring", take, font, t.scr.dim);
    }

    void PlayPage::paintGrab (juce::Graphics& g, juce::Rectangle<float> r)
    {
        const auto& t = theme();
        const auto ochre = t.encoder (1).line;
        const bool bars = model.grabUsesBars();
        auto head = r.removeFromTop (18.0f);
        paint::lamp (g, { head.getX() + 3.5f, head.getCentreY() }, ochre, 7.0f);
        paint::text (g, "GRAB", head.withTrimmedLeft (12.0f), HaloFonts::font (Role::label), ochre);
        paint::text (g, bars ? "clock running " + arrow + " bars" : "no clock " + arrow + " seconds", head,
                     HaloFonts::font (Role::ui, 11.5f), t.scr.dim, juce::Justification::centredRight);
        r.removeFromTop (4.0f);
        r.removeFromTop (frame.density == layout::Density::regular ? 30.0f : 26.0f); // chips + key
        auto last = r.removeFromBottom (18.0f);
        r.removeFromBottom (2.0f);

        // The tape lane: 30 s of history, the bracket = exactly what GRAB takes.
        auto& engine = model.monitor();
        double windowSeconds = 0.0;
        if (bars)
        {
            if (const auto bpm = model.clockBpm())
                windowSeconds = model.grabber().bars * 4.0 * 60.0 / *bpm;
        }
        else
        {
            windowSeconds = model.grabber().seconds;
        }
        double buffered = 0.0;
        if (const auto history = engine.history(); history != nullptr && engine.route().sampleRate > 0.0)
            buffered = juce::jmin (30.0, (double) (history->nowFrame() - history->oldestResidentFrame()) / engine.route().sampleRate);
        auto laneArea = r.withTrimmedTop (2.0f);
        if (laneArea.getHeight() >= 30.0f)
        {
            const auto print = HaloFonts::font (Role::print, 10.5f);
            auto captions = laneArea.removeFromBottom (12.0f);
            paint::text (g, "history 30 s", captions, print, t.scr.dim);
            paint::text (g, "now", captions, print, t.scr.dim, juce::Justification::centredRight);
        }
        gauges::tapeLane (g, laneArea.withTrimmedTop (2.0f).withTrimmedRight (2.0f), 30.0,
                          buffered, engine.isRunning() ? juce::jmin (30.0, windowSeconds) : 0.0, ochre);

        const auto font = HaloFonts::font (Role::ui, 12.0f);
        if (const auto& g2 = model.lastGrab())
        {
            juce::String line = "last grab" + dot + dayStamp (model.lastGrabTime());
            if (model.lastGrabBars() > 0)
                line << dot << model.lastGrabBars() << " bars";
            line << dot << juce::String (g2->seconds, 1) << " s";
            paint::text (g, paint::middleEllipsis (line, font, last.getWidth() - 56.0f), last, font, t.scr.ink);
        }
        else
        {
            paint::text (g, engine.isRunning() ? "ctrl g keeps what you just played" : "grab needs monitor on (ctrl m)", last, font, t.scr.dim);
        }
    }

    void PlayPage::paintRack (juce::Graphics& g, juce::Rectangle<float> r)
    {
        const auto& t = theme();
        auto& p = model.rack().parameters();
        const bool engaged = p.engaged.load();
        const auto orange = t.encoder (3).line;
        const juce::String mix = juce::String (juce::roundToInt (p.echoMix.load() * 100.0f));

        struct Module { const char* name; bool on; };
        const Module modules[] { { "echo", p.echoOn.load() }, { "spring", p.springOn.load() }, { "sweep", p.sweepOn.load() }, { "low", p.lowOn.load() } };

        if (frame.density != layout::Density::regular)
        {
            // One line: lamp + rack engaged · echo spring · mix 35.
            juce::String on;
            for (const auto& m : modules)
                if (m.on)
                    on << (on.isEmpty() ? "" : " ") << m.name;
            auto row = r.withSizeKeepingCentre (r.getWidth(), 18.0f);
            if (engaged) paint::lamp (g, { row.getX() + 3.5f, row.getCentreY() }, t.scr.hot, 7.0f);
            else         paint::hollowLamp (g, { row.getX() + 3.5f, row.getCentreY() }, t.scr.dim, 7.0f);
            paint::text (g, juce::String (engaged ? "rack engaged" : "rack bypassed") + dot + (on.isNotEmpty() ? on : juce::String ("all off")) + dot + "mix " + mix,
                         row.withTrimmedLeft (14.0f), HaloFonts::font (Role::ui, 12.0f), engaged ? t.scr.ink : t.scr.dim);
            paint::text (g, "ctrl 5", row, HaloFonts::font (Role::ui, 11.5f), t.scr.dim, juce::Justification::centredRight);
            return;
        }

        auto head = r.removeFromTop (18.0f);
        paint::text (g, "DUB RACK", head, HaloFonts::font (Role::label), t.scr.dim);
        const float tagX = head.getX() + paint::textWidth (HaloFonts::font (Role::label), "DUB RACK") + 10.0f;
        paint::tag (g, { tagX, head.getY() }, engaged ? "ENGAGED" : "BYPASSED", engaged ? paint::TagStyle::filled : paint::TagStyle::outline, {}, {}, 16.0f);
        const float right = head.getRight();
        numerals::draw (g, mix, right - numerals::width (mix, numerals::S), head.getBottom() + 2.0f, numerals::S, orange);
        paint::text (g, "mix", { right - numerals::width (mix, numerals::S) - 30.0f, head.getY(), 26.0f, head.getHeight() },
                     HaloFonts::font (Role::ui, 11.5f), orange, juce::Justification::centredRight);
        r.removeFromTop (6.0f);

        // The chain: in -> echo -> spring -> sweep -> low -> out, 50 x 44 tiles.
        const float tileW = 50.0f, tileH = 40.0f;
        const float gapW = (r.getWidth() - 6.0f * tileW) / 5.0f;
        const auto print = HaloFonts::font (Role::print, 10.5f);
        for (int i = 0; i < 6; ++i)
        {
            const auto tile = juce::Rectangle<float> (r.getX() + (float) i * (tileW + gapW), r.getY(), tileW, tileH);
            const bool module = i >= 1 && i <= 4;
            const bool on = ! module || modules[i - 1].on;
            const auto line = on ? t.scr.ink : t.scr.ramp[3];
            if (module && ! on)
                paint::dashedRect (g, tile.reduced (0.75f), t.scr.ramp[3], 1.2f, 3.0f, 3.0f, 4.0f);
            else
            {
                g.setColour (t.scr.ink.withAlpha (0.8f));
                g.drawRoundedRectangle (tile.reduced (0.75f), 4.0f, 1.2f);
            }
            auto art = tile.reduced (10.0f, 8.0f);
            const auto c = art.getCentre();
            switch (i)
            {
                case 0: pictograms::draw (g, Pict::arrowRight, art, line, 1.4f); break;
                case 1: // echo: rings with an orange dot
                    g.setColour (line);
                    for (float rad : { 4.0f, 8.0f, 11.0f })
                        g.drawEllipse (juce::Rectangle<float> (rad * 2.0f, rad * 2.0f).withCentre (c), 1.1f);
                    paint::lamp (g, c, orange, 5.0f);
                    break;
                case 2: // spring: an ochre coil
                {
                    juce::Path coil;
                    coil.startNewSubPath (art.getX(), c.y);
                    for (int k = 0; k <= 48; ++k)
                    {
                        const float u = (float) k / 48.0f;
                        coil.lineTo (art.getX() + art.getWidth() * u, c.y + std::sin (u * juce::MathConstants<float>::twoPi * 6.0f) * art.getHeight() * 0.45f);
                    }
                    g.setColour (on ? t.encoder (1).line : line);
                    g.strokePath (coil, juce::PathStrokeType (1.2f));
                    break;
                }
                case 3: // sweep: a resonant filter curve
                {
                    juce::Path curve;
                    curve.startNewSubPath (art.getX(), art.getBottom() - 4.0f);
                    curve.lineTo (art.getX() + art.getWidth() * 0.45f, art.getBottom() - 4.0f);
                    curve.quadraticTo (art.getX() + art.getWidth() * 0.62f, art.getY() - 2.0f, art.getX() + art.getWidth() * 0.7f, art.getBottom() - 6.0f);
                    curve.lineTo (art.getRight(), art.getBottom());
                    g.setColour (line);
                    g.strokePath (curve, juce::PathStrokeType (1.3f));
                    break;
                }
                case 4: // low: a sine
                {
                    juce::Path sine;
                    for (int k = 0; k <= 32; ++k)
                    {
                        const float u = (float) k / 32.0f;
                        const auto pt = juce::Point<float> (art.getX() + art.getWidth() * u, c.y - std::sin (u * juce::MathConstants<float>::twoPi) * art.getHeight() * 0.45f);
                        if (k == 0) sine.startNewSubPath (pt); else sine.lineTo (pt);
                    }
                    g.setColour (line);
                    g.strokePath (sine, juce::PathStrokeType (1.4f));
                    break;
                }
                default: pictograms::draw (g, Pict::speaker, art, line, 1.4f); break;
            }
            const juce::String names[] { "in", "echo", "spring", "sweep", "low", "out" };
            paint::text (g, names[i] + (module && ! on ? dot + "off" : juce::String()),
                         { tile.getX() - 12.0f, tile.getBottom() + 1.0f, tileW + 24.0f, 13.0f }, print, on ? t.scr.ink : t.scr.dim,
                         juce::Justification::centredTop);
            if (i < 5)
                pictograms::draw (g, Pict::arrowRight, { tile.getRight() + gapW * 0.5f - 4.0f, tile.getCentreY() - 4.0f, 8.0f, 8.0f },
                                  t.scr.mute, 1.2f);
        }
    }

    void PlayPage::paintEvents (juce::Graphics& g, juce::Rectangle<float> r)
    {
        const auto& t = theme();
        auto head = r.removeFromTop (18.0f);
        // The MIDI port's name (a mock run shows a plain "midi in": owner decision, no mock marking).
        title (g, head, "EVENTS", t.scr.dim, model.isMock() ? juce::String ("midi in")
                                             : model.deviceName().isNotEmpty() ? model.deviceName().toLowerCase() : juce::String ("no midi port"));
        const auto& log = model.eventLog();
        const auto mono = HaloFonts::font (Role::mono);
        const int lines = juce::jmax (0, (int) (r.getHeight() / 18.0f));
        const int first = juce::jmax (0, (int) log.size() - lines);
        int age = (int) log.size() - first - 1;
        for (int i = first; i < (int) log.size(); ++i, --age)
        {
            auto row = r.removeFromTop (18.0f);
            const auto& line = log[(size_t) i];
            const auto time = line.substring (0, 8);
            auto text = line.substring (8).trimStart();
            const bool isError = text.containsIgnoreCase ("error") || text.containsIgnoreCase ("failed") || text.startsWithIgnoreCase ("warning")
                              || text.startsWithIgnoreCase ("can't") || text.containsIgnoreCase ("in use by another");
            // Newest in ink; older lines step down to ramp6 then ramp5 (never dimmer).
            const auto ink = age == 0 ? t.scr.ink : age == 1 ? t.scr.ramp[6] : t.scr.ramp[5];
            paint::text (g, time, row.removeFromLeft (64.0f), mono, t.scr.ramp[5]);
            if (isError)
            {
                g.setColour (t.honest.danger);
                g.fillRect (row.removeFromLeft (8.0f).withSizeKeepingCentre (6.0f, 6.0f));
                row.removeFromLeft (4.0f);
            }
            paint::text (g, paint::middleEllipsis (text.toLowerCase(), mono, row.getWidth()), row, mono, ink);
        }
    }

    void PlayPage::paintSpine (juce::Graphics& g, juce::Rectangle<float> r)
    {
        const auto& t = theme();
        g.setColour (t.scr.panel);
        g.fillRoundedRectangle (r, 4.0f);
        {
            juce::Graphics::ScopedSaveState save (g);
            g.addTransform (juce::AffineTransform::rotation (juce::MathConstants<float>::halfPi, r.getCentreX(), r.getCentreY()));
            paint::text (g, "MONITOR", juce::Rectangle<float> (140.0f, 16.0f).withCentre (r.getCentre().translated (0.0f, 0.0f)),
                         HaloFonts::font (Role::label), t.scr.dim, juce::Justification::centred);
        }
        const auto& lv = model.meterLevels();
        auto litFor = [] (float db) { return std::isfinite (db) ? juce::jlimit (0, 12, (int) std::floor ((db + 40.0f) / 40.0f * 12.0f)) : 0; };
        auto ladders = juce::Rectangle<float> (r.getCentreX() - 8.0f, r.getBottom() - 90.0f, 6.0f, 80.0f);
        gauges::ladder (g, ladders, 12, litFor (lv.peakL), t.scr.ink, true);
        gauges::ladder (g, ladders.translated (10.0f, 0.0f), 12, litFor (lv.peakR), t.scr.ink, true);
        paint::text (g, "out", { r.getX(), r.getBottom() - 10.0f, r.getWidth(), 10.0f }, HaloFonts::font (Role::print, 10.5f), t.scr.dim,
                     juce::Justification::centred);
    }

    //==========================================================================
    void PlayPage::paintLane (juce::Graphics& g, juce::Rectangle<float> r, bool compact) const
    {
        const auto& t = theme();
        const auto bpm = model.clockBpm();
        const auto& d = model.display();
        const bool playing = bpm.has_value() && d.isPlaying;
        const float cap = compact ? numerals::M : numerals::L;

        auto left = r.removeFromLeft (compact ? 176.0f : 220.0f);
        auto label = left.removeFromTop (14.0f);
        paint::text (g, "CLOCK", label, HaloFonts::font (Role::label), t.scr.dim);
        pictograms::draw (g, playing ? Pict::play : Pict::stop, { label.getX() + 54.0f, label.getY() + 1.0f, 11.0f, 11.0f },
                          playing ? t.scr.ink : t.scr.dim, 1.3f, playing);
        const float base = left.getY() + 6.0f + cap;
        if (bpm)
            numerals::drawWithUnit (g, juce::String (*bpm, 1), "bpm", left.getX(), base, cap, t.scr.ink, t.scr.dim);
        else
            numerals::drawWithUnit (g, enDash, "bpm", left.getX(), base, cap, t.scr.dim, t.scr.dim);

        const int ticks = model.clockTicks();
        const int bar = ticks > 0 ? (ticks - 1) / 96 + 1 : 0;
        const int beat = ticks > 0 ? ((ticks - 1) % 96) / 24 + 1 : 0;
        auto status = juce::Rectangle<float> (left.getX(), base + 4.0f, left.getWidth(), 14.0f);
        paint::text (g, ! bpm ? juce::String ("no clock") : playing ? "playing" + dot + "bar " + juce::String (bar) + "." + juce::String (beat)
                                                                    : juce::String ("stopped"),
                     status, HaloFonts::font (Role::ui, 11.5f), t.scr.dim);

        // The 16-step paired beat ruler; the current step lit white.
        r.removeFromLeft (12.0f);
        auto ruler = r.removeFromTop (compact ? 30.0f : 40.0f).withTrimmedTop (compact ? 6.0f : 12.0f);
        const int current = playing ? ((ticks - 1) % 96) / 6 : -1;
        const float sw = (ruler.getWidth() - 15.0f * 3.0f) / 16.0f;
        for (int i = 0; i < 16; ++i)
        {
            auto s = juce::Rectangle<float> (ruler.getX() + (float) i * (sw + 3.0f), ruler.getY(), sw, ruler.getHeight());
            if (! bpm)
            {
                g.setColour (t.scr.frame);
                g.drawRoundedRectangle (s.reduced (0.6f), 2.0f, 1.0f);
                continue;
            }
            g.setColour (i == current ? t.scr.hot : ((i / 4) % 2 == 0 ? t.scr.stepA.brighter (0.25f) : t.scr.stepB.brighter (0.15f)));
            g.fillRoundedRectangle (s, 2.0f);
        }
        const auto print = HaloFonts::font (Role::print, 10.5f);
        for (int beatNo = 0; beatNo < 4; ++beatNo)
            paint::text (g, juce::String (beatNo + 1), { ruler.getX() + (float) beatNo * 4.0f * (sw + 3.0f), ruler.getBottom() + 1.0f, 20.0f, 12.0f },
                         print, t.scr.dim);
        auto hints = juce::Rectangle<float> (r.getX(), juce::jmax (ruler.getBottom() + 14.0f, r.getBottom() - 16.0f), r.getWidth(), 14.0f);
        paint::text (g, "ctrl g grab" + dot + "ctrl m monitor" + dot + "ctrl r rec", hints, HaloFonts::font (Role::ui, 11.0f), t.scr.dim);
    }
}
