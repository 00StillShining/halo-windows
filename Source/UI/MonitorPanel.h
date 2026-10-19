#pragma once

#include "App/AppModel.h"

#include <juce_gui_basics/juce_gui_basics.h>

namespace halo
{
    // MONITOR section of the Play rail: start/stop the route, record takes,
    // pick the output, set the level, and see honest meters. Port of the
    // monitor and recorder controls in PlayRail.swift / CaptureRail.swift.
    class MonitorPanel : public juce::Component, private AppModel::Listener, private juce::Timer
    {
    public:
        explicit MonitorPanel (AppModel&);
        ~MonitorPanel() override;

        static constexpr int preferredHeight = 274;

        void paint (juce::Graphics&) override;
        void resized() override;

    private:
        void statusChanged() override { refresh(); }
        void timerCallback() override;
        void refresh();
        void rebuildOutputs();
        void paintMeter (juce::Graphics&, juce::Rectangle<float>, float rmsDb, float peakDb, bool clipped) const;

        AppModel& model;
        juce::TextButton monitorButton, recordButton, showButton { "SHOW" };
        juce::ComboBox outputBox;
        juce::Slider level;
        juce::StringArray outputs;
        StereoLevels levels;
        bool clipL = false, clipR = false;
        int clipHold = 0;
        juce::Rectangle<float> meterArea;
        juce::Rectangle<int> infoArea, takeArea, levelValueArea;
    };
}
