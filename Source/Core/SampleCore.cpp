#include "Core/SampleCore.h"

#include <algorithm>
#include <cmath>
#include <cstdio>

namespace halo
{
    std::vector<float> AudioData::monoMixdown() const
    {
        if (channels.empty())
            return {};
        if (channels.size() == 1)
            return channels[0];
        const auto n = channels[0].size();
        std::vector<float> out (n, 0.0f);
        const float scale = 1.0f / (float) channels.size();
        for (const auto& ch : channels)
            for (size_t i = 0; i < std::min (n, ch.size()); ++i)
                out[i] += ch[i] * scale;
        return out;
    }

    //==============================================================================
    float WaveformSummary::peakMagnitude() const noexcept
    {
        float p = 0.0f;
        for (float v : maxima) p = std::max (p, v);
        for (float v : minima) p = std::max (p, -v);
        return p;
    }

    WaveformSummary WaveformSummary::build (const std::vector<float>& mono, int targetBuckets)
    {
        WaveformSummary s;
        const int n = (int) mono.size();
        s.sourceFrameCount = n;
        if (n == 0 || targetBuckets <= 0)
            return s;

        const int buckets = std::min (targetBuckets, n);
        const int per = (n + buckets - 1) / buckets; // ceil: buckets tile the whole signal
        s.framesPerBucket = per;
        for (int start = 0; start < n; start += per)
        {
            const auto first = mono.begin() + start;
            const auto last = mono.begin() + std::min (n, start + per);
            const auto [lo, hi] = std::minmax_element (first, last);
            s.minima.push_back (*lo);
            s.maxima.push_back (*hi);
        }
        return s;
    }

    //==============================================================================
    float SamplePrep::dbToLinear (float db) noexcept { return std::pow (10.0f, db / 20.0f); }

    SamplePrep::Range SamplePrep::resolvedTrim (int n) const noexcept
    {
        const int start = std::clamp (trimStartFrame, 0, std::max (0, n));
        const int end = std::clamp (trimEndFrame.value_or (n), start, std::max (0, n));
        return { start, end };
    }

    int SamplePrep::outputFrameCount (int n) const noexcept
    {
        const auto r = resolvedTrim (n);
        return r.end - r.start;
    }

    int SamplePrep::outputChannelCount (int c) const noexcept
    {
        switch (channelMode)
        {
            case Channels::preserve: return std::max (0, c);
            case Channels::mono:     return c > 0 ? 1 : 0;
            case Channels::stereo:   return c > 0 ? 2 : 0;
        }
        return c;
    }

    AudioData SamplePrep::apply (const AudioData& source) const
    {
        AudioData out;
        out.sampleRate = source.sampleRate;
        const auto r = resolvedTrim (source.frameCount());
        const int count = r.end - r.start;
        if (count <= 0 || source.channels.empty())
        {
            out.channels.assign ((size_t) outputChannelCount (source.channelCount()), {});
            return out;
        }

        // 1. Trim.
        std::vector<std::vector<float>> ch;
        for (const auto& c : source.channels)
            ch.emplace_back (c.begin() + r.start, c.begin() + r.end);

        // 2. Channel conversion. Mono is an equal-power sum; mono -> stereo copies.
        if (channelMode == Channels::mono && ch.size() > 1)
        {
            std::vector<float> sum ((size_t) count, 0.0f);
            for (const auto& c : ch)
                for (int i = 0; i < count; ++i)
                    sum[(size_t) i] += c[(size_t) i];
            const float scale = 1.0f / std::sqrt ((float) ch.size());
            for (auto& v : sum)
                v *= scale;
            ch = { std::move (sum) };
        }
        else if (channelMode == Channels::stereo)
        {
            if (ch.size() >= 2)
                ch.resize (2);
            else
                ch.push_back (ch[0]);
        }

        // 3. Gain.
        if (gainDecibels != 0.0f)
        {
            const float g = dbToLinear (gainDecibels);
            for (auto& c : ch)
                for (auto& v : c)
                    v *= g;
        }

        // 4. Equal-power fades: sin / cos quarter-waves, g = 0 -> 1 and 1 -> 0.
        const int fin = std::clamp (fadeInFrames, 0, count);
        const int fout = std::clamp (fadeOutFrames, 0, count);
        constexpr float halfPi = 1.5707963267948966f;
        for (auto& c : ch)
        {
            if (fin > 1)
                for (int i = 0; i < fin; ++i)
                    c[(size_t) i] *= std::sin (halfPi * (float) i / (float) (fin - 1));
            if (fout > 1)
                for (int j = 0; j < fout; ++j)
                    c[(size_t) (count - fout + j)] *= std::cos (halfPi * (float) j / (float) (fout - 1));
        }

        // 5. Peak normalise (opt-in).
        if (normalize)
        {
            float peak = 0.0f;
            for (const auto& c : ch)
                for (float v : c)
                    peak = std::max (peak, std::abs (v));
            if (peak > 0.0f)
            {
                const float scale = dbToLinear (normalizeTargetDecibels) / peak;
                for (auto& c : ch)
                    for (auto& v : c)
                        v *= scale;
            }
        }

        out.channels = std::move (ch);
        return out;
    }

    //==============================================================================
    const char* label (Treatment t)
    {
        switch (t)
        {
            case Treatment::original: return "ORIGINAL";
            case Treatment::high:     return "HIGH";
            case Treatment::balanced: return "BALANCED";
            case Treatment::loFi22k:  return "LO-FI 22K";
            case Treatment::loFi11k:  return "LO-FI 11K";
        }
        return "";
    }

    double resolvedSampleRate (Treatment t, double source)
    {
        switch (t)
        {
            case Treatment::original: return source > 0.0 ? std::min (source, deviceMaxSampleRate) : deviceMaxSampleRate;
            case Treatment::high:     return deviceMaxSampleRate;
            case Treatment::balanced: return 32000.0;
            case Treatment::loFi22k:  return 22050.0;
            case Treatment::loFi11k:  return 11025.0;
        }
        return deviceMaxSampleRate;
    }

    int outputFrameCount (Treatment t, int frames, double source)
    {
        if (frames <= 0 || source <= 0.0)
            return 0;
        const double target = resolvedSampleRate (t, source);
        return target == source ? frames : (int) std::llround ((double) frames * target / source);
    }

    SizeEstimate estimateSize (int sourceFrames, int sourceChannels, double sourceRate,
                               const SamplePrep& prep, Treatment t)
    {
        SizeEstimate e;
        e.channels = prep.outputChannelCount (sourceChannels);
        e.sampleRate = resolvedSampleRate (t, sourceRate);
        e.frames = outputFrameCount (t, prep.outputFrameCount (sourceFrames), sourceRate);
        e.payloadBytes = (long long) e.frames * e.channels * (deviceBitDepth / 8);
        return e;
    }

    std::string formatBytes (long long bytes)
    {
        char text[32];
        if (bytes >= 1024 * 1024)
            std::snprintf (text, sizeof (text), "%.2f MB", (double) bytes / (1024.0 * 1024.0));
        else if (bytes >= 1024)
            std::snprintf (text, sizeof (text), "%.1f KB", (double) bytes / 1024.0);
        else
            std::snprintf (text, sizeof (text), "%lld B", bytes);
        return text;
    }
}
