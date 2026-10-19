#pragma once

#include "App/AppModel.h"
#include "Core/UiLayout.h"
#include "Stage/StageComponent.h"
#include "UI/ChopSurface.h"
#include "UI/Design/EncoderBus.h"
#include "UI/Design/Motion.h"
#include "UI/Design/Primitives/Deck.h"
#include "UI/Design/Primitives/StageFrame.h"
#include "UI/Design/Primitives/StatusStrip.h"
#include "UI/Design/WriteGate.h"
#include "UI/HaloLookAndFeel.h"
#include "UI/PlayRail.h"

#include <juce_gui_basics/juce_gui_basics.h>

namespace halo
{
    // The window (docs/redesign/plan.md section 3.1): one slab of black glass
    // (status strip, page body, lane, parameter strip, soft keys) set into a
    // graphite deck (mode keys, SHIFT, REC, four colour encoders). The 3D stage
    // shows only in PLAY; CHOP is a mode; pages not rebuilt yet stay docked at
    // the right of the body beside an honest "not rebuilt yet" plate.
    class MainComponent : public juce::Component, private AppModel::Listener
    {
    public:
        explicit MainComponent (AppModel&);
        ~MainComponent() override;

        void paint (juce::Graphics&) override;
        void resized() override;
        bool keyPressed (const juce::KeyPress&) override;
        void modifierKeysChanged (const juce::ModifierKeys&) override;

        const layout::Frame& currentFrame() const noexcept { return frame; }

    private:
        void statusChanged() override { updateMode(); }
        void updateMode();
        void layoutBody();
        float physicalScale() const;

        // A dim plate where a page that hasn't been rebuilt yet will go.
        class LegacyPlate : public juce::Component
        {
        public:
            void setMode (AppMode m) { mode = m; repaint(); }
            void paint (juce::Graphics&) override;

        private:
            AppMode mode = AppMode::load;
        };

        AppModel& model;
        HaloLookAndFeel lookAndFeel;
        MotionClock motion { *this };
        EncoderBus bus;
        // Every device write goes through this gate and its confirm card (H3).
        std::unique_ptr<DeviceWrites> deviceWrites { makeDeviceWrites (model.deviceWriter()) };
        WriteGate writeGate;
        bool cardOpen = false;

        StatusStrip statusStrip { model };
        StageComponent stage { model };
        StageFrame stageFrame { model, stage };
        PlayRail rail;
        ChopSurface chopSurface { model, writeGate };
        LegacyPlate plate;
        Deck deck { model, bus };
        // One tooltip window for the app; nothing depends on it (every reason is also printed).
        juce::TooltipWindow tooltips { this, 600 };

        layout::Frame frame;
        AppMode shownMode = AppMode::play;

        JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (MainComponent)
    };
}
