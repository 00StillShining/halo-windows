#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

namespace halo
{
    // Every symbol in the UI (docs/redesign/plan.md section 2.5): monoline
    // paths on a 16 px grid, 1.5 px stroke, round caps and joins, filled only
    // when active. Never font glyphs, so nothing falls back to Segoe UI Symbol.
    enum class Pict
    {
        none,
        // modes
        play, load, edit, chop, capture, rack, backup,
        // transport and status
        stop, rec, monitor, grab, usb, plugX, lockClosed, lockOpen, triangle, info,
        folder, eye, ear, speaker, feed, clock, device, pads, file, upload, exportOut,
        caret, arrowUp, arrowDown, arrowLeft, arrowRight, enter, check, cross, chevronLeft, chevronRight,
        count
    };

    namespace pictograms
    {
        // Draws the pictogram centred in `box` (scaled from its 16 px grid to the
        // box's smaller side). `filled` fills closed shapes (play, stop, rec...).
        void draw (juce::Graphics&, Pict, juce::Rectangle<float> box, juce::Colour, float strokePx = 1.5f, bool filled = false);
        const juce::Path& path (Pict);
        const char* name (Pict);
    }
}
