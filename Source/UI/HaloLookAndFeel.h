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

        // Mechanical knob (HaloKnob.swift): a paper cap with a hard offset
        // shadow, an ink indicator and an orange value arc.
        void drawRotarySlider (juce::Graphics&, int x, int y, int width, int height, float sliderPos,
                               float startAngle, float endAngle, juce::Slider&) override;

        // Draws the utility-panel look: paper slab with a hard offset shadow.
        static void drawPanel (juce::Graphics&, juce::Rectangle<float> bounds, juce::Colour fill);

        // Tooltips: a small paper card with black text (plan section 3.14).
        // Nothing depends on them; every reason is also printed.
        juce::Rectangle<int> getTooltipBounds (const juce::String& tipText, juce::Point<int> screenPos, juce::Rectangle<int> parentArea) override;
        void drawTooltip (juce::Graphics&, const juce::String& text, int width, int height) override;

        // Stock widgets (ComboBox, PopupMenu, AlertWindow) get the embedded
        // faces once the redesign's theme is on.
        juce::Typeface::Ptr getTypefaceForFont (const juce::Font&) override;

        // Any remaining AlertWindow wears the ConfirmCard's paper (plan 3.13).
        void drawAlertBox (juce::Graphics&, juce::AlertWindow&, const juce::Rectangle<int>& textArea, juce::TextLayout&) override;
        juce::Font getAlertWindowTitleFont() override;
        juce::Font getAlertWindowMessageFont() override;
        juce::Font getAlertWindowFont() override;
    };
}
