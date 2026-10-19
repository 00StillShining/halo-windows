#include "Core/Chop.h"
#include "Core/Ep40MidiMapping.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <complex>
#include <cstdio>
#include <numeric>

namespace halo::chop
{
    namespace
    {
        // In-place iterative radix-2 FFT (n must be a power of two).
        void fft (std::vector<std::complex<float>>& a)
        {
            const size_t n = a.size();
            for (size_t i = 1, j = 0; i < n; ++i)
            {
                size_t bit = n >> 1;
                for (; j & bit; bit >>= 1)
                    j ^= bit;
                j ^= bit;
                if (i < j)
                    std::swap (a[i], a[j]);
            }
            for (size_t len = 2; len <= n; len <<= 1)
            {
                const float angle = -2.0f * 3.14159265358979f / (float) len;
                const std::complex<float> step (std::cos (angle), std::sin (angle));
                for (size_t i = 0; i < n; i += len)
                {
                    std::complex<float> w (1.0f, 0.0f);
                    for (size_t k = 0; k < len / 2; ++k)
                    {
                        const auto u = a[i + k], v = a[i + k + len / 2] * w;
                        a[i + k] = u + v;
                        a[i + k + len / 2] = u - v;
                        w *= step;
                    }
                }
            }
        }

        struct Flux
        {
            std::vector<float> log;    // for finding onsets
            std::vector<float> linear; // for ranking them by loudness
        };

        // Half-wave-rectified spectral flux per hop, Hann-windowed, two ways.
        //  - Log-compressed magnitude (log(1 + 100|X|)) finds onsets. The Mac used
        //    linear magnitude, whose flux flickers on noisy tails: on a 16-hit drum
        //    loop it found 20 cuts at every sensitivity; this finds the 16 hits.
        //  - Linear magnitude ranks them, because log compression rates a quiet hit
        //    after silence almost as highly as a loud one.
        Flux spectralFlux (const std::vector<float>& mono, int windowSize, int hopSize)
        {
            const int half = windowSize / 2;
            std::vector<float> window ((size_t) windowSize);
            for (int i = 0; i < windowSize; ++i)
                window[(size_t) i] = 0.5f - 0.5f * std::cos (2.0f * 3.14159265358979f * (float) i / (float) windowSize);

            std::vector<std::complex<float>> frame ((size_t) windowSize);
            std::vector<float> lin ((size_t) half), prevLin ((size_t) half);
            Flux flux;
            bool first = true;

            for (size_t start = 0; start + (size_t) windowSize <= mono.size(); start += (size_t) hopSize)
            {
                for (int i = 0; i < windowSize; ++i)
                    frame[(size_t) i] = { mono[start + (size_t) i] * window[(size_t) i], 0.0f };
                fft (frame);
                for (int k = 0; k < half; ++k)
                    lin[(size_t) k] = std::abs (frame[(size_t) k]);

                // The first frame has no predecessor: zero it so there's no phantom onset.
                float sLog = 0.0f, sLin = 0.0f;
                if (! first)
                    for (int k = 0; k < half; ++k)
                    {
                        sLog += std::max (0.0f, std::log1p (100.0f * lin[(size_t) k]) - std::log1p (100.0f * prevLin[(size_t) k]));
                        sLin += std::max (0.0f, lin[(size_t) k] - prevLin[(size_t) k]);
                    }
                flux.log.push_back (sLog);
                flux.linear.push_back (sLin);
                first = false;
                std::swap (prevLin, lin);
            }
            return flux;
        }
    }

    std::vector<int> detectOnsets (const std::vector<float>& mono, double sampleRate, const OnsetParameters& p)
    {
        std::vector<int> frames;
        for (const auto& o : detectOnsetsWithStrength (mono, sampleRate, p))
            frames.push_back (o.frame);
        return frames;
    }

    std::vector<int> strongestCuts (const std::vector<Onset>& onsets, int maxCuts, int minGap)
    {
        auto byStrength = onsets;
        std::stable_sort (byStrength.begin(), byStrength.end(), [] (const Onset& a, const Onset& b) { return a.strength > b.strength; });

        std::vector<int> chosen;
        for (const auto& o : byStrength)
        {
            if ((int) chosen.size() >= maxCuts)
                break;
            const bool clear = std::none_of (chosen.begin(), chosen.end(), [&] (int f) { return std::abs (f - o.frame) < minGap; });
            if (clear)
                chosen.push_back (o.frame);
        }
        std::sort (chosen.begin(), chosen.end());
        return chosen;
    }

    std::vector<int> evenCuts (int sourceFrames, int slices)
    {
        std::vector<int> cuts;
        for (int i = 1; i < slices && sourceFrames > 0; ++i)
            cuts.push_back ((int) std::llround ((double) sourceFrames * i / slices));
        return cuts;
    }

    std::vector<Onset> detectOnsetsWithStrength (const std::vector<float>& mono, double sampleRate, const OnsetParameters& p)
    {
        const int n = (int) mono.size();
        const bool powerOfTwo = p.windowSize > 0 && (p.windowSize & (p.windowSize - 1)) == 0;
        if (n < p.windowSize || sampleRate <= 0.0 || ! powerOfTwo || p.hopSize <= 0)
            return {};

        const auto both = spectralFlux (mono, p.windowSize, p.hopSize);
        const auto& flux = both.log;
        if (flux.size() <= 2)
            return {};

        // Adaptive threshold: local mean (+/-6 hops) x multiplier.
        const float mult = (float) (2.2 - 1.4 * std::clamp (p.sensitivity, 0.0, 1.0));
        const int minGap = std::max (1, (int) (p.minSliceMilliseconds / 1000.0 * sampleRate));
        constexpr int W = 6;

        std::vector<Onset> onsets;
        int last = -minGap;
        for (int m = 1; m < (int) flux.size() - 1; ++m)
        {
            const int lo = std::max (0, m - W), hi = std::min ((int) flux.size() - 1, m + W);
            const float mean = std::accumulate (flux.begin() + lo, flux.begin() + hi + 1, 0.0f) / (float) (hi - lo + 1);
            const bool localMax = flux[(size_t) m] > flux[(size_t) m - 1] && flux[(size_t) m] >= flux[(size_t) m + 1];
            const int frame = m * p.hopSize;
            // Cuts closer than minGap to either end would make a sliver slice.
            if (flux[(size_t) m] > mean * mult + 1.0e-6f && localMax && frame >= minGap && frame <= n - minGap)
            {
                // Rank by loudness: the strongest linear flux right around the onset.
                const float strength = std::max ({ both.linear[(size_t) m - 1], both.linear[(size_t) m], both.linear[(size_t) m + 1] });
                if (frame - last >= minGap)
                {
                    onsets.push_back ({ frame, strength });
                    last = frame;
                }
                else if (! onsets.empty() && strength > onsets.back().strength)
                {
                    // Two candidates inside the gap: keep the stronger, not just the
                    // earlier (a quiet pre-hit used to mask the real hit after it).
                    onsets.back() = { frame, strength };
                    last = frame;
                }
            }
        }
        return onsets;
    }

    //==============================================================================
    SliceSet::SliceSet (int sourceFrames, const std::vector<int>& onsets) : frames (std::max (0, sourceFrames))
    {
        for (int f : onsets)
            if (f > 0 && f < frames)
                cutPoints.push_back (f);
        std::sort (cutPoints.begin(), cutPoints.end());
        cutPoints.erase (std::unique (cutPoints.begin(), cutPoints.end()), cutPoints.end());
    }

    std::vector<Slice> SliceSet::slices() const
    {
        std::vector<Slice> out;
        if (frames <= 0)
            return out;
        int start = 0;
        for (size_t i = 0; i <= cutPoints.size(); ++i)
        {
            const int end = i < cutPoints.size() ? cutPoints[i] : frames;
            out.push_back ({ (int) i, start, end });
            start = end;
        }
        return out;
    }

    int SliceSet::sliceAt (int frame) const noexcept
    {
        return (int) (std::upper_bound (cutPoints.begin(), cutPoints.end(), frame) - cutPoints.begin());
    }

    bool SliceSet::addCut (int frame, int minGap)
    {
        const int g = std::max (1, minGap);
        const int f = std::clamp (frame, 0, frames);
        if (f < g || frames - f < g)
            return false;
        for (int c : cutPoints)
            if (std::abs (c - f) < g)
                return false;
        cutPoints.insert (std::upper_bound (cutPoints.begin(), cutPoints.end(), f), f);
        return true;
    }

    void SliceSet::removeCut (int index)
    {
        if (index >= 0 && index < (int) cutPoints.size())
            cutPoints.erase (cutPoints.begin() + index);
    }

    void SliceSet::moveCut (int index, int frame, int minGap)
    {
        if (index < 0 || index >= (int) cutPoints.size())
            return;
        const int g = std::max (1, minGap);
        const int lower = (index == 0 ? 0 : cutPoints[(size_t) index - 1]) + g;
        const int upper = (index == (int) cutPoints.size() - 1 ? frames : cutPoints[(size_t) index + 1]) - g;
        if (lower <= upper)
            cutPoints[(size_t) index] = std::clamp (frame, lower, upper);
    }

    //==============================================================================
    double capToFit (const std::vector<double>& lengths, double maxSeconds, double budgetSeconds)
    {
        auto total = [&lengths] (double cap)
        {
            double sum = 0.0;
            for (double l : lengths)
                sum += std::min (l, cap);
            return sum;
        };
        if (total (maxSeconds) <= budgetSeconds)
            return maxSeconds;
        // The total grows with the cap: binary-search the largest cap that fits.
        double lo = 0.0, hi = maxSeconds;
        for (int i = 0; i < 60; ++i)
        {
            const double mid = 0.5 * (lo + hi);
            (total (mid) <= budgetSeconds ? lo : hi) = mid;
        }
        return lo;
    }

    PadPlan planPads (const std::vector<Slice>& slices, int channelCount, Treatment treatment, double sourceRate,
                      int group, int startGridIndex, const std::string& baseName, int maxSliceFrames)
    {
        PadPlan plan;
        plan.group = std::clamp (group, 0, ep40::groupCount - 1);
        plan.startGridIndex = std::clamp (startGridIndex, 0, ep40::padsPerGroup);
        const int capacity = ep40::padsPerGroup - plan.startGridIndex;
        const int placed = std::min ((int) slices.size(), capacity);
        plan.overflowCount = (int) slices.size() - placed;

        std::string base;
        for (char ch : baseName)
            if (! std::isspace ((unsigned char) ch))
                base += (char) std::toupper ((unsigned char) ch);
        if (base.empty())
            base = "CHOP";

        for (int i = 0; i < placed; ++i)
        {
            const auto& s = slices[(size_t) i];
            int sliceFrames = s.frameCount();
            if (maxSliceFrames > 0 && sliceFrames > maxSliceFrames)
            {
                sliceFrames = maxSliceFrames;
                ++plan.trimmedCount;
            }
            const int frames = outputFrameCount (treatment, sliceFrames, sourceRate);
            char name[64];
            std::snprintf (name, sizeof (name), "%s.%02d", base.c_str(), i + 1);
            PadWrite w { s.index, plan.group, plan.startGridIndex + i, name,
                         (long long) frames * std::max (0, channelCount) * (deviceBitDepth / 8) };
            plan.totalBytes += w.payloadBytes;
            const double seconds = sourceRate > 0.0 ? sliceFrames / sourceRate : 0.0;
            plan.longestSeconds = std::max (plan.longestSeconds, seconds);
            if (seconds > maxSoundSeconds)
                ++plan.tooLongCount;
            plan.writes.push_back (std::move (w));
        }
        return plan;
    }

    SamplePrep microFadePrep (double sampleRate)
    {
        SamplePrep p;
        p.fadeInFrames = p.fadeOutFrames = std::max (1, (int) (0.004 * std::max (0.0, sampleRate)));
        return p;
    }
}
