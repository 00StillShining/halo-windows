#pragma once

#include "Core/DisplayState.h"
#include "Core/LcdState.h"

#include <juce_graphics/juce_graphics.h>

#include <array>

namespace halo
{
    // Draws the replica's display texture (1024 x 289) from an LcdState (plan M9):
    // TE's authored indicator atlas dimmed to the feed's backlight, only the
    // icons LcdState lights drawn lit (with a soft in-glass halation), and the
    // digits in TE's own ten-segment shapes (Stage/LcdSegments.h, generated from
    // TE's screen drawing), so "120", "___" and "SIM" read on the device itself.
    class DisplayRenderer
    {
    public:
        static constexpr int width = 1024;
        static constexpr int height = 289;

        DisplayRenderer();

        juce::Image render (const LcdState&) const;
        juce::Image render (const DisplayState& s) const { return render (toLcdState (s)); }

    private:
        void drawDigits (juce::Graphics&, const LcdState&) const;
        void drawMeters (juce::Graphics&, const LcdState&) const;

        juce::Image atlas;      // the authored PNG
        juce::Image indicators; // the atlas with its flat green background removed
        // Per icon: its crisp crop and its blurred halo, made once (not per frame).
        std::array<juce::Image, (size_t) LcdIcon::count> iconCrops, iconHalos;
    };
}
