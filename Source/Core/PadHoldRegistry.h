#pragma once

#include "Core/Ep40MidiMapping.h"

#include <algorithm>
#include <array>
#include <cstdint>
#include <optional>
#include <vector>

// Port of PadHoldRegistry.swift. Tracks which (channel, note) holds keep each
// physical pad down, so pad travel is polyphonic and honest. Two notes from
// different groups (e.g. 36 and 48) land on the same physical pad; a pad only
// releases when its last hold ends.
namespace halo
{
    class PadHoldRegistry
    {
    public:
        enum class Kind { press, restrike, release, sustain };

        struct Transition
        {
            int pad = 0;
            Kind kind = Kind::sustain;
            float velocity = 0.0f; // 0...1, meaningful for press and restrike
        };

        std::optional<Transition> noteOn (std::uint8_t channel, std::uint8_t note, float velocity01)
        {
            const auto pad = ep40::gridIndexForNote (note);
            if (! pad)
                return std::nullopt;

            auto& held = holds[(std::size_t) *pad];
            const bool wasHeld = ! held.empty();
            const auto key = makeKey (channel, note);
            if (std::find (held.begin(), held.end(), key) == held.end())
                held.push_back (key);

            return Transition { *pad, wasHeld ? Kind::restrike : Kind::press, velocity01 };
        }

        std::optional<Transition> noteOff (std::uint8_t channel, std::uint8_t note)
        {
            const auto pad = ep40::gridIndexForNote (note);
            if (! pad)
                return std::nullopt;

            auto& held = holds[(std::size_t) *pad];
            const auto it = std::find (held.begin(), held.end(), makeKey (channel, note));
            if (it == held.end())
                return std::nullopt; // orphan Note Off: ignore

            held.erase (it);
            return Transition { *pad, held.empty() ? Kind::release : Kind::sustain, 0.0f };
        }

        // Disconnect / reset: returns the pads that were down, then clears all
        // state. No observation may survive a connection change.
        std::vector<int> releaseAll()
        {
            std::vector<int> released;
            for (std::size_t i = 0; i < holds.size(); ++i)
            {
                if (! holds[i].empty())
                    released.push_back ((int) i);
                holds[i].clear();
            }
            return released;
        }

        bool isHeld (int pad) const { return pad >= 0 && pad < ep40::padsPerGroup && ! holds[(std::size_t) pad].empty(); }

    private:
        static std::uint16_t makeKey (std::uint8_t channel, std::uint8_t note) noexcept
        {
            return (std::uint16_t) ((channel << 8) | note);
        }

        std::array<std::vector<std::uint16_t>, ep40::padsPerGroup> holds;
    };
}
