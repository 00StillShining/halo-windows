#include "Stage/StageStats.h"

#include <algorithm>

using namespace juce::gl;

namespace halo
{
    void StageStats::create()
    {
        const auto* version = (const char*) glGetString (GL_VERSION);
        const auto parts = juce::StringArray::fromTokens (juce::String (version != nullptr ? version : "0.0"), ". ", {});
        const int major = parts[0].getIntValue(), minor = parts[1].getIntValue();
        const bool core33 = major > 3 || (major == 3 && minor >= 3);
        timerAvailable = (core33 || juce::OpenGLHelpers::isExtensionSupported ("GL_ARB_timer_query"))
                      && glGenQueries != nullptr && glBeginQuery != nullptr && glEndQuery != nullptr
                      && glGetQueryObjectiv != nullptr && glGetQueryObjectui64v != nullptr;
        if (timerAvailable)
            glGenQueries (queryCount, queries.data());
        pending.fill (false);
    }

    void StageStats::release()
    {
        if (timerAvailable && queries[0] != 0)
            glDeleteQueries (queryCount, queries.data());
        queries.fill (0);
        timerAvailable = false;
    }

    void StageStats::beginFrame()
    {
        collect();
        uniformLookups = 0;
        geometryDraws = 0;
        if (timerAvailable && ! pending[(size_t) next])
        {
            glBeginQuery (GL_TIME_ELAPSED, queries[(size_t) next]);
            inFrame = true;
        }
    }

    void StageStats::endFrame (const char* reason)
    {
        if (inFrame)
        {
            glEndQuery (GL_TIME_ELAPSED);
            pending[(size_t) next] = true;
            next = (next + 1) % queryCount;
            inFrame = false;
        }
        ++frames;
        ++reasons[reason];
        lookupsInWindow += uniformLookups;
        drawsInWindow += geometryDraws;
        maxDrawsPerFrame = std::max (maxDrawsPerFrame, geometryDraws);
    }

    void StageStats::collect()
    {
        if (! timerAvailable)
            return;
        for (int i = 0; i < queryCount; ++i)
        {
            if (! pending[(size_t) i])
                continue;
            GLint ready = 0;
            glGetQueryObjectiv (queries[(size_t) i], GL_QUERY_RESULT_AVAILABLE, &ready);
            if (ready == 0)
                continue;
            GLuint64 ns = 0;
            glGetQueryObjectui64v (queries[(size_t) i], GL_QUERY_RESULT, &ns);
            gpuMs.push_back ((double) ns * 1.0e-6);
            pending[(size_t) i] = false;
        }
    }

    juce::String StageStats::poll (double now, const juce::String& tier, double period)
    {
        if (windowStart < 0.0)
            windowStart = now;
        const double elapsed = now - windowStart;
        if (elapsed < period)
            return {};

        auto percentile = [this] (double p)
        {
            if (gpuMs.empty())
                return juce::String ("n/a");
            auto sorted = gpuMs;
            std::sort (sorted.begin(), sorted.end());
            const auto index = (size_t) std::min ((double) sorted.size() - 1.0, std::floor (p * (double) (sorted.size() - 1) + 0.5));
            return juce::String (sorted[index], 3) + "ms";
        };

        juce::String why;
        for (const auto& [name, count] : reasons)
            why << (why.isEmpty() ? "" : " ") << name << ":" << count;

        juce::String line;
        line << "stats fps=" << juce::String ((double) frames / elapsed, 1)
             << " frames=" << frames
             << " gpu_p50=" << percentile (0.50) << " gpu_p95=" << percentile (0.95)
             << " gpu_samples=" << (int) gpuMs.size()
             << " tier=" << tier
             << " uniform_lookups_per_frame=" << (tracksLookups ? juce::String (frames > 0 ? (double) lookupsInWindow / frames : 0.0, 1) : juce::String ("untracked"))
             << " draws_per_frame_max=" << maxDrawsPerFrame
             << " reasons=" << (why.isEmpty() ? juce::String ("none") : why);

        windowStart = now;
        frames = lookupsInWindow = drawsInWindow = maxDrawsPerFrame = 0;
        gpuMs.clear();
        reasons.clear();
        return line;
    }
}
