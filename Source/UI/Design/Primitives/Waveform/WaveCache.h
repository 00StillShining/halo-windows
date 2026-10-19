#pragma once

#include <juce_events/juce_events.h>

#include <algorithm>
#include <atomic>
#include <cstdint>
#include <functional>
#include <memory>
#include <mutex>
#include <thread>
#include <vector>

namespace halo
{
    // One cache of min/max per pixel column (docs/redesign/plan.md section
    // 2.5), shared by the SampleBand (EDIT) and the Slicer (CHOP). Columns are
    // built on a background thread for a given width and frame span; the
    // result is handed back on the message thread, and a newer request (a
    // resize, a zoom) cancels an older build cleanly. The overlay (markers,
    // playhead, selection) repaints without touching the cache.
    class WaveCache
    {
    public:
        struct Columns
        {
            int count = 0;
            std::int64_t start = 0, end = 0;
            std::vector<float> mins, maxs;
            float peak = 0.0f; // largest |value| in the span, for scaling
        };

        // `post` hands a finished build to the message thread; tests may drain
        // their own queue instead of MessageManager::callAsync.
        using Post = std::function<void (std::function<void()>)>;
        explicit WaveCache (Post post = {});
        ~WaveCache();

        void setSource (std::shared_ptr<const std::vector<float>> samples);
        std::int64_t sourceLength() const noexcept { return source != nullptr ? (std::int64_t) source->size() : 0; }

        // The columns for this request if they're ready; otherwise null, and a
        // build starts (cancelling any older one). onReady fires when it lands.
        const Columns* get (int columns, std::int64_t start, std::int64_t end);
        std::function<void()> onReady;

        // Pure: the columns for a span (used by the worker and by tests).
        static Columns build (const std::vector<float>&, int columns, std::int64_t start, std::int64_t end,
                              const std::atomic<int>* cancel = nullptr, int generation = 0);

        // Test hooks.
        int buildsStarted() const noexcept { return started; }
        int buildsDelivered() const noexcept { return delivered; }
        int buildsCancelled() const noexcept { return shared->cancelled.load(); }
        std::thread::id lastBuildThread() const;

    private:
        struct Shared
        {
            std::atomic<int> generation { 0 };
            std::atomic<int> cancelled { 0 };
            std::atomic<bool> alive { true };
            std::mutex threadLock;
            std::thread::id lastThread;
        };

        Post post;
        std::shared_ptr<Shared> shared = std::make_shared<Shared>();
        std::shared_ptr<const std::vector<float>> source;
        Columns ready;
        bool haveReady = false;
        int pendingColumns = 0;
        std::int64_t pendingStart = -1, pendingEnd = -1;
        int started = 0, delivered = 0;
    };
}
