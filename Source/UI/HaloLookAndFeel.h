#pragma once

#include "UI/HaloColours.h"

#include <juce_gui_basics/juce_gui_basics.h>

namespace halo
{
    // Hard-edged "mechanical" button language (MechanicalButtonStyle.swift):
    // square-ish paper caps with a hard offset shadow that collapses on press,
    // and a thin orange rim for the selected state.
    class HaloLookAndFeel : public juce::LookAndFeel_V4
    {
    public:
        HaloLookAndFeel();

        void drawButtonBackground (juce::Graphics&, juce::Button&, const juce::Colour&,
                                   bool highlighted, bool down) override;
        void drawButtonText (juce::Graphics&, juce::TextButton&, bool highlighted, bool down) override;
        juce::Font getTextButtonFont (juce::TextButton&, int buttonHeight) override;

        // Draws the utility-panel look: paper slab with a hard offset shadow.
        static void drawPanel (juce::Graphics&, juce::Rectangle<float> bounds, juce::Colour fill);
    };
}
