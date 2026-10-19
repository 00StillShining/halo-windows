#pragma once

#include "App/AppModel.h"

#include <juce_gui_basics/juce_gui_basics.h>

namespace halo
{
    // Bottom mode bar (HaloModeBar.swift): PLAY LOAD EDIT CAPTURE RACK BACKUPS,
    // switched with Ctrl+1...6.
    class ModeBar : public juce::Component, private AppModel::Listener
    {
    public:
        explicit ModeBar (AppModel&);
        ~ModeBar() override;

        void paint (juce::Graphics&) override;
        void resized() override;

        static juce::String title (AppMode);

    private:
        void statusChanged() override;

        AppModel& model;
        juce::OwnedArray<juce::TextButton> buttons;
    };
}
