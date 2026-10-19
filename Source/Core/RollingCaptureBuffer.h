#pragma once

#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstdint>
#include <vector>

namespace halo
{
    // Always-on stereo history for GRAB (port of RollingCaptureBuffer.swift).
    // One producer (the audio thread) writes; the message thread snapshots.
    // A frame counter that only grows makes torn reads detectable: a snapshot
    // re-checks the counter afterwards and refuses if the producer overran it.
    //
    // New on Windows: the audio thread also records which frame was written at
    // which moment, so a MIDI clock timestamp converts to an exact audio frame
    // (the Mac stamped downbeats when the message reached the UI thread).
    class RollingCaptureBuffer
    {
    public:
        static constexpr std::int64_t guardFrames = 4096;

        explicit RollingCaptureBuffer (double seconds = 60.0, double sampleRate = 48000.0)
        {
            const auto want = (std::int64_t) std::llround (seconds * sampleRate);
            std::int64_t cap = 2;
            while (cap < want)
                cap <<= 1;
            capacity = cap;
            mask = cap - 1;
            left.assign ((size_t) cap, 0.0f);
            right.assign ((size_t) cap, 0.0f);
        }

        std::int64_t capacityFrames() const noexcept { return capacity; }

        // Audio thread. nowSeconds is when this block reached the app;
        // latencyFrames is the input latency, i.e. how long ago the block's last
        // sample was captured, so the frame being captured "now" lies that far
        // past the end of what has been written.
        void write (const float* l, const float* r, int frames, double nowSeconds = 0.0, std::int64_t latencyFrames = 0) noexcept
        {
            if (frames <= 0)
                return;
            const auto w = written.load (std::memory_order_relaxed);
            const auto start = w & mask;
            const auto first = std::min<std::int64_t> (frames, capacity - start);
            std::copy (l, l + first, left.data() + start);
            std::copy (r, r + first, right.data() + start);
            if (first < frames)
            {
                std::copy (l + first, l + frames, left.data());
                std::copy (r + first, r + frames, right.data());
            }

            // Seqlock-publish the (frame, time) anchor for frameAt().
            anchorVersion.fetch_add (1, std::memory_order_acq_rel);
            anchorFrame.store (w + frames + latencyFrames, std::memory_order_relaxed);
            anchorTime.store (nowSeconds, std::memory_order_relaxed);
            anchorVersion.fetch_add (1, std::memory_order_release);

            written.store (w + frames, std::memory_order_release);
        }

        std::int64_t nowFrame() const noexcept { return written.load (std::memory_order_acquire); }

        // Forget history from before this point (a new route or session).
        void resetSession() noexcept { floorFrame.store (nowFrame(), std::memory_order_release); }
        std::int64_t floor() const noexcept { return floorFrame.load (std::memory_order_acquire); }

        std::int64_t oldestResidentFrame() const noexcept
        {
            return std::max (floor(), nowFrame() - capacity + guardFrames);
        }

        // The audio frame that was being captured at timeSeconds (same clock as
        // write()'s nowSeconds), or -1 before any audio has arrived.
        std::int64_t frameAt (double timeSeconds, double sampleRate) const noexcept
        {
            for (int attempt = 0; attempt < 4; ++attempt)
            {
                const auto v0 = anchorVersion.load (std::memory_order_acquire);
                if (v0 & 1)
                    continue;
                const auto frame = anchorFrame.load (std::memory_order_relaxed);
                const auto time = anchorTime.load (std::memory_order_relaxed);
                if (anchorVersion.load (std::memory_order_acquire) == v0)
                    return v0 == 0 ? -1 : frame + (std::int64_t) std::llround ((timeSeconds - time) * sampleRate);
            }
            return nowFrame();
        }

        // Copies [startFrame, endFrame) into l/r (each at least that long).
        // Returns false if the range isn't resident or was overwritten mid-copy.
        bool snapshot (std::int64_t startFrame, std::int64_t endFrame, float* l, float* r) const noexcept
        {
            const auto n = endFrame - startFrame;
            const auto w0 = nowFrame();
            if (n <= 0 || endFrame > w0 || startFrame < std::max (floor(), w0 - capacity))
                return false;

            for (auto f = startFrame, o = (std::int64_t) 0; f < endFrame;)
            {
                const auto idx = f & mask;
                const auto run = std::min (endFrame - f, capacity - idx);
                std::copy (left.data() + idx, left.data() + idx + run, l + o);
                std::copy (right.data() + idx, right.data() + idx + run, r + o);
                f += run;
                o += run;
            }
            return nowFrame() - startFrame <= capacity; // oldest frame still intact
        }

    private:
        std::int64_t capacity = 0, mask = 0;
        std::vector<float> left, right;
        std::atomic<std::int64_t> written { 0 }, floorFrame { 0 };
        std::atomic<std::uint64_t> anchorVersion { 0 };
        std::atomic<std::int64_t> anchorFrame { 0 };
        std::atomic<double> anchorTime { 0.0 };
    };
}
