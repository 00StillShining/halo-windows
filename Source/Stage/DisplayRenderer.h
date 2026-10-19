#pragma once

#include "Core/DisplayState.h"

#include <juce_graphics/juce_graphics.h>

namespace halo
{
    // Draws the replica's display texture (1024 x 289) from a DisplayState.
    // Port of EP40DisplayRenderer.swift: the authored indicator atlas is dimmed,
    // and only the elements backed by observed state are lit.
    class DisplayRenderer
    {
    public:
        static constexpr int width = 1024;
        static constexpr int height = 289;

        DisplayRenderer();

        juce::Image render (const DisplayState&) const;

    private:
        void highlight (juce::Graphics&, juce::Rectangle<int>) const;
        void drawDigits (juce::Graphics&, int value, float brightness) const;
        void drawPadActivity (juce::Graphics&, std::optional<int> pad, float velocity) const;
        void drawMeters (juce::Graphics&, float left, float right) const;
        void drawFeedBeacon (juce::Graphics&, FeedMode, bool pulse) const;

        juce::Image atlas;      // the authored PNG
        juce::Image indicators; // the atlas with its flat green background removed
    };
}
