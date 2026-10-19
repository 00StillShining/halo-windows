#pragma once

#include <algorithm>
#include <optional>

// Port of EP40DisplayState.swift and EP40DisplayPreviewSource.swift.
namespace halo
{
    // Where the replica display's pixels came from. A USB connection alone is
    // deliberately not "live": live begins only after an observed MIDI event.
    enum class FeedMode { preview, waiting, live };

    // The five labelled areas along the lower edge of the EP-40 display.
    enum class DisplayMode { sound = 0, main, tempo, erase, system };

    struct DisplayState
    {
        FeedMode feedMode = FeedMode::preview;
        DisplayMode mode = DisplayMode::sound;
        int value = 0;
        std::optional<int> activeGroup;
        std::optional<int> activePadIndex;
        float velocity = 0.0f;
        float leftMeter = 0.0f;
        float rightMeter = 0.0f;
        bool isPlaying = false;
        bool clockPulse = false;
        int activityStep = 0;

        bool operator== (const DisplayState&) const = default;

        void clamp()
        {
            value = std::clamp (value, 0, 999);
            velocity = std::clamp (velocity, 0.0f, 1.0f);
            leftMeter = std::clamp (leftMeter, 0.0f, 1.0f);
            rightMeter = std::clamp (rightMeter, 0.0f, 1.0f);
            if (activeGroup) activeGroup = std::clamp (*activeGroup, 0, 3);
            if (activePadIndex) activePadIndex = std::clamp (*activePadIndex, 0, 11);
        }

        static DisplayState previewStill()
        {
            return { FeedMode::preview, DisplayMode::sound, 40, 0, 3, 0.72f, 0.68f, 0.52f, true, true, 0 };
        }

        static DisplayState waiting()
        {
            return { FeedMode::waiting, DisplayMode::system, 0, std::nullopt, std::nullopt, 0, 0, 0, false, false, 0 };
        }

        static DisplayState liveIdle()
        {
            return { FeedMode::live, DisplayMode::main, 0, std::nullopt, std::nullopt, 0, 0, 0, false, false, 0 };
        }

        // Deterministic disconnected demo, six frames per second. Always labelled
        // PREVIEW in the UI: it is never presented as hardware truth.
        static DisplayState previewFrame (int tick)
        {
            static constexpr int values[] { 40, 72, 96, 120, 83, 64, 108, 77 };
            static constexpr float meters[] { 0.12f, 0.46f, 0.82f, 0.30f, 0.66f, 0.96f, 0.42f, 0.74f };

            const int safeTick = std::max (tick, 0);
            const int phraseBeat = (safeTick / 3) % 24;
            const int index = phraseBeat % 8;
            const float decay = (float) (2 - safeTick % 3) * 0.08f;

            DisplayState s;
            s.feedMode = FeedMode::preview;
            s.mode = (DisplayMode) ((phraseBeat / 5) % 5);
            s.value = values[index];
            s.activeGroup = (phraseBeat / 6) % 4;
            s.activePadIndex = phraseBeat % 12;
            s.velocity = std::min (meters[index] + decay, 1.0f);
            s.leftMeter = s.velocity;
            s.rightMeter = std::min (meters[(index + 3) % 8] * 0.84f + decay, 1.0f);
            s.isPlaying = true;
            s.clockPulse = safeTick % 3 == 0;
            s.activityStep = phraseBeat;
            return s;
        }
    };
}
