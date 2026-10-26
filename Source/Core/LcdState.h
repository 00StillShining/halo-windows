#pragma once

#include "Core/DisplayState.h"

#include <array>
#include <cstdint>

// What the replica LCD shows, decided from DisplayState alone (plan M9, D4):
// every lit element has a backing DisplayState field, and the feed's
// provenance is on the device itself, not only beside it:
//
//   feed      backlight   digits
//   live      0.35        the value (tempo)
//   waiting   0.25        "___", nothing lit (nothing observed yet)
//   preview   0.15        "SIM" (the demo; never presented as hardware truth)
//
// The plan asked for "PRV", but TE's ten-segment digit has no middle bars and
// cannot draw a V (it reads "PRY"); "SIM" (simulated) reads unmistakably.
//
// Pure logic, no JUCE: Tests/LcdStateTests.cpp covers it exhaustively.
namespace halo
{
    // The TE screen icons Halo can back today (DisplayRenderer.cpp lights only these).
    enum class LcdIcon : int { sound = 0, main, tempo, erase, system, play, bpm, groupA, groupB, groupC, groupD, numpad, count };

    struct LcdState
    {
        FeedMode feed = FeedMode::preview;
        float backlight = 0.15f;                 // relative LCD backlight (live 0.35)
        std::array<char, 3> text { 'S', 'I', 'M' };  // 0-9, S, I, M, _, or ' ' (blank)
        std::array<bool, (int) LcdIcon::count> lit {};
        std::array<float, 4> meters {};          // the right-hand banks: L, R, then L and R at 0.78

        bool operator== (const LcdState&) const = default;

        static constexpr float liveBacklight = 0.35f, waitingBacklight = 0.25f, previewBacklight = 0.15f;
    };

    LcdState toLcdState (const DisplayState&);

    // Segment mask (bit per lcd::Segment: top, bottom, left, right, upper and
    // lower centre, upper-left, upper-right, lower-left, lower-right) of a glyph
    // in TE's ten-segment digit; 0 for blank or an unknown character.
    std::uint16_t lcdGlyph (char);
}
