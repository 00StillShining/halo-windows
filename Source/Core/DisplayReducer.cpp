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
        resetTiming();
    }

    void ClockTracker::resetTiming() noexcept
    {
        stored = 0;
        head = 0;
        lastClock.reset();
    }

    void ClockTracker::tick (double timeSeconds) noexcept
    {
        ++count;

        // A long gap (or time going backwards) means the clock restarted:
        // don't average across it.
        if (lastClock && (timeSeconds - *lastClock > gapResetSeconds || timeSeconds < *lastClock))
        {
            stored = 0;
            head = 0;
        }

        times[(std::size_t) head] = timeSeconds;
        head = (head + 1) % (int) times.size();
        stored = std::min (stored + 1, (int) times.size());
        lastClock = timeSeconds;
    }

    bool ClockTracker::isStale (double nowSeconds) const noexcept
    {
        return ! lastClock || nowSeconds - *lastClock > staleAfterSeconds;
    }

    std::optional<double> ClockTracker::secondsPerClock (double nowSeconds) const noexcept
    {
        if (isStale (nowSeconds) || stored < minimumTicks + 1)
            return std::nullopt;

        const auto size = (int) times.size();
        const double newest = times[(std::size_t) ((head - 1 + size) % size)];
        const double oldest = times[(std::size_t) ((head - stored + size) % size)];
        const double span = newest - oldest;
        if (span <= 0.0)
            return std::nullopt;
        return span / (double) (stored - 1);
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
                // Unlike the Mac app, Stop also forgets the tempo so nothing
                // downstream keeps using a stale one. The bar phase survives
                // so Continue resumes in phase.
                clockTracker.resetTiming();
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
        if (current.feedMode != FeedMode::live || ! clockTracker.wentQuiet (nowSeconds))
            return false;

        clockTracker.reset();
        current.clockPulse = false;
        return true;
    }
}
