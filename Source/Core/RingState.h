#pragma once

#include <algorithm>
#include <cmath>
#include <optional>
#include <string>

// Port of HaloRingState.swift and HaloLifecycle.swift. Every state is backed by
// an observed app or device fact; neither is ever driven by preview frames.
namespace halo
{
    struct RingState
    {
        enum class Kind { disconnected, discovering, connected, monitoring, recording, transfer, error };

        struct Inputs
        {
            bool observerRunning = false;
            bool endpointConnected = false;
            std::optional<std::string> errorLabel;
            bool monitorEngaged = false;
            std::optional<double> recordingStartedAt;
            std::optional<double> transferProgress;
        };

        Kind kind = Kind::disconnected;
        double transferProgress = 0.0;
        std::string errorLabel;

        bool operator== (const RingState&) const = default;

        // Priority: error > transfer > recording > monitoring > connected > discovering.
        static RingState derive (const Inputs& i)
        {
            if (i.errorLabel) return { Kind::error, 0.0, *i.errorLabel };
            if (i.transferProgress) return { Kind::transfer, std::clamp (*i.transferProgress, 0.0, 1.0), {} };
            if (i.recordingStartedAt) return { Kind::recording, 0.0, {} };
            if (i.monitorEngaged) return { Kind::monitoring, 0.0, {} };
            if (i.endpointConnected) return { Kind::connected, 0.0, {} };
            if (i.observerRunning) return { Kind::discovering, 0.0, {} };
            return {};
        }

        // The short word for the HALO status chip.
        std::string statusWord() const
        {
            switch (kind)
            {
                case Kind::disconnected: return "DARK";
                case Kind::discovering:  return "SCAN";
                case Kind::connected:    return "LINK";
                case Kind::monitoring:   return "MON";
                case Kind::recording:    return "REC";
                case Kind::transfer:     return "TX " + std::to_string ((int) std::lround (transferProgress * 100.0)) + "%";
                case Kind::error:        return "ERR " + errorLabel;
            }
            return {};
        }

        bool isError() const { return kind == Kind::error; }
    };

    struct LifecyclePhase
    {
        enum class Kind { starting, waiting, ready, live, suspended, error };

        struct Inputs
        {
            bool observerRunning = false;
            bool endpointConnected = false;
            bool displayLive = false;
            bool monitorEngaged = false;
            bool systemAsleep = false;
            std::optional<std::string> errorLabel;
        };

        Kind kind = Kind::starting;
        std::string errorLabel;

        bool operator== (const LifecyclePhase&) const = default;

        static LifecyclePhase derive (const Inputs& i)
        {
            if (i.errorLabel) return { Kind::error, *i.errorLabel };
            if (i.systemAsleep) return { Kind::suspended, {} };
            if (i.displayLive || i.monitorEngaged) return { Kind::live, {} };
            if (i.endpointConnected) return { Kind::ready, {} };
            if (i.observerRunning) return { Kind::waiting, {} };
            return {};
        }

        std::string word() const
        {
            switch (kind)
            {
                case Kind::starting:  return "STARTING";
                case Kind::waiting:   return "WAIT";
                case Kind::ready:     return "READY";
                case Kind::live:      return "LIVE";
                case Kind::suspended: return "SLEEP";
                case Kind::error:     return "ERR " + errorLabel;
            }
            return {};
        }

        bool isError() const { return kind == Kind::error; }
    };
}
