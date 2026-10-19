#pragma once

#include "App/AppModel.h"

#include <juce_gui_basics/juce_gui_basics.h>

namespace halo
{
    // Top status bar (HaloStatusBar.swift): wordmark plus honest status chips.
    // Every value comes from the model; nothing is inferred for display.
    class StatusBar : public juce::Component, private AppModel::Listener
    {
    public:
        explicit StatusBar (AppModel&);
        ~StatusBar() override;

        void paint (juce::Graphics&) override;

    private:
        void statusChanged() override { repaint(); }
        void displayChanged (const DisplayState&) override;

        struct Chip
        {
            juce::String label, value;
            juce::Colour tint;
        };

        float drawChip (juce::Graphics&, const Chip&, float x, float centreY) const;

        AppModel& model;
        FeedMode lastFeed = FeedMode::preview;
    };
}
