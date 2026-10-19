#pragma once

#include "Core/RollingCaptureBuffer.h"

#include <juce_audio_formats/juce_audio_formats.h>
#include <juce_events/juce_events.h>

#include <functional>
#include <memory>
#include <optional>

namespace halo
{
    // GRAB: freeze the last few bars (clocked, transport playing) or the last
    // few seconds (otherwise) from the always-on history into a loop take.
    // Port of GrabController.swift. Differences:
    //  - the snapshot, trim, estimate and WAV write run on a background thread
    //    (the Mac did it all on the main thread, finding #8);
    //  - bar mode needs the transport playing: the EP-40 sends clock while
    //    stopped, and bar phase means nothing then;
    //  - downbeats are exact audio frames from MIDI timestamps.
    class GrabController
    {
    public:
        struct Result
        {
            enum class Kind { grabbed, truncated, emptyHistory, notMonitoring, failed };
            Kind kind = Kind::failed;
            juce::File file;
            double seconds = 0.0;
            int askedBars = 0, gotBars = 0;
            juce::String message;
        };

        GrabController();
        ~GrabController();

        // Called on the message thread when a grab finishes.
        std::function<void (const Result&)> onFinished;

        int bars = 8;     // 4, 8 or 16
        int seconds = 10; // 5, 10 or 30

        void setTransportPlaying (bool playing);
        void markDownbeat (std::int64_t frame) noexcept { lastDownbeat = frame; }
        void clockLost() noexcept { lastDownbeat.reset(); }

        // Bar mode is available right now (fresh clock, playing, a downbeat seen).
        bool canGrabBars (std::optional<double> bpm) const noexcept { return bpm && playing && lastDownbeat; }
        bool isBusy() const noexcept { return busy; }

        // history may be null when no route runs. bpm is the observed clock tempo.
        void grab (std::shared_ptr<RollingCaptureBuffer> history, double sampleRate, std::optional<double> bpm);

    private:
        static Result run (std::shared_ptr<RollingCaptureBuffer> history, double sampleRate,
                           std::optional<double> bpm, std::optional<std::int64_t> downbeat,
                           int bars, int seconds);

        bool playing = false;
        bool busy = false;
        std::optional<std::int64_t> lastDownbeat;
        std::shared_ptr<bool> alive = std::make_shared<bool> (true);
    };
}
