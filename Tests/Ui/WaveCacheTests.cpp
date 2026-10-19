#include "UI/Design/Primitives/Waveform/WaveCache.h"

#include <doctest/doctest.h>

#include <chrono>
#include <cmath>
#include <deque>
#include <mutex>

using namespace halo;

namespace
{
    // A message queue the test drains itself, standing in for callAsync.
    struct Queue
    {
        std::mutex lock;
        std::deque<std::function<void()>> items;

        WaveCache::Post poster()
        {
            return [this] (std::function<void()> f)
            {
                std::lock_guard<std::mutex> l (lock);
                items.push_back (std::move (f));
            };
        }

        // Runs posted work on this (the "message") thread until `done` or a timeout.
        bool drainUntil (std::function<bool()> done, int timeoutMs = 4000)
        {
            const auto until = std::chrono::steady_clock::now() + std::chrono::milliseconds (timeoutMs);
            while (std::chrono::steady_clock::now() < until)
            {
                std::deque<std::function<void()>> now;
                {
                    std::lock_guard<std::mutex> l (lock);
                    now.swap (items);
                }
                for (auto& f : now)
                    f();
                if (done())
                    return true;
                std::this_thread::sleep_for (std::chrono::milliseconds (2));
            }
            return false;
        }
    };

    std::shared_ptr<std::vector<float>> sine (size_t n)
    {
        auto v = std::make_shared<std::vector<float>> (n);
        for (size_t i = 0; i < n; ++i)
            (*v)[i] = 0.8f * std::sin ((float) i * 0.001f) * (i % 7 == 0 ? 1.0f : 0.5f);
        return v;
    }
}

TEST_CASE ("wavecache: columns hold the true min and max of their frames")
{
    const auto s = sine (10000);
    const auto c = WaveCache::build (*s, 100, 0, 10000);
    REQUIRE (c.count == 100);
    for (int i = 0; i < 100; ++i)
    {
        float lo = 1.0f, hi = -1.0f;
        for (int k = i * 100; k < (i + 1) * 100; ++k)
        {
            lo = std::min (lo, (*s)[(size_t) k]);
            hi = std::max (hi, (*s)[(size_t) k]);
        }
        CHECK (c.mins[(size_t) i] == lo);
        CHECK (c.maxs[(size_t) i] == hi);
    }
    // Zoomed in further than one frame per column still gives every column a value.
    const auto z = WaveCache::build (*s, 400, 1000, 1100);
    CHECK (z.count == 400);
    CHECK (z.maxs.back() >= z.mins.back());
}

TEST_CASE ("wavecache: builds off the message thread and hands the result back")
{
    Queue q;
    WaveCache cache (q.poster());
    int ready = 0;
    cache.onReady = [&] { ++ready; };
    cache.setSource (sine (200000));

    CHECK (cache.get (800, 0, 200000) == nullptr); // not yet: it's building
    REQUIRE (q.drainUntil ([&] { return ready == 1; }));
    CHECK (cache.lastBuildThread() != std::this_thread::get_id());
    const auto* c = cache.get (800, 0, 200000);
    REQUIRE (c != nullptr);
    CHECK (c->count == 800);
    CHECK (cache.buildsStarted() == 1);
}

TEST_CASE ("wavecache: a resize cancels the older build cleanly")
{
    Queue q;
    WaveCache cache (q.poster());
    int ready = 0;
    cache.onReady = [&] { ++ready; };
    cache.setSource (sine (8000000));

    cache.get (4000, 0, 8000000); // A: a slow build...
    cache.get (1200, 0, 8000000); // B: ...replaced at once by a resize
    REQUIRE (q.drainUntil ([&] { return ready >= 1; }));
    q.drainUntil ([] { return false; }, 300); // anything else still in flight lands now

    CHECK (ready == 1);                       // only B was delivered
    CHECK (cache.buildsDelivered() == 1);
    CHECK (cache.buildsCancelled() >= 1);
    const auto* c = cache.get (1200, 0, 8000000);
    REQUIRE (c != nullptr);
    CHECK (c->count == 1200);
}

TEST_CASE ("wavecache: destroying it with a build in flight is safe")
{
    Queue q;
    {
        WaveCache cache (q.poster());
        cache.setSource (sine (4000000));
        cache.get (3000, 0, 4000000);
    }
    // The orphaned build must not deliver into a dead cache.
    q.drainUntil ([] { return false; }, 400);
    CHECK (true);
}
