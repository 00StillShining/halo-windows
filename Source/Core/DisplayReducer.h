#pragma once

#include "Core/DisplayState.h"
#include "Core/MidiEvent.h"

#include <array>
#include <cstdint>
#include <optional>
#include <vector>

namespace halo
{
    // MIDI clock tempo tracker. Two changes from the Mac's updateTempo:
    //  - a freshness timeout, so a vanished clock can't leave a stale BPM
    //    (Mac audit finding #5);
    //  - tempo comes from the span across a window of ticks, not a smoothed
    //    per-tick interval. Windows delivers clock in small bursts; the
    //    per-tick filter discarded the near-zero gaps inside a burst and read
    //    a 180 BPM EP-40 as 179.2.
    class ClockTracker
    {
    public:
        static constexpr double staleAfterSeconds = 0.5;
        static constexpr double gapResetSeconds = 0.2;
        static constexpr int clocksPerQuarter = 24;
        static constexpr int clocksPerBar = 96;
        static constexpr int windowTicks = 96;  // one bar
        static constexpr int minimumTicks = 12; // half a beat before reading

        void reset() noexcept;
        // Forget tempo timing but keep the bar phase (Stop, so Continue resumes in phase).
        void resetTiming() noexcept;
        void tick (double timeSeconds) noexcept;

        // True when the most recent tick was the first tick of a bar: tick 1
        // after Start, then 97, 193... (the Mac marked tick 96, one tick early).
        bool lastTickWasDownbeat() const noexcept { return count > 0 && (count - 1) % clocksPerBar == 0; }

        // Bar phase in clocks since Start or a Song Position locate.
        int clockCount() const noexcept { return count; }
        void setClockCount (int value) noexcept { count = value; }

        // Seconds per clock, only while ticks are still arriving.
        std::optional<double> secondsPerClock (double nowSeconds) const noexcept;

        // Observed tempo in BPM, within 30...300, only while ticks are fresh.
        std::optional<double> bpm (double nowSeconds) const noexcept;

        bool isStale (double nowSeconds) const noexcept;

        // Ticks were arriving and then stopped (not just "no timing yet").
        bool wentQuiet (double nowSeconds) const noexcept { return lastClock && nowSeconds - *lastClock > staleAfterSeconds; }

    private:
        int count = 0;
        std::array<double, windowTicks + 1> times {}; // ring of recent tick times
        int stored = 0;                                 // valid entries, up to windowTicks + 1
        int head = 0;                                   // next write position
        std::optional<double> lastClock;
    };

    // Turns observed MIDI into the replica display's state. A port of the
    // display half of HaloAppModel.receive(_:), without GRAB or the rack.
    class DisplayReducer
    {
    public:
        DisplayReducer();

        const DisplayState& state() const noexcept { return current; }
        const ClockTracker& clock() const noexcept { return clockTracker; }
        std::size_t heldKeyCount() const noexcept { return held.size(); }

        // A connection change resets everything: no held key, clock or live
        // display may survive it.
        void connectionChanged (bool connected);

        // Applies one event. Returns true when the display should be redrawn
        // (MIDI clock is thinned to eight redraws per quarter note).
        bool apply (const MidiEvent& event, double nowSeconds);

        // Call regularly. Drops a clock that stopped arriving. Returns true when
        // the display changed.
        bool expire (double nowSeconds);

        // Shows a preview frame while disconnected. Ignored once a feed exists.
        bool showPreview (const DisplayState& frame);

        // Forget every held key (e.g. after a MIDI overflow or sleep).
        void releaseAllHeldKeys() noexcept { held.clear(); }

    private:
        struct HeldKey
        {
            std::uint8_t channel;
            std::uint8_t note;
            bool operator== (const HeldKey&) const = default;
        };

        static void applyPadMapping (std::uint8_t note, DisplayState& state);

        DisplayState current;
        ClockTracker clockTracker;
        std::vector<HeldKey> held;
        double lastNoteTime = -1.0e9;
    };
}
