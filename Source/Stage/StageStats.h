#pragma once

#include <juce_opengl/juce_opengl.h>

#include <array>
#include <map>
#include <vector>

namespace halo
{
    // HALO_STAGE_STATS=1: fps, GPU time per frame (GL_TIME_ELAPSED p50/p95),
    // the tier and why frames were drawn, summarised every two seconds.
    // GL thread only. Timer queries are optional (GL 3.3 or ARB_timer_query);
    // without them the GPU columns read "n/a".
    class StageStats
    {
    public:
        void create();  // after the context exists
        void release(); // before it goes

        void beginFrame();
        void endFrame (const char* reason);

        // Counters that the renderer bumps itself (e.g. uniform lookups, draws).
        int uniformLookups = 0;
        int geometryDraws = 0;
        int depthRenders = 0;   // shadow + height map re-renders (cached while nothing moves)
        bool tracksLookups = false; // set once uniform lookups are counted (GlProgram)

        // A summary line every `periodSeconds`, else an empty string.
        juce::String poll (double nowSeconds, const juce::String& tier, double periodSeconds = 2.0);

        bool hasTimer() const noexcept { return timerAvailable; }

    private:
        void collect();

        static constexpr int queryCount = 6;
        std::array<GLuint, queryCount> queries {};
        std::array<bool, queryCount> pending {};
        int next = 0;
        bool timerAvailable = false, inFrame = false;

        std::vector<double> gpuMs;
        std::map<juce::String, int> reasons;
        int frames = 0, lookupsInWindow = 0, drawsInWindow = 0, maxDrawsPerFrame = 0, depthInWindow = 0;
        double windowStart = -1.0;
    };
}
