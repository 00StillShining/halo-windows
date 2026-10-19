#include "UI/RackPanel.h"
#include "UI/HaloColours.h"

namespace halo
{
    namespace
    {
        const auto& c = HaloColours::graphPaper;
        constexpr int knobSize = 56, knobLabel = 30, moduleTitle = 30;
        constexpr EchoDivision divisions[] { EchoDivision::eighth, EchoDivision::dottedEighth,
                                             EchoDivision::quarter, EchoDivision::tripletEighth };

        juce::String percent (double v) { return juce::String (juce::roundToInt (v * 100.0)) + "%"; }
    }

    RackPanel::Knob& RackPanel::addKnob (const juce::String& name, double min, double max, double value,
                                         std::function<void (double)> onChange, std::function<juce::String (double)> format)
    {
        auto* k = allKnobs.add (new Knob());
        k->name = name;
        k->format = format ? std::move (format) : std::function<juce::String (double)> (percent);
        k->slider.setRange (min, max);
        k->slider.setValue (value, juce::dontSendNotification);
        k->slider.setDoubleClickReturnValue (true, value);
        k->slider.setMouseDragSensitivity (180);
        k->slider.onValueChange = [this, k, onChange]
        {
            onChange (k->slider.getValue());
            repaint();
        };
        addAndMakeVisible (k->slider);
        return *k;
    }

    void RackPanel::addModule (Module& m, const juce::String& title, std::atomic<bool>& flag, std::vector<Knob*> knobs)
    {
        m.title = title;
        m.flag = &flag;
        m.knobs = std::move (knobs);
        m.onButton.onClick = [this, &m]
        {
            m.flag->store (! m.flag->load());
            refresh();
        };
        addAndMakeVisible (m.onButton);
    }

    RackPanel::RackPanel (AppModel& am) : model (am)
    {
        auto& p = model.monitor().rackParameters();
        auto& rack = model.rack();
        auto store = [] (std::atomic<float>& a) { return [&a] (double v) { a.store ((float) v); }; };

        echoTime = &addKnob ("TIME", 40.0, 1200.0, rack.freeMs, [this] (double v) { model.rack().setFreeMs (v); },
                             [] (double v) { return juce::String (juce::roundToInt (v)) + " ms"; });
        echoTime->slider.setSkewFactorFromMidPoint (300.0);
        addModule (echo, "TAPE ECHO", p.echoOn,
                   { echoTime,
                     &addKnob ("FEEDBACK", 0, 1, p.echoFeedback.load(), store (p.echoFeedback)),
                     &addKnob ("TONE", 0, 1, p.echoTone.load(), store (p.echoTone)),
                     &addKnob ("WOW", 0, 1, p.echoWow.load(), store (p.echoWow)),
                     &addKnob ("MIX", 0, 1, p.echoMix.load(), store (p.echoMix)) });
        addModule (spring, "SPRING", p.springOn,
                   { &addKnob ("MIX", 0, 1, p.springMix.load(), store (p.springMix)),
                     &addKnob ("DECAY", 0, 1, p.springDecay.load(), store (p.springDecay)),
                     &addKnob ("TONE", 0, 1, p.springTone.load(), store (p.springTone)) });
        addModule (sweep, "SWEEP", p.sweepOn,
                   { &addKnob ("SWEEP", 0, 1, p.sweepMacro.load(), store (p.sweepMacro),
                               [] (double v) { return v < 0.45 ? juce::String ("LP") : v > 0.55 ? juce::String ("HP") : juce::String ("BP"); }),
                     &addKnob ("RESO", 0, 1, p.sweepReso.load(), store (p.sweepReso)) });
        addModule (lowEnd, "LOW END", p.lowOn,
                   { &addKnob ("AMOUNT", 0, 1, p.lowAmount.load(), store (p.lowAmount),
                               [] (double v) { return "+" + juce::String (v * 6.0, 1) + " dB"; }) });

        engageButton.onClick = [&p, this] { p.engaged.store (! p.engaged.load()); refresh(); };
        printButton.onClick = [this]
        {
            model.monitor().setPrintFx (! model.monitor().isPrintingFx());
            refresh();
        };
        monitorButton.onClick = [this] { model.toggleMonitor(); };
        syncButton.onClick = [this] { model.rack().setSync (! model.rack().sync); refresh(); };
        tapButton.onClick = [this] { model.rack().tap (AppModel::now()); refresh(); };
        for (size_t i = 0; i < divisionButtons.size(); ++i)
        {
            divisionButtons[i].setButtonText (label (divisions[i]));
            divisionButtons[i].onClick = [this, i] { model.rack().setDivision (divisions[i]); refresh(); };
            addAndMakeVisible (divisionButtons[i]);
        }
        for (auto* b : { &engageButton, &printButton, &monitorButton, &syncButton, &tapButton })
            addAndMakeVisible (b);

        model.addListener (this);
        refresh();
        startTimerHz (4); // the clock tempo can change underneath
    }

    RackPanel::~RackPanel()
    {
        model.removeListener (this);
    }

    void RackPanel::refresh()
    {
        auto& p = model.monitor().rackParameters();
        auto& rack = model.rack();
        const bool running = model.monitor().isRunning();

        engageButton.setButtonText (p.engaged.load() ? "ENGAGED" : "ENGAGE");
        engageButton.setToggleState (p.engaged.load(), juce::dontSendNotification);
        printButton.setButtonText (model.monitor().isPrintingFx() ? "PRINT FX ON" : "PRINT FX");
        printButton.setToggleState (model.monitor().isPrintingFx(), juce::dontSendNotification);
        printButton.setEnabled (! model.monitor().isRecording());
        monitorButton.setButtonText (running ? "MONITOR ON" : "MONITOR");
        monitorButton.setToggleState (running, juce::dontSendNotification);

        for (auto* m : { &echo, &spring, &sweep, &lowEnd })
        {
            const bool on = m->flag->load();
            m->onButton.setButtonText (on ? "ON" : "BYPASSED");
            m->onButton.setToggleState (on, juce::dontSendNotification);
            for (auto* k : m->knobs)
                k->slider.setEnabled (on);
        }

        syncButton.setButtonText (rack.sync ? "SYNC" : "FREE");
        syncButton.setToggleState (rack.sync, juce::dontSendNotification);
        for (size_t i = 0; i < divisionButtons.size(); ++i)
        {
            divisionButtons[i].setVisible (rack.sync);
            divisionButtons[i].setToggleState (rack.division == divisions[i], juce::dontSendNotification);
        }
        // In SYNC the TIME knob shows (and can't fight) the tempo-derived time.
        echoTime->slider.setEnabled (echo.flag->load() && ! rack.sync);
        repaint();
    }

    void RackPanel::resized()
    {
        auto area = getLocalBounds().reduced (16, 12);
        headerArea = area.removeFromTop (18);
        area.removeFromTop (8);

        auto top = area.removeFromTop (36);
        const int third = top.getWidth() / 3;
        engageButton.setBounds (top.removeFromLeft (third).reduced (2, 2));
        printButton.setBounds (top.removeFromLeft (third).reduced (2, 2));
        monitorButton.setBounds (top.reduced (2, 2));
        noteArea = area.removeFromTop (22);

        auto layoutModule = [&] (Module& m, bool withTempo)
        {
            area.removeFromTop (6);
            auto title = area.removeFromTop (moduleTitle);
            m.onButton.setBounds (title.removeFromRight (104).reduced (0, 2));
            m.titleArea = title;

            if (withTempo)
            {
                auto row = area.removeFromTop (32);
                syncButton.setBounds (row.removeFromLeft (64).reduced (2, 2));
                tapButton.setBounds (row.removeFromLeft (56).reduced (2, 2));
                tempoArea = row.withTrimmedLeft (8);
                auto divisionRow = area.removeFromTop (30);
                const int each = divisionRow.getWidth() / 4;
                for (auto& b : divisionButtons)
                    b.setBounds (divisionRow.removeFromLeft (each).reduced (2, 2));
            }

            auto knobs = area.removeFromTop (knobSize + knobLabel);
            const int each = juce::jmin (72, knobs.getWidth() / juce::jmax (1, (int) m.knobs.size()));
            for (auto* k : m.knobs)
                k->slider.setBounds (knobs.removeFromLeft (each).removeFromTop (knobSize).withSizeKeepingCentre (knobSize, knobSize));
        };
        layoutModule (echo, true);
        layoutModule (spring, false);
        layoutModule (sweep, false);
        layoutModule (lowEnd, false);
    }

    void RackPanel::paint (juce::Graphics& g)
    {
        const bool running = model.monitor().isRunning();

        g.setFont (HaloType::label (11.0f));
        g.setColour (c.inkSoft);
        g.drawText ("DUB RACK", headerArea, juce::Justification::centredLeft);

        g.setFont (HaloType::mono (11.5f));
        g.setColour (running ? c.inkSoft : c.orange);
        g.drawText (running ? "on the monitor route, before the -1 dBFS limiter"
                            : "start MONITOR to hear the rack (Ctrl+M)",
                    noteArea, juce::Justification::centredLeft, true);

        for (auto* m : { &echo, &spring, &sweep, &lowEnd })
        {
            g.setColour (c.ink.withAlpha (0.14f));
            g.fillRect (m->titleArea.getX(), m->titleArea.getY() - 3, getWidth() - 32, 1);
            g.setFont (HaloType::label (13.0f));
            g.setColour (m->flag->load() ? c.ink : c.inkSoft);
            g.drawText (m->title, m->titleArea, juce::Justification::centredLeft);

            for (auto* k : m->knobs)
            {
                const auto b = k->slider.getBounds();
                auto labels = juce::Rectangle<int> (b.getCentreX() - 40, b.getBottom(), 80, knobLabel);
                g.setFont (HaloType::label (10.0f));
                g.setColour (c.inkSoft);
                g.drawText (k->name, labels.removeFromTop (14), juce::Justification::centred);
                g.setFont (HaloType::mono (11.0f));
                g.setColour (k->slider.isEnabled() ? c.ink : c.inkSoft.withAlpha (0.6f));
                const double shown = k == echoTime ? model.rack().effectiveMs() : k->slider.getValue();
                g.drawText (k->format (shown), labels, juce::Justification::centred);
            }
        }

        // Tempo readout: where the echo's sync tempo comes from, honestly.
        auto& rack = model.rack();
        g.setFont (HaloType::mono (12.0f));
        const auto bpm = rack.tempoBpm();
        g.setColour (bpm ? c.ink : c.inkSoft);
        g.drawText (juce::String (rack.tempoSource()) + (bpm ? "  " + juce::String (juce::roundToInt (*bpm)) + " BPM" : juce::String()),
                    tempoArea, juce::Justification::centredLeft, true);
    }
}
