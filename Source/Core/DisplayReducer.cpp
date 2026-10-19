#include "Core/DisplayReducer.h"
#include "Core/Ep40MidiMapping.h"

#include <algorithm>
#include <cmath>

namespace halo
{
    //==============================================================================
    void ClockTracker::reset() noexcept
    {
        count = 0;
        lastClock.reset();
        smoothedInterval.reset();
    }

    void ClockTracker::tick (double timeSeconds) noexcept
    {
        ++count;

        if (lastClock)
        {
            const double interval = timeSeconds - *lastClock;
            if (interval > 0.001 && interval < 0.20)
                smoothedInterval = smoothedInterval ? *smoothedInterval * 0.84 + interval * 0.16 : interval;
            else
                smoothedInterval.reset();
        }
        lastClock = timeSeconds;
    }

    bool ClockTracker::isStale (double nowSeconds) const noexcept
    {
        return ! lastClock || nowSeconds - *lastClock > staleAfterSeconds;
    }

    std::optional<double> ClockTracker::secondsPerClock (double nowSeconds) const noexcept
    {
        if (isStale (nowSeconds) || ! smoothedInterval || *smoothedInterval <= 0.0)
            return std::nullopt;
        return smoothedInterval;
    }

    std::optional<double> ClockTracker::bpm (double nowSeconds) const noexcept
    {
        const auto interval = secondsPerClock (nowSeconds);
        if (! interval)
            return std::nullopt;

        const double value = 60.0 / (*interval * clocksPerQuarter);
        if (value < 30.0 || value > 300.0)
            return std::nullopt;
        return value;
    }

    //==============================================================================
    DisplayReducer::DisplayReducer() : current (DisplayState::previewStill()) {}

    void DisplayReducer::connectionChanged (bool connected)
    {
        held.clear();
        clockTracker.reset();
        lastNoteTime = -1.0e9;
        current = connected ? DisplayState::waiting() : DisplayState::previewStill();
    }

    bool DisplayReducer::showPreview (const DisplayState& frame)
    {
        if (current.feedMode != FeedMode::preview || current == frame)
            return false;
        current = frame;
        return true;
    }

    void DisplayReducer::applyPadMapping (std::uint8_t note, DisplayState& state)
    {
        // Group from note range and grid position are documented inferences
        // shared with the tests (Ep40MidiMapping).
        state.activeGroup = ep40::groupForNote (note);
        state.activePadIndex = ep40::gridIndexForNote (note);
    }

    bool DisplayReducer::apply (const MidiEvent& event, double nowSeconds)
    {
        const bool firstLiveEvent = current.feedMode != FeedMode::live;
        DisplayState state = firstLiveEvent ? DisplayState::liveIdle() : current;
        state.feedMode = FeedMode::live;
        state.activityStep += 1;

        switch (event.kind)
        {
            case MidiKind::noteOn:
            {
                const HeldKey key { event.channel, event.data1 };
                std::erase (held, key);
                held.push_back (key);
                lastNoteTime = nowSeconds;

                state.mode = DisplayMode::sound;
                state.value = event.data1;
                state.velocity = (float) event.data2 / 127.0f;
                state.leftMeter = state.velocity;
                state.rightMeter = std::min (state.velocity * 0.82f + 0.08f, 1.0f);
                state.clockPulse = true;
                applyPadMapping (event.data1, state);
                break;
            }

            case MidiKind::noteOff:
            {
                const HeldKey key { event.channel, event.data1 };
                const bool wasFrontmost = ! held.empty() && held.back() == key;
                std::erase (held, key);

                if (wasFrontmost && ! held.empty())
                {
                    state.value = held.back().note;
                    applyPadMapping (held.back().note, state);
                    state.velocity *= 0.64f;
                }
                else if (held.empty())
                {
                    state.activeGroup.reset();
                    state.activePadIndex.reset();
                    state.velocity = 0.0f;
                }
                state.leftMeter *= 0.52f;
                state.rightMeter *= 0.52f;
                state.clockPulse = false;
                break;
            }

            case MidiKind::controlChange:
            {
                state.mode = DisplayMode::sound;
                state.value = event.data2;
                const float normalised = (float) event.data2 / 127.0f;
                if (event.data1 == 13)
                    state.rightMeter = normalised;
                else
                    state.leftMeter = normalised;
                state.clockPulse = event.data1 == 12 || event.data1 == 13;
                break;
            }

            case MidiKind::start:
                clockTracker.reset();
                state.mode = DisplayMode::main;
                state.isPlaying = true;
                state.clockPulse = true;
                break;

            case MidiKind::continuePlaying:
                state.mode = DisplayMode::main;
                state.isPlaying = true;
                state.clockPulse = true;
                break;

            case MidiKind::stop:
                // Unlike the Mac app, Stop also forgets the clock so nothing
                // downstream keeps using a stale tempo.
                clockTracker.reset();
                state.mode = DisplayMode::main;
                state.isPlaying = false;
                state.clockPulse = false;
                break;

            case MidiKind::songPosition:
                state.mode = DisplayMode::main;
                state.value = event.songPosition % 1000;
                state.activityStep = event.songPosition;
                // Song position counts sixteenths (6 clocks each); keep bar phase.
                clockTracker.setClockCount ((event.songPosition % 16) * 6);
                break;

            case MidiKind::timingClock:
            {
                clockTracker.tick (event.timeSeconds);

                // 24 PPQN: eight visual updates per quarter note keep the feel
                // without redrawing on every message.
                if (! firstLiveEvent && clockTracker.clockCount() % 3 != 0)
                    return false;

                state.clockPulse = clockTracker.clockCount() % 6 == 0;
                state.leftMeter *= 0.92f;
                state.rightMeter *= 0.90f;

                if (nowSeconds - lastNoteTime > 0.45)
                    if (const auto bpm = clockTracker.bpm (event.timeSeconds))
                    {
                        state.mode = DisplayMode::tempo;
                        state.value = (int) std::lround (*bpm);
                    }
                break;
            }
        }

        state.clamp();
        current = state;
        return true;
    }

    bool DisplayReducer::expire (double nowSeconds)
    {
        if (current.feedMode != FeedMode::live || clockTracker.clockCount() == 0 || ! clockTracker.isStale (nowSeconds))
            return false;

        clockTracker.reset();
        current.clockPulse = false;
        return true;
    }
}
