#pragma once

#include "App/EditSession.h"
#include "UI/Design/Primitives/Waveform/WaveCache.h"

#include <juce_gui_basics/juce_gui_basics.h>

namespace halo
{
    // CHOP's stepped slicer (docs/redesign/plan.md sections 2.5 and 3.7), in
    // the OP-XY manner: 3 px columns on a 4 px pitch, heights snapped to 4 px,
    // mirrored; the selected slice in ramp7 on its own backdrop, the others in
    // ramp4; the tails LENGTH / AUTO-TRIM drop are dark and hatched with a
    // 1.5 px orange edge, a foot tab and a "-3.5s" chip; 3 px black cut lines
    // with 9 px square handles (the selected slice's in orange); slice numbers
    // top-left (the selected one in a white chip); the hot playhead.
    //   click            audition the slice
    //   double-click     add a cut
    //   drag a marker    move it
    //   right-click one  remove it
    //   wheel            scroll (when zoomed)
    //   ctrl + wheel     zoom (the one exception to "ctrl = coarse")
    //   wheel on a handle   nudge that cut 10 ms (alt: 1 ms)
    class Slicer : public juce::Component, private juce::ChangeListener, private juce::Timer
    {
    public:
        explicit Slicer (EditSession&);
        ~Slicer() override;

        // The visible span in frames (whole file unless zoomed).
        std::int64_t viewStart() const noexcept { return start; }
        std::int64_t viewEnd() const noexcept { return end; }
        std::function<void()> onViewChanged;
        void resetView();

        void paint (juce::Graphics&) override;
        void resized() override;
        void mouseMove (const juce::MouseEvent&) override;
        void mouseDown (const juce::MouseEvent&) override;
        void mouseDrag (const juce::MouseEvent&) override;
        void mouseUp (const juce::MouseEvent&) override;
        void mouseDoubleClick (const juce::MouseEvent&) override;
        void mouseWheelMove (const juce::MouseEvent&, const juce::MouseWheelDetails&) override;

    private:
        void changeListenerCallback (juce::ChangeBroadcaster*) override;
        void timerCallback() override;
        float frameToX (double frame) const;
        double xToFrame (float x) const;
        int cutAt (float x) const;          // a cut line within reach of x
        int handleAt (juce::Point<float>) const; // a square handle under the point
        juce::Rectangle<float> plot() const;
        void clampView();

        EditSession& session;
        WaveCache cache;
        std::shared_ptr<const std::vector<float>> source;
        std::int64_t start = 0, end = 0;
        int dragCut = -1;
        bool dragged = false;
        float wheelAcc = 0.0f;
    };
}
