#pragma once

#include "App/AppModel.h"
#include "Stage/StageComponent.h"
#include "UI/HaloLookAndFeel.h"
#include "UI/ModeBar.h"
#include "UI/PlayRail.h"
#include "UI/StatusBar.h"

#include <juce_gui_basics/juce_gui_basics.h>

namespace halo
{
    // Window content (HaloRootView.swift): status bar on top, the 3D stage,
    // the contextual rail on the right, the mode bar at the bottom.
    class MainComponent : public juce::Component
    {
    public:
        explicit MainComponent (AppModel&);
        ~MainComponent() override;

        void resized() override;
        bool keyPressed (const juce::KeyPress&) override;

    private:
        AppModel& model;
        HaloLookAndFeel lookAndFeel;
        StatusBar statusBar;
        StageComponent stage;
        PlayRail rail;
        ModeBar modeBar;

        JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (MainComponent)
    };
}
