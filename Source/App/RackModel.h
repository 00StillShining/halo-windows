#pragma once

#include "Core/DubRack.h"

#include <optional>

namespace halo
{
    // UI-side state of the dub rack (port of RackModel.swift). Writes every
    // change straight into the engine's lock-free RackParameters.
    class RackModel
    {
    public:
        explicit RackModel (RackParameters& p) : params (p) { applyEchoTime(); }

        // TAPE ECHO time: SYNC follows the tempo (clock first, then TAP), FREE uses freeMs.
        bool sync = false;
        EchoDivision division = EchoDivision::eighth;
        double freeMs = 360.0;

        void setSync (bool on) { sync = on; applyEchoTime(); }
        void setDivision (EchoDivision d) { division = d; applyEchoTime(); }
        void setFreeMs (double ms) { freeMs = ms; applyEchoTime(); }

        // Observed MIDI clock tempo (nullopt when there is none). Cheap to call often.
        void updateClockBpm (std::optional<double> bpm)
        {
            const auto rounded = bpm ? std::optional<double> (std::round (*bpm)) : std::nullopt;
            if (rounded == clockBpm)
                return;
            clockBpm = rounded;
            applyEchoTime();
        }

        void tap (double nowSeconds)
        {
            tapper.tap (nowSeconds);
            applyEchoTime();
        }

        std::optional<double> tempoBpm() const { return clockBpm ? clockBpm : tapper.bpm(); }
        const char* tempoSource() const { return clockBpm ? "CLOCK" : tapper.bpm() ? "TAP" : "NO TEMPO"; }
        double effectiveMs() const { return effectiveEchoMs (sync, division, tempoBpm(), freeMs); }

        RackParameters& parameters() noexcept { return params; }

    private:
        void applyEchoTime() { params.echoTimeMs.store ((float) effectiveMs(), std::memory_order_relaxed); }

        RackParameters& params;
        std::optional<double> clockBpm;
        TapTempo tapper;
    };
}
