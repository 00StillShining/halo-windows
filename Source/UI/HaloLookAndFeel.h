#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

namespace halo
{
    // The few stock JUCE widgets Halo still shows (the output PopupMenu, the
    // CAPTURE search field, scroll bars, any AlertWindow, tooltips) dressed
    // from the theme: black glass, ink text, paper cards with black text.
    class HaloLookAndFeel : public juce::LookAndFeel_V4
    {
    public:
        HaloLookAndFeel();

        // Tooltips: a small paper card with black text (plan section 3.14).
        // Nothing depends on them; every reason is also printed.
        juce::Rectangle<int> getTooltipBounds (const juce::String& tipText, juce::Point<int> screenPos, juce::Rectangle<int> parentArea) override;
        void drawTooltip (juce::Graphics&, const juce::String& text, int width, int height) override;

        // Stock widgets get the embedded faces.
        juce::Typeface::Ptr getTypefaceForFont (const juce::Font&) override;

        // Any remaining AlertWindow wears the ConfirmCard's paper (plan 3.13).
        void drawAlertBox (juce::Graphics&, juce::AlertWindow&, const juce::Rectangle<int>& textArea, juce::TextLayout&) override;
        juce::Font getAlertWindowTitleFont() override;
        juce::Font getAlertWindowMessageFont() override;
        juce::Font getAlertWindowFont() override;
        juce::Font getPopupMenuFont() override;
    };
}
