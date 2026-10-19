#pragma once

#include <algorithm>
#include <atomic>
#include <cmath>
#include <limits>

// Real-time-safe monitor DSP, ported from MonitorGain.swift, SafetyLimiter.swift
// and AudioMeter.swift. No allocation, locks or logging: safe to call from the
// audio callback. JUCE hands audio over as separate channel arrays, so these
// take a left and a right pointer instead of the Mac's interleaved buffer.
namespace halo
{
    // Smoothed monitor gain. Starts at -12 dB; -60 dB or lower is true silence.
    class MonitorGain
    {
    public:
        static constexpr double silenceFloorDb = -60.0;
        static constexpr double defaultDb = -12.0;

        explicit MonitorGain (double db = defaultDb, double sampleRate = 48000.0, double smoothingSeconds = 0.02)
            : current (linearFromDb (db)), target (current)
        {
            prepare (sampleRate, smoothingSeconds);
        }

        void prepare (double sampleRate, double smoothingSeconds = 0.02)
        {
            coefficient = (float) std::exp (-1.0 / (std::max (1.0e-4, smoothingSeconds) * std::max (1.0, sampleRate)));
        }

        static float linearFromDb (double db)
        {
            return db <= silenceFloorDb ? 0.0f : (float) std::pow (10.0, db / 20.0);
        }

        // May be called from any thread; the audio thread picks it up.
        void setTargetDb (double db) { target.store (linearFromDb (db), std::memory_order_relaxed); }

        // Jump straight to a level (route start); not for use while processing.
        void reset (double db)
        {
            current = linearFromDb (db);
            target.store (current, std::memory_order_relaxed);
        }

        float currentLinear() const noexcept { return current; }

        void process (float* left, float* right, int frames) noexcept
        {
            const float goal = target.load (std::memory_order_relaxed);
            float g = current;
            for (int i = 0; i < frames; ++i)
            {
                g = goal + (g - goal) * coefficient;
                left[i] *= g;
                right[i] *= g;
            }
            // Snap once within epsilon (~-80 dB) so it settles exactly; in float
            // the exponential approach otherwise stalls just short of the goal.
            current = std::abs (g - goal) < 1.0e-4f ? goal : g;
        }

    private:
        float current;
        std::atomic<float> target;
        float coefficient = 0.0f;
    };

    // Brick-wall safety limiter at -1 dBFS: instant attack, so no sample ever
    // exceeds the ceiling, and a 100 ms release back to unity.
    class SafetyLimiter
    {
    public:
        static constexpr float ceiling = 0.8912509381337456f; // 10^(-1/20)

        explicit SafetyLimiter (double sampleRate = 48000.0, double releaseSeconds = 0.10)
        {
            prepare (sampleRate, releaseSeconds);
        }

        void prepare (double sampleRate, double releaseSeconds = 0.10)
        {
            releaseCoefficient = (float) std::exp (-1.0 / (std::max (1.0e-4, releaseSeconds) * std::max (1.0, sampleRate)));
            gain = 1.0f;
        }

        float currentGain() const noexcept { return gain; }
        void reset() noexcept { gain = 1.0f; }

        void process (float* left, float* right, int frames) noexcept
        {
            float g = gain;
            for (int i = 0; i < frames; ++i)
            {
                const float peak = std::max (std::abs (left[i]), std::abs (right[i]));
                const float required = peak > ceiling ? ceiling / peak : 1.0f;
                if (required < g)
                {
                    g = required;
                }
                else
                {
                    g = std::min (1.0f, g + (1.0f - g) * (1.0f - releaseCoefficient));
                    if (peak * g > ceiling)
                        g = required;
                }
                left[i] *= g;
                right[i] *= g;
            }
            gain = g;
        }

    private:
        float gain = 1.0f;
        float releaseCoefficient = 0.0f;
    };

    struct MeterMath
    {
        static float peak (const float* x, int frames) noexcept
        {
            float p = 0.0f;
            for (int i = 0; i < frames; ++i)
                p = std::max (p, std::abs (x[i]));
            return p;
        }

        static float rms (const float* x, int frames) noexcept
        {
            if (frames <= 0)
                return 0.0f;
            float sum = 0.0f;
            for (int i = 0; i < frames; ++i)
                sum += x[i] * x[i];
            return std::sqrt (sum / (float) frames);
        }

        static float linearToDb (float x) noexcept
        {
            return x <= 0.0f ? -std::numeric_limits<float>::infinity() : 20.0f * std::log10 (x);
        }

        static float ballistic (float previous, float sample, float attack, float release) noexcept
        {
            return previous + (sample >= previous ? attack : release) * (sample - previous);
        }
    };

    struct StereoLevels
    {
        float rmsL = -std::numeric_limits<float>::infinity(), rmsR = rmsL;
        float peakL = rmsL, peakR = rmsL;
        bool clippedL = false, clippedR = false;
        // The safety limiter acted: the pre-limiter peak went above its
        // -1 dBFS ceiling (the `limit` lamp on the level arcs).
        bool limitedL = false, limitedR = false;
    };

    // Lock-free stereo meter: the audio thread publishes, the UI snapshots.
    class AudioMeter
    {
    public:
        explicit AudioMeter (double sampleRate = 48000.0, double attackSeconds = 0.005, double releaseSeconds = 0.30)
        {
            prepare (sampleRate, attackSeconds, releaseSeconds);
        }

        void prepare (double sampleRate, double attackSeconds = 0.005, double releaseSeconds = 0.30)
        {
            const double sr = std::max (1.0, sampleRate);
            attack = (float) (1.0 - std::exp (-1.0 / (std::max (1.0e-4, attackSeconds) * sr)));
            release = (float) (1.0 - std::exp (-1.0 / (std::max (1.0e-4, releaseSeconds) * sr)));
        }

        // Audio thread. rawPeak* are pre-limiter peaks, so the clip flag shows
        // a real overload the limiter then caught.
        void publish (const float* left, const float* right, int frames, float rawPeakL, float rawPeakR) noexcept
        {
            heldPeakL = MeterMath::ballistic (heldPeakL, MeterMath::peak (left, frames), 1.0f, blockRelease (frames));
            heldPeakR = MeterMath::ballistic (heldPeakR, MeterMath::peak (right, frames), 1.0f, blockRelease (frames));
            heldRmsL = MeterMath::ballistic (heldRmsL, MeterMath::rms (left, frames), blockAttack (frames), blockRelease (frames));
            heldRmsR = MeterMath::ballistic (heldRmsR, MeterMath::rms (right, frames), blockAttack (frames), blockRelease (frames));

            peakL.store (heldPeakL, std::memory_order_relaxed);
            peakR.store (heldPeakR, std::memory_order_relaxed);
            rmsL.store (heldRmsL, std::memory_order_relaxed);
            rmsR.store (heldRmsR, std::memory_order_relaxed);

            unsigned clip = (rawPeakL >= 1.0f ? 1u : 0u) | (rawPeakR >= 1.0f ? 2u : 0u);
            if (clip != 0)
                clipBits.fetch_or (clip, std::memory_order_relaxed);
            unsigned limit = (rawPeakL > SafetyLimiter::ceiling ? 1u : 0u) | (rawPeakR > SafetyLimiter::ceiling ? 2u : 0u);
            if (limit != 0)
                limitBits.fetch_or (limit, std::memory_order_relaxed);
        }

        // UI thread. Reading clears the clip flags.
        StereoLevels snapshot() noexcept
        {
            const unsigned clip = clipBits.exchange (0, std::memory_order_relaxed);
            const unsigned limit = limitBits.exchange (0, std::memory_order_relaxed);
            return { MeterMath::linearToDb (rmsL.load (std::memory_order_relaxed)),
                     MeterMath::linearToDb (rmsR.load (std::memory_order_relaxed)),
                     MeterMath::linearToDb (peakL.load (std::memory_order_relaxed)),
                     MeterMath::linearToDb (peakR.load (std::memory_order_relaxed)),
                     (clip & 1u) != 0, (clip & 2u) != 0,
                     (limit & 1u) != 0, (limit & 2u) != 0 };
        }

        // Only while the audio callback isn't running.
        void reset() noexcept
        {
            heldPeakL = heldPeakR = heldRmsL = heldRmsR = 0.0f;
            peakL = peakR = rmsL = rmsR = 0.0f;
            clipBits = 0;
            limitBits = 0;
        }

    private:
        // The Mac publishes per-sample coefficients once per block; scale them
        // to the block length so ballistics don't depend on buffer size.
        float blockAttack (int frames) const noexcept { return 1.0f - std::pow (1.0f - attack, (float) std::max (1, frames)); }
        float blockRelease (int frames) const noexcept { return 1.0f - std::pow (1.0f - release, (float) std::max (1, frames)); }

        float attack = 0.0f, release = 0.0f;
        float heldPeakL = 0.0f, heldPeakR = 0.0f, heldRmsL = 0.0f, heldRmsR = 0.0f;
        std::atomic<float> peakL { 0.0f }, peakR { 0.0f }, rmsL { 0.0f }, rmsR { 0.0f };
        std::atomic<unsigned> clipBits { 0 };
        std::atomic<unsigned> limitBits { 0 };
    };
}
