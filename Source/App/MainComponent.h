#pragma once

#include "App/AppModel.h"
#include "Core/UiLayout.h"
#include "Stage/StageComponent.h"
#include "UI/ChopSurface.h"
#include "UI/Design/EncoderBus.h"
#include "UI/Design/Motion.h"
#include "UI/Design/Primitives/Deck.h"
#include "UI/Design/Primitives/Lane.h"
#include "UI/Design/Primitives/ParamStrip.h"
#include "UI/Design/Primitives/StatusStrip.h"
#include "UI/Design/WriteGate.h"
#include "UI/HaloLookAndFeel.h"
#include "UI/BackupsPanel.h"
#include "UI/EditPanel.h"
#include "UI/LibraryPanel.h"
#include "UI/LoadPanel.h"
#include "UI/Pages/PlayPage.h"
#include "UI/RackPanel.h"

#include <juce_gui_basics/juce_gui_basics.h>

namespace halo
{
    // The window (docs/redesign/plan.md section 3.1): one slab of black glass
    // (status strip, page body, lane, parameter strip, soft keys) set into a
    // graphite deck (mode keys, SHIFT, REC, four colour encoders). The 3D stage
    // shows only in PLAY; CHOP is a mode; pages not rebuilt yet stay docked at
    // the right of the body beside an honest "not rebuilt yet" plate.
    class MainComponent : public juce::Component, private AppModel::Listener, private juce::Timer
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
        void timerCallback() override;
        void updateMode();
        void updateBindings();
        void updateHint();
        void layoutBody();
        juce::Component* legacyPanel (AppMode) noexcept;
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
        PlayPage playPage { model, stage };
        // Pages not rebuilt yet: today's panels, docked at the right of the body.
        RackPanel rackPanel { model };
        LibraryPanel libraryPanel { model };
        EditPanel editPanel { model, writeGate };
        LoadPanel loadPanel { model, writeGate };
        BackupsPanel backupsPanel { model };
        juce::Rectangle<int> dock;
        ChopSurface chopSurface { model, writeGate };
        LegacyPlate plate;
        Deck deck { model, bus };
        Lane lane { bus };
        juce::OwnedArray<ParamCell> cells;
        SoftKeyRow softKeys { bus };
        juce::String deckHint;
        // One tooltip window for the app; nothing depends on it (every reason is also printed).
        juce::TooltipWindow tooltips { this, 600 };

        layout::Frame frame;
        AppMode shownMode = AppMode::play;
        AppMode boundMode = AppMode::chop;

        JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (MainComponent)
    };
}
