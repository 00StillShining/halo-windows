#pragma once

#include "Core/DisplayState.h"
#include "Core/MidiEvent.h"

#include <cstdint>
#include <optional>
#include <vector>

namespace halo
{
    // MIDI clock tempo tracker (port of HaloAppModel.updateTempo). Unlike the
    // Mac version it has a freshness timeout, so a vanished clock can't leave a
    // stale BPM behind (Mac audit finding #5).
    class ClockTracker
    {
    public:
        static constexpr double staleAfterSeconds = 0.5;
        static constexpr int clocksPerQuarter = 24;
        static constexpr int clocksPerBar = 96;

        void reset() noexcept;
        void tick (double timeSeconds) noexcept;

        // Bar phase in clocks since Start or a Song Position locate.
        int clockCount() const noexcept { return count; }
        void setClockCount (int value) noexcept { count = value; }

        // Seconds per clock, only while ticks are still arriving.
        std::optional<double> secondsPerClock (double nowSeconds) const noexcept;

        // Observed tempo in BPM, within 30...300, only while ticks are fresh.
        std::optional<double> bpm (double nowSeconds) const noexcept;

        bool isStale (double nowSeconds) const noexcept;

    private:
        int count = 0;
        std::optional<double> lastClock;
        std::optional<double> smoothedInterval;
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
