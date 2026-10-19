#pragma once

#include <array>
#include <atomic>
#include <cmath>
#include <optional>
#include <vector>

// The dub FX rack (port of DubRack.swift, RackParameters.swift, RackModel's
// tempo logic): TAPE ECHO -> SPRING -> SWEEP -> LOW END at the monitor insert,
// post-gain / pre-limiter. Real-time safe: all memory is allocated in
// prepare(); process() never allocates, locks or logs.
//
// Fixed while porting: SWEEP and LOW END smoothed their controls once per block
// with a per-sample coefficient, so a 20 ms glide took ~10 s at 480-frame
// blocks. Block-rate smoothing now uses coefficient^frames.
namespace halo
{
    // Lock-free UI -> audio parameter bridge.
    struct RackParameters
    {
        std::atomic<bool> engaged { false };

        std::atomic<bool> echoOn { true };
        std::atomic<float> echoTimeMs { 360.0f }, echoFeedback { 0.45f }, echoTone { 0.5f }, echoWow { 0.25f }, echoMix { 0.35f };

        std::atomic<bool> springOn { false };
        std::atomic<float> springMix { 0.30f }, springDecay { 0.55f }, springTone { 0.5f };

        std::atomic<bool> sweepOn { false };
        std::atomic<float> sweepMacro { 0.5f }, sweepReso { 0.3f };

        std::atomic<bool> lowOn { false };
        std::atomic<float> lowAmount { 0.4f };
    };

    struct RackTargets
    {
        bool engaged;
        bool echoOn; float echoTimeMs, echoFeedback, echoTone, echoWow, echoMix;
        bool springOn; float springMix, springDecay, springTone;
        bool sweepOn; float sweepMacro, sweepReso;
        bool lowOn; float lowAmount;

        static RackTargets load (const RackParameters&) noexcept;
    };

    class DubRack
    {
    public:
        void prepare (double sampleRate, int maxBlock);
        void reset() noexcept;

        // In place, any block length (processed in chunks of maxBlock).
        void process (float* left, float* right, int frames, const RackParameters&) noexcept;

        // True once fully bypassed (the null-test state).
        bool isSettledBypass() const noexcept { return masterMix == 0.0f; }

    private:
        struct Echo
        {
            void prepare (float sampleRate);
            void reset() noexcept;
            void process (float* l, float* r, int n, const RackTargets&) noexcept;
            float tap (const std::vector<float>& buf, float delay) const noexcept;

            float sr = 48000.0f;
            int size = 0, writeIdx = 0;
            std::vector<float> bufL, bufR;
            float dampL = 0, dampR = 0, wowPhase = 0, flutPhase = 0, wowInc = 0, flutInc = 0, maxMod = 0;
            float enable = 0, timeSamples = 0, feedback = 0, tone = 0.5f, wow = 0, mix = 0;
            float ctrlCoef = 0, timeCoef = 0;
        };

        struct Spring
        {
            void prepare (float sampleRate);
            void reset() noexcept;
            void process (float* l, float* r, int n, const RackTargets&) noexcept;

            std::array<std::vector<float>, 4> lines;
            std::array<int, 4> idx {};
            std::array<float, 4> damp {};
            float enable = 0, mix = 0, decay = 0.55f, tone = 0.5f, ctrlCoef = 0;
        };

        struct Sweep
        {
            void prepare (float sampleRate);
            void reset() noexcept;
            void process (float* l, float* r, int n, const RackTargets&) noexcept;

            float sr = 48000.0f;
            float ic1L = 0, ic2L = 0, ic1R = 0, ic2R = 0;
            float enable = 0, macro = 0.5f, reso = 0.3f, ctrlCoef = 0;
        };

        struct LowEnd
        {
            void prepare (float sampleRate);
            void reset() noexcept;
            void process (float* l, float* r, int n, const RackTargets&) noexcept;

            float sr = 48000.0f;
            float x1L = 0, x2L = 0, y1L = 0, y2L = 0, x1R = 0, x2R = 0, y1R = 0, y2R = 0;
            float enable = 0, amount = 0, ctrlCoef = 0;
        };

        void processChunk (float* left, float* right, int n, const RackTargets&) noexcept;

        Echo echo;
        Spring spring;
        Sweep sweep;
        LowEnd lowEnd;
        std::vector<float> wetL, wetR;
        float masterMix = 0.0f, masterCoef = 0.0f;
        int maxFrames = 0;
    };

    // Echo time from tempo (RackModel.swift).
    enum class EchoDivision { eighth, dottedEighth, quarter, tripletEighth };

    double beatFraction (EchoDivision);
    const char* label (EchoDivision);

    // Synced: a division of the beat at bpm; free (or no tempo): freeMs.
    // Always clamped to 40...1200 ms.
    double effectiveEchoMs (bool sync, EchoDivision, std::optional<double> bpm, double freeMs);

    // Tap tempo: mean of up to the last 4 intervals; a 2 s gap starts over.
    class TapTempo
    {
    public:
        std::optional<double> tap (double nowSeconds);
        std::optional<double> bpm() const noexcept { return current; }
        void clear() noexcept { count = 0; current.reset(); }

    private:
        std::array<double, 5> times {};
        int count = 0;
        std::optional<double> current;
    };
}
