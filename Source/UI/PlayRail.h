#pragma once

#include "App/AppModel.h"
#include "UI/MonitorPanel.h"
#include "UI/RackPanel.h"

#include <juce_gui_basics/juce_gui_basics.h>

namespace halo
{
    // The contextual right rail. In PLAY it's a light utility column that can
    // collapse to a spine (HaloRailView.swift / PlayRail.swift). Modes that
    // aren't ported to Windows yet show an honest state plate instead of
    // pretending to work.
    class PlayRail : public juce::Component, private AppModel::Listener, private juce::Timer
    {
    public:
        explicit PlayRail (AppModel&);
        ~PlayRail() override;

        // The width the rail wants right now (spine when collapsed in PLAY).
        int preferredWidth() const;
        void setExpanded (bool shouldExpand);
        std::function<void()> onPreferredWidthChanged;

        void paint (juce::Graphics&) override;
        void resized() override;
        void mouseUp (const juce::MouseEvent&) override;

    private:
        void statusChanged() override;
        void displayChanged (const DisplayState&) override { dirty = true; }
        void eventLogged() override { dirty = true; }
        void timerCallback() override;

        void paintPlay (juce::Graphics&, juce::Rectangle<int>);
        void paintPlate (juce::Graphics&, juce::Rectangle<int>);
        juce::Rectangle<int> spineArea() const;
        juce::Rectangle<int> contentArea() const;

        // Height of the painted MIDI / LAST PAD / CLOCK block above the monitor.
        static constexpr int infoHeight = 3 * 39 + (3 + 4 + 2) * 22;

        AppModel& model;
        MonitorPanel monitorPanel { model };
        RackPanel rackPanel { model };
        bool expanded = false;
        bool dirty = true;
    };
}
