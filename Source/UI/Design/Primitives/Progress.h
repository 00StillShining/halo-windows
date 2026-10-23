#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

namespace halo::progress
{
    // A transfer in progress (docs/redesign/plan.md section 2.5): a pill
    // title (SENDING, BACKING UP), a dot timeline with a hot line, the percent
    // in Halo Numerals with the time left, and two reels turning with the
    // transfer (TP-7). It stays visible until the job ends.
    struct State
    {
        juce::String title;      // "SENDING"
        double fraction = 0.0;   // 0...1, or < 0 when unknown
        juce::String eta;        // "about 1 min left"
        double reelAngle = 0.0;  // radians; advanced by the caller while bytes move
    };

    void paint (juce::Graphics&, juce::Rectangle<float>, const State&);
    void reel (juce::Graphics&, juce::Point<float> centre, float radius, double angle, float wound, juce::Colour);

    // A line-drawn compact cassette (BACKUPS' empty shelf): shell, screws,
    // a ruled label with its title and side, the window with two toothed
    // hubs and the tape between them, and the head opening at the bottom.
    void cassette (juce::Graphics&, juce::Rectangle<float>, juce::Colour line, const juce::String& title, double hubAngle = 0.0);
}
