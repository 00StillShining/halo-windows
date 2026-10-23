#pragma once

#include "App/AppModel.h"
#include "Core/UiLayout.h"
#include "UI/Design/Primitives/ActionKey.h"
#include "UI/Design/Primitives/Gauges.h"
#include "UI/Design/Primitives/StageFrame.h"
#include "UI/Design/Primitives/StepKeyRow.h"

#include <juce_gui_basics/juce_gui_basics.h>

namespace halo
{
    // PLAY (Ctrl+1; docs/redesign/plan.md section 3.5): the EP-40 on the black
    // glass in its honest frame, and the rail beside it:
    //   LAST PAD  four read-only ramp cells (pad, note, velocity, channel)
    //   MONITOR   L / R level arcs (tap out) with limit lamps, the LEVEL
    //             numeral and ladder (encoder 1 owns it), the out key,
    //             monitor and rec keys, the info line, the last take
    //   GRAB      the length chips (bars with a clock, else seconds), the
    //             30 s tape lane with the ochre bracket, grab, the last grab
    //   RACK      the sound-path chain (one line at compact sizes)
    //   EVENTS    the newest lines, never fewer than three
    // The rail collapses to a spine (click it, or Esc); HALO_RAIL=closed.
    class PlayPage : public juce::Component, private AppModel::Listener, private juce::Timer
    {
    public:
        PlayPage (AppModel&, juce::Component& stage);
        ~PlayPage() override;

        void setFrame (const layout::Frame&);
        void setRailOpen (bool);
        bool isRailOpen() const noexcept { return railOpen; }

        // The lane's redundant content in PLAY: the clock, the beat ruler, keycaps.
        void paintLane (juce::Graphics&, juce::Rectangle<float>, bool compact) const;

        StageFrame& stageFrame() noexcept { return frameView; }
        std::function<void()> onLaneChanged; // the clock moved a step

        void paint (juce::Graphics&) override;
        void resized() override;
        // The 30 Hz poll runs only while PLAY shows.
        void visibilityChanged() override { if (isVisible()) startTimerHz (30); else stopTimer(); }
        void mouseUp (const juce::MouseEvent&) override;

    private:
        void statusChanged() override;
        void displayChanged (const DisplayState&) override;
        void eventLogged() override { repaint (local (play.events)); }
        void timerCallback() override;
        void refreshControls();
        void showOutputMenu();
        juce::Rectangle<int> local (const layout::Rect&) const;
        juce::Rectangle<float> localF (const layout::Rect&) const;

        void paintLastPad (juce::Graphics&, juce::Rectangle<float>);
        void paintMonitor (juce::Graphics&, juce::Rectangle<float>);
        void paintGrab (juce::Graphics&, juce::Rectangle<float>);
        void paintRack (juce::Graphics&, juce::Rectangle<float>);
        void paintEvents (juce::Graphics&, juce::Rectangle<float>);
        void paintSpine (juce::Graphics&, juce::Rectangle<float>);

        // A step-key styled toggle (monitor, rec).
        class ToggleKey : public juce::Button
        {
        public:
            ToggleKey (const juce::String& text, bool recordLamp) : juce::Button (text), lamp (recordLamp) {}
            void setOn (bool on) { if (on != isOn) { isOn = on; repaint(); } }
            void paintButton (juce::Graphics&, bool highlighted, bool down) override;

        private:
            bool lamp = false, isOn = false;
        };

        AppModel& model;
        StageFrame frameView;
        layout::Frame frame;
        layout::PlayLayout play;
        bool railOpen = true;

        LevelArc arcL { "L", "out" }, arcR { "R", "out" };
        ToggleKey monitorKey { "monitor", false }, recKey { "rec", true };
        ActionKey outKey { "out", Pict::speaker }, grabKey { "grab", Pict::grab };
        ActionKey showTake { "show" }, showGrab { "show" };
        StepKeyRow grabLengths;
        juce::StringArray outputs;
        int lastStep = -1;
        bool lastPadLive = false;
        juce::Rectangle<int> spineArea;
    };
}
