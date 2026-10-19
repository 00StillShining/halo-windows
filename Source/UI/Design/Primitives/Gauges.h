#pragma once

#include "UI/Design/Motion.h"

#include <juce_gui_basics/juce_gui_basics.h>

namespace halo
{
    // Small line-drawn gauges and meters (docs/redesign/plan.md section 2.5).
    // Nothing decorative pretends to be a signal: every meter names its tap.
    namespace gauges
    {
        // An arc of ticks (SENSITIVITY): lit up to `norm` in the line colour.
        void tickDial (juce::Graphics&, juce::Rectangle<float>, double norm, juce::Colour line);
        // A row of dots (SLICES "pads left"): `lit` of `count` filled.
        void dotRow (juce::Graphics&, juce::Rectangle<float>, int count, int lit, juce::Colour line);
        // A tick ruler with a triangle pointer at `norm` and an optional crimson
        // limit tick (LENGTH: the EP-40's 20 s); labels at both ends.
        void ruler (juce::Graphics&, juce::Rectangle<float>, double norm, juce::Colour line, double limitNorm = -1.0,
                    const juce::String& lowLabel = {}, const juce::String& highLabel = {});
        // A mini timeline: a hairline with ticks and a dot at `norm` (SLICE).
        void timeline (juce::Graphics&, juce::Rectangle<float>, double norm, int ticks, juce::Colour line);
        // An LED ladder: `segments` 2 px ticks, `lit` of them lit; unlit plum.
        void ladder (juce::Graphics&, juce::Rectangle<float>, int segments, int lit, juce::Colour litColour,
                     bool vertical = false, juce::Colour unlit = {});

        // The tape lane (GRAB): 12 px of history; buffered audio in ramp2, the
        // window GRAB would take in the encoder colour with an outlined start
        // circle and a filled end circle, the hot "now" head at the right.
        void tapeLane (juce::Graphics&, juce::Rectangle<float>, double historySeconds, double bufferedSeconds,
                       double windowSeconds, juce::Colour window);
    }

    // A level arc (was VuArc): calibrated in dBFS, -40...0, ticks every 6 dB,
    // the limit zone (-1...0) as a 5 px crimson arc, a hot needle with
    // ballistics, a ghosted big L or R behind, the tap name printed under it
    // ("out", "in", "pre-rack") and a `limit` lamp (plum -> crimson, held 1.5 s)
    // driven by the pre-limiter peak. It never shows a VU over-zone: the
    // published post-limiter level can't exceed -1 dBFS.
    class LevelArc : public juce::Component, private MotionClock::Animated
    {
    public:
        LevelArc (const juce::String& channel, const juce::String& tap);
        ~LevelArc() override;

        void setLevel (float peakDb, bool limitHit); // feed at any rate; the needle moves on the motion clock
        void setRunning (bool);
        void paint (juce::Graphics&) override;

    private:
        bool tick (double dt) override;

        juce::String channel, tap;
        Ballistics needle;
        float target = -100.0f;
        double limitHold = 0.0;
        bool running = false;
    };
}
