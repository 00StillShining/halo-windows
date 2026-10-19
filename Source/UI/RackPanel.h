#pragma once

#include "App/AppModel.h"

#include <juce_gui_basics/juce_gui_basics.h>

namespace halo
{
    // RACK mode (RackRail.swift): ENGAGE / PRINT FX, then TAPE ECHO, SPRING,
    // SWEEP and LOW END, each with its own ON switch and knobs.
    class RackPanel : public juce::Component, private AppModel::Listener, private juce::Timer
    {
    public:
        explicit RackPanel (AppModel&);
        ~RackPanel() override;

        void paint (juce::Graphics&) override;
        void resized() override;

    private:
        struct Knob
        {
            juce::String name;
            juce::Slider slider { juce::Slider::RotaryHorizontalVerticalDrag, juce::Slider::NoTextBox };
            std::function<juce::String (double)> format;
        };

        struct Module
        {
            juce::String title;
            juce::TextButton onButton;
            std::atomic<bool>* flag = nullptr;
            std::vector<Knob*> knobs;
            juce::Rectangle<int> titleArea;
        };

        Knob& addKnob (const juce::String& name, double min, double max, double value,
                       std::function<void (double)> onChange, std::function<juce::String (double)> format = {});
        void addModule (Module&, const juce::String& title, std::atomic<bool>& flag, std::vector<Knob*> knobs);
        void statusChanged() override { refresh(); }
        void timerCallback() override { refresh(); }
        void refresh();

        AppModel& model;
        juce::OwnedArray<Knob> allKnobs;
        Module echo, spring, sweep, lowEnd;
        juce::TextButton engageButton, printButton, monitorButton, syncButton { "SYNC" }, tapButton { "TAP" };
        std::array<juce::TextButton, 4> divisionButtons;
        Knob* echoTime = nullptr;
        juce::Rectangle<int> headerArea, tempoArea, noteArea;
    };
}
