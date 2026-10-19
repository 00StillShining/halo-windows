#pragma once

#include <cstddef>
#include <optional>
#include <string>
#include <vector>

// Pure sample-handling core, ported from SampleProcessor.swift (buffer),
// WaveformSummary.swift, SamplePrep.swift, SampleTreatment.swift (rate maths)
// and SampleMemoryEstimator.swift. No JUCE: decoding, resampling and file
// writing live in Source/Audio/SampleIO.
namespace halo
{
    // Deinterleaved float audio at one sample rate.
    struct AudioData
    {
        std::vector<std::vector<float>> channels;
        double sampleRate = 0.0;

        int channelCount() const noexcept { return (int) channels.size(); }
        int frameCount() const noexcept { return channels.empty() ? 0 : (int) channels[0].size(); }
        double durationSeconds() const noexcept { return sampleRate > 0.0 ? frameCount() / sampleRate : 0.0; }

        // Average of all channels (for analysis and drawing, not for export).
        std::vector<float> monoMixdown() const;
    };

    // Min/max per bucket for drawing a waveform quickly.
    struct WaveformSummary
    {
        std::vector<float> minima, maxima;
        int framesPerBucket = 0;
        int sourceFrameCount = 0;

        static constexpr int defaultBuckets = 512;
        int bucketCount() const noexcept { return (int) minima.size(); }
        float peakMagnitude() const noexcept;

        static WaveformSummary build (const std::vector<float>& mono, int targetBuckets = defaultBuckets);
    };

    // Non-destructive preparation: trim -> channel mode -> gain -> equal-power
    // fades -> optional peak normalise. The source is never modified.
    struct SamplePrep
    {
        enum class Channels { preserve, mono, stereo };

        int trimStartFrame = 0;
        std::optional<int> trimEndFrame;
        int fadeInFrames = 0;
        int fadeOutFrames = 0;
        float gainDecibels = 0.0f;
        bool normalize = false;
        float normalizeTargetDecibels = -1.0f;
        Channels channelMode = Channels::preserve;

        bool operator== (const SamplePrep&) const = default;

        struct Range { int start, end; };
        Range resolvedTrim (int sourceFrames) const noexcept;
        int outputFrameCount (int sourceFrames) const noexcept;
        int outputChannelCount (int sourceChannels) const noexcept;

        AudioData apply (const AudioData& source) const;

        static float dbToLinear (float db) noexcept;
    };

    // EP-40-oriented export quality. The device stores 16-bit audio at up to
    // 46.875 kHz (TE spec); lower rates trade fidelity for memory.
    enum class Treatment { original, high, balanced, loFi22k, loFi11k };

    inline constexpr double deviceMaxSampleRate = 46875.0;
    inline constexpr int deviceBitDepth = 16;
    inline constexpr double deviceMemoryBytes = 122.0 * 1024.0 * 1024.0; // ~122 MB usable (TE docs; prefer device-reported)

    const char* label (Treatment);
    double resolvedSampleRate (Treatment, double sourceSampleRate);
    int outputFrameCount (Treatment, int sourceFrames, double sourceSampleRate);

    struct SizeEstimate
    {
        int frames = 0, channels = 0;
        double sampleRate = 0.0;
        long long payloadBytes = 0;
        double durationSeconds() const noexcept { return sampleRate > 0.0 ? frames / sampleRate : 0.0; }
    };

    // Exact payload bytes (frames x channels x 2) after prep and treatment.
    SizeEstimate estimateSize (int sourceFrames, int sourceChannels, double sourceRate,
                               const SamplePrep&, Treatment);

    std::string formatBytes (long long bytes);
}
