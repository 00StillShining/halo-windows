#include "UI/PlayRail.h"
#include "Core/Ep40MidiMapping.h"
#include "UI/HaloColours.h"
#include "UI/HaloLookAndFeel.h"

namespace halo
{
    namespace
    {
        const auto& c = legacyColours();

        void sectionTitle (juce::Graphics& g, juce::Rectangle<int>& area, const juce::String& text)
        {
            area.removeFromTop (14);
            g.setFont (HaloType::label (11.0f));
            g.setColour (c.inkSoft);
            g.drawText (text, area.removeFromTop (18), juce::Justification::centredLeft);
            g.setColour (c.ink.withAlpha (0.14f));
            g.fillRect (area.removeFromTop (1));
            area.removeFromTop (6);
        }

        void row (juce::Graphics& g, juce::Rectangle<int>& area, const juce::String& label,
                  const juce::String& value, juce::Colour valueColour = c.ink)
        {
            auto line = area.removeFromTop (22);
            g.setFont (HaloType::label (11.0f, false));
            g.setColour (c.inkSoft.withAlpha (0.85f));
            g.drawText (label, line.removeFromLeft (96), juce::Justification::centredLeft);
            g.setFont (HaloType::mono (13.0f));
            g.setColour (valueColour);
            g.drawText (value, line, juce::Justification::centredLeft, true);
        }
    }

    PlayRail::PlayRail (AppModel& m, WriteGate& g) : model (m), gate (g)
    {
        for (auto* panel : std::initializer_list<juce::Component*> { &monitorPanel, &rackPanel, &libraryPanel, &editPanel, &loadPanel, &backupsPanel })
            addChildComponent (panel);
        model.addListener (this);
        startTimerHz (20); // coalesce bursty MIDI into at most 20 repaints a second
    }

    PlayRail::~PlayRail()
    {
        model.removeListener (this);
    }

    int PlayRail::preferredWidth (int windowWidth) const
    {
        switch (model.mode())
        {
            case AppMode::play:    return expanded ? HaloMetrics::railPlayWidth : HaloMetrics::railSpineWidth;
            case AppMode::rack:    return 420;
            case AppMode::chop:
            case AppMode::load:
            case AppMode::backups:
            case AppMode::capture:
            case AppMode::edit:    break;
        }
        // Data-heavy modes take ~37% of the window (HaloMetrics.dataRailWidth on the Mac).
        return juce::jlimit (520, 640, (int) (windowWidth * 0.37f));
    }

    void PlayRail::statusChanged()
    {
        dirty = true;
        if (onPreferredWidthChanged)
            onPreferredWidthChanged();
        resized(); // mode changes show or hide the monitor panel
    }

    void PlayRail::timerCallback()
    {
        if (dirty)
        {
            dirty = false;
            repaint();
        }
    }

    juce::Rectangle<int> PlayRail::spineArea() const
    {
        return getLocalBounds().removeFromLeft (HaloMetrics::railSpineWidth);
    }

    void PlayRail::setExpanded (bool shouldExpand)
    {
        if (expanded == shouldExpand)
            return;
        expanded = shouldExpand;
        if (onPreferredWidthChanged)
            onPreferredWidthChanged();
        resized();
        repaint();
    }

    void PlayRail::mouseUp (const juce::MouseEvent& e)
    {
        if (model.mode() == AppMode::play && spineArea().contains (e.getPosition()))
            setExpanded (! expanded);
    }

    void PlayRail::paint (juce::Graphics& g)
    {
        g.fillAll (c.paperHigh.withAlpha (0.92f));
        g.setColour (c.ink.withAlpha (0.16f));
        g.fillRect (getLocalBounds().removeFromLeft (1));

        if (model.mode() != AppMode::play)
            return; // the mode's own panel draws itself

        // The spine: always present in PLAY, click to expand or collapse.
        const auto spine = spineArea();
        g.setFont (HaloType::label (11.0f));
        g.setColour (c.inkSoft);
        {
            juce::Graphics::ScopedSaveState save (g);
            g.addTransform (juce::AffineTransform::rotation (juce::MathConstants<float>::halfPi,
                                                             (float) spine.getCentreX(), (float) spine.getCentreY()));
            g.drawText (expanded ? "HIDE" : "RAIL", spine.withSizeKeepingCentre (80, 20), juce::Justification::centred);
        }
        g.fillRect (spine.getCentreX() - 2, spine.getCentreY() + 48, 4, 4);

        if (expanded)
            paintPlay (g, contentArea());
    }

    juce::Rectangle<int> PlayRail::contentArea() const
    {
        return getLocalBounds().withTrimmedLeft (HaloMetrics::railSpineWidth).reduced (16, 8);
    }

    void PlayRail::resized()
    {
        const bool show = model.mode() == AppMode::play && expanded;
        monitorPanel.setVisible (show);
        if (show)
            monitorPanel.setBounds (contentArea().withTrimmedTop (infoHeight).withHeight (MonitorPanel::preferredHeight));

        auto showFor = [this] (juce::Component& panel, AppMode mode)
        {
            const bool visible = model.mode() == mode;
            panel.setVisible (visible);
            if (visible)
                panel.setBounds (getLocalBounds().withTrimmedLeft (1));
        };
        showFor (rackPanel, AppMode::rack);
        showFor (libraryPanel, AppMode::capture);
        showFor (editPanel, AppMode::edit);
        showFor (loadPanel, AppMode::load);
        showFor (backupsPanel, AppMode::backups);
    }

    void PlayRail::paintPlay (juce::Graphics& g, juce::Rectangle<int> area)
    {
        const auto& d = model.display();
        const bool live = d.feedMode == FeedMode::live;

        sectionTitle (g, area, model.isMock() ? "MIDI  (MOCK SOURCE)" : "MIDI");
        row (g, area, "PORT", model.deviceName().isEmpty() ? noValue : model.deviceName());
        row (g, area, "EP-40", model.deviceStatus());
        row (g, area, "FEED", model.displayStatus(), live ? c.riddimGreen : c.ink);

        // Always four rows, so the monitor panel below never moves.
        sectionTitle (g, area, "LAST PAD");
        const bool held = live && d.activePadIndex && d.activeGroup;
        row (g, area, "PAD", held ? juce::String (ep40::groupLabels[(size_t) *d.activeGroup].data()) + " "
                                        + juce::String (ep40::gridLabels[(size_t) *d.activePadIndex].data())
                                  : juce::String (live ? "none held" : "no live feed"),
             held ? c.ink : c.inkSoft);
        row (g, area, "NOTE", held ? juce::String (d.value) : noValue, held ? c.ink : c.inkSoft);
        row (g, area, "VELOCITY", held ? juce::String (juce::roundToInt (d.velocity * 127.0f)) : noValue, held ? c.ink : c.inkSoft);
        row (g, area, "LISTENING", "all 16 channels", c.inkSoft); // a halo fact, not a device claim

        sectionTitle (g, area, "CLOCK");
        const auto bpm = model.clockBpm();
        row (g, area, "TEMPO", bpm ? juce::String (*bpm, 1) + " BPM" : juce::String ("no clock"), bpm ? c.ink : c.inkSoft);
        row (g, area, "TRANSPORT", live ? (d.isPlaying ? "PLAYING" : "STOPPED") : noValue);

        // The monitor panel (a child component) sits here.
        area.removeFromTop (MonitorPanel::preferredHeight);

        sectionTitle (g, area, "EVENTS");
        g.setFont (HaloType::mono (11.5f));
        const auto& log = model.eventLog();
        const int lineHeight = 17;
        const int visible = juce::jmax (0, area.getHeight() / lineHeight);
        const int first = juce::jmax (0, (int) log.size() - visible);
        for (int i = first; i < (int) log.size(); ++i)
        {
            g.setColour (i == (int) log.size() - 1 ? c.ink : c.inkSoft);
            g.drawText (log[(size_t) i], area.removeFromTop (lineHeight), juce::Justification::centredLeft, true);
        }
    }
}
