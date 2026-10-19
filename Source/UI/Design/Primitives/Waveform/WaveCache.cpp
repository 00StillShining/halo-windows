#include "UI/Design/Primitives/Waveform/WaveCache.h"

#include <cmath>

namespace halo
{
    WaveCache::WaveCache (Post p) : post (std::move (p))
    {
        if (post == nullptr)
            post = [] (std::function<void()> f) { juce::MessageManager::callAsync (std::move (f)); };
    }

    WaveCache::~WaveCache()
    {
        shared->alive = false;
        ++shared->generation; // any running build stops at its next check
    }

    void WaveCache::setSource (std::shared_ptr<const std::vector<float>> samples)
    {
        source = std::move (samples);
        haveReady = false;
        pendingColumns = 0;
        pendingStart = pendingEnd = -1;
        ++shared->generation;
    }

    std::thread::id WaveCache::lastBuildThread() const
    {
        std::lock_guard<std::mutex> l (shared->threadLock);
        return shared->lastThread;
    }

    WaveCache::Columns WaveCache::build (const std::vector<float>& s, int columns, std::int64_t start, std::int64_t end,
                                         const std::atomic<int>* cancel, int generation)
    {
        Columns c;
        c.count = std::max (0, columns);
        c.start = start;
        c.end = end;
        c.mins.assign ((size_t) c.count, 0.0f);
        c.maxs.assign ((size_t) c.count, 0.0f);
        const auto n = (std::int64_t) s.size();
        if (c.count == 0 || n == 0 || end <= start)
            return c;
        const double span = (double) (end - start);
        for (int i = 0; i < c.count; ++i)
        {
            if (cancel != nullptr && (i & 63) == 0 && cancel->load (std::memory_order_relaxed) != generation)
                return {};
            auto a = start + (std::int64_t) std::floor (span * i / c.count);
            auto b = start + (std::int64_t) std::floor (span * (i + 1) / c.count);
            a = std::clamp<std::int64_t> (a, 0, n - 1);
            b = std::clamp<std::int64_t> (std::max (b, a + 1), 1, n);
            float lo = s[(size_t) a], hi = lo;
            for (auto k = a + 1; k < b; ++k)
            {
                const float v = s[(size_t) k];
                lo = std::min (lo, v);
                hi = std::max (hi, v);
            }
            c.mins[(size_t) i] = lo;
            c.maxs[(size_t) i] = hi;
            c.peak = std::max ({ c.peak, std::abs (lo), std::abs (hi) });
        }
        return c;
    }

    const WaveCache::Columns* WaveCache::get (int columns, std::int64_t start, std::int64_t end)
    {
        if (source == nullptr || columns <= 0)
            return nullptr;
        if (haveReady && ready.count == columns && ready.start == start && ready.end == end)
        {
            if (pendingColumns > 0)
            {
                // Back to what's already built: drop the build in flight.
                ++shared->generation;
                ++shared->cancelled;
                pendingColumns = 0;
                pendingStart = pendingEnd = -1;
            }
            return &ready;
        }
        if (pendingColumns == columns && pendingStart == start && pendingEnd == end)
            return haveReady ? &ready : nullptr; // still building; keep showing the old one
        // A new request: cancel the old build and start this one.
        if (pendingColumns > 0)
            ++shared->cancelled;
        pendingColumns = columns;
        pendingStart = start;
        pendingEnd = end;
        const int generation = ++shared->generation;
        ++started;
        juce::Thread::launch ([samples = source, columns, start, end, generation, state = shared, post = post, this]
        {
            {
                std::lock_guard<std::mutex> l (state->threadLock);
                state->lastThread = std::this_thread::get_id();
            }
            auto result = std::make_shared<Columns> (build (*samples, columns, start, end, &state->generation, generation));
            if (state->generation.load() != generation || ! state->alive.load())
                return;
            post ([this, result, generation, state]
            {
                if (! state->alive.load() || state->generation.load() != generation)
                    return;
                ready = std::move (*result);
                haveReady = true;
                pendingColumns = 0;
                pendingStart = pendingEnd = -1;
                ++delivered;
                if (onReady)
                    onReady();
            });
        });
        return haveReady ? &ready : nullptr;
    }
}
