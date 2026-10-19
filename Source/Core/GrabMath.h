#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

// Port of GrabMath.swift: pure maths behind GRAB (instant loop capture).
namespace halo::grab
{
    struct Window
    {
        std::int64_t startFrame = 0;
        std::int64_t endFrame = 0;
        int bars = 0;

        std::int64_t frames() const noexcept { return endFrame - startFrame; }
        bool operator== (const Window&) const = default;
    };

    inline constexpr int barChoices[] { 4, 8, 16 };
    inline constexpr int secondsChoices[] { 5, 10, 30 };

    double framesPerBeat (double sampleRate, double bpm);

    // Whole bars ending exactly at the last downbeat. Shrinks the bar count
    // until it fits in resident history; 0 bars means nothing to grab.
    Window barWindow (double sampleRate, double bpm, int beatsPerBar, int bars,
                      std::int64_t lastDownbeatFrame, std::int64_t oldestResident);

    // The last N seconds, clamped to resident history.
    Window secondsWindow (double sampleRate, double seconds, std::int64_t now, std::int64_t oldestResident);

    // Nearest rising zero crossing (mono[i-1] < 0 <= mono[i]) within radius of
    // target, so both loop ends sit on the same slope; target if none.
    int snapToZeroCrossing (const std::vector<float>& mono, int target, int radius);

    // A reference-only tempo estimate from onset autocorrelation. nullopt when
    // the signal is too flat to call a tempo honestly.
    std::optional<double> estimateBpm (const std::vector<float>& mono, double sampleRate,
                                       double minBpm = 60.0, double maxBpm = 180.0);

    struct Stamp
    {
        int year, month, day, hour, minute, second;
    };

    struct Kind
    {
        bool clocked = false;
        int bars = 0;             // clocked
        int bpm = 0;              // clocked: observed
        int seconds = 0;          // unclocked
        std::optional<int> estBpm; // unclocked: estimate
    };

    // grab-YYYY-MM-DD-HHmm-ss-08bars-92bpm.wav, grab-...-10s-est92bpm.wav or
    // grab-...-30s.wav. The file name is the only persistence (no database).
    std::string filename (const Kind&, const Stamp&);

    // "GRAB · 2026-07-13 21.42 · 8 BARS · 92 BPM", or nullopt for other files.
    std::optional<std::string> titleFromFilename (const std::string& name);
}
