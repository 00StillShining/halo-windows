#include "Core/DubRack.h"

#include <algorithm>

namespace halo
{
    namespace
    {
        constexpr float pi = 3.14159265358979f;

        float onePoleCoef (float seconds, float sampleRate) { return std::exp (-1.0f / (std::max (1.0e-4f, seconds) * sampleRate)); }
        float smooth (float current, float target, float coef) noexcept { return target + (current - target) * coef; }
        float clamp01 (float v) noexcept { return std::clamp (v, 0.0f, 1.0f); }
        // Bounded soft saturation for feedback loops (keeps self-oscillation finite).
        float softClip (float x) noexcept { return std::tanh (x); }
    }

    RackTargets RackTargets::load (const RackParameters& p) noexcept
    {
        constexpr auto o = std::memory_order_relaxed;
        return { p.engaged.load (o),
                 p.echoOn.load (o), p.echoTimeMs.load (o), p.echoFeedback.load (o), p.echoTone.load (o), p.echoWow.load (o), p.echoMix.load (o),
                 p.springOn.load (o), p.springMix.load (o), p.springDecay.load (o), p.springTone.load (o),
                 p.sweepOn.load (o), p.sweepMacro.load (o), p.sweepReso.load (o),
                 p.lowOn.load (o), p.lowAmount.load (o) };
    }

    //==============================================================================
    void DubRack::prepare (double sampleRate, int maxBlock)
    {
        const auto sr = (float) std::max (1.0, sampleRate);
        maxFrames = std::max (1, maxBlock);
        echo.prepare (sr);
        spring.prepare (sr);
        sweep.prepare (sr);
        lowEnd.prepare (sr);
        wetL.assign ((size_t) maxFrames, 0.0f);
        wetR.assign ((size_t) maxFrames, 0.0f);
        masterCoef = onePoleCoef (0.020f, sr); // ~20 ms engage fade
        masterMix = 0.0f;
    }

    void DubRack::reset() noexcept
    {
        masterMix = 0.0f;
        echo.reset();
        spring.reset();
        sweep.reset();
        lowEnd.reset();
    }

    void DubRack::process (float* left, float* right, int frames, const RackParameters& params) noexcept
    {
        const auto t = RackTargets::load (params);

        // NULL TEST: disengaged and settled -> leave the audio exactly as it came.
        if (! t.engaged && masterMix == 0.0f)
            return;

        for (int offset = 0; offset < frames; offset += maxFrames)
            processChunk (left + offset, right + offset, std::min (maxFrames, frames - offset), t);
    }

    void DubRack::processChunk (float* left, float* right, int n, const RackTargets& t) noexcept
    {
        std::copy (left, left + n, wetL.data());
        std::copy (right, right + n, wetR.data());
        echo.process (wetL.data(), wetR.data(), n, t);
        spring.process (wetL.data(), wetR.data(), n, t);
        sweep.process (wetL.data(), wetR.data(), n, t);
        lowEnd.process (wetL.data(), wetR.data(), n, t);

        // Master dry/wet crossfade; at m == 0 this is exactly dry.
        const float target = t.engaged ? 1.0f : 0.0f;
        float m = masterMix;
        for (int i = 0; i < n; ++i)
        {
            m = smooth (m, target, masterCoef);
            left[i] += (wetL[(size_t) i] - left[i]) * m;
            right[i] += (wetR[(size_t) i] - right[i]) * m;
        }
        masterMix = std::abs (m - target) < 1.0e-5f ? target : m;

        // Just settled into bypass: clear tails so the next engage starts clean.
        if (masterMix == 0.0f)
        {
            echo.reset();
            spring.reset();
            sweep.reset();
            lowEnd.reset();
        }
    }

    //==============================================================================
    void DubRack::Echo::prepare (float sampleRate)
    {
        sr = sampleRate;
        maxMod = 0.004f * sr; // +/-4 ms wow/flutter
        size = (int) (1.2f * sr + maxMod) + 4;
        bufL.assign ((size_t) size, 0.0f);
        bufR.assign ((size_t) size, 0.0f);
        timeSamples = 0.360f * sr;
        wowInc = 2.0f * pi * 0.7f / sr;
        flutInc = 2.0f * pi * 6.3f / sr;
        ctrlCoef = onePoleCoef (0.015f, sr);
        timeCoef = onePoleCoef (0.080f, sr); // tape-like pitch slew on time changes
        reset();
    }

    void DubRack::Echo::reset() noexcept
    {
        std::fill (bufL.begin(), bufL.end(), 0.0f);
        std::fill (bufR.begin(), bufR.end(), 0.0f);
        writeIdx = 0;
        dampL = dampR = wowPhase = flutPhase = 0.0f;
        enable = 0.0f;
    }

    float DubRack::Echo::tap (const std::vector<float>& buf, float delay) const noexcept
    {
        const float d = std::clamp (delay, 1.0f, (float) (size - 2));
        const float readPos = (float) writeIdx - d;
        int i0 = (int) std::floor (readPos);
        const float frac = readPos - (float) i0;
        i0 = ((i0 % size) + size) % size;
        const int i1 = (i0 + 1) % size;
        return buf[(size_t) i0] + (buf[(size_t) i1] - buf[(size_t) i0]) * frac;
    }

    void DubRack::Echo::process (float* l, float* r, int n, const RackTargets& t) noexcept
    {
        const float enTarget = t.echoOn ? 1.0f : 0.0f;
        const float timeTarget = std::clamp (t.echoTimeMs, 40.0f, 1200.0f) / 1000.0f * sr;
        const float fbTarget = clamp01 (t.echoFeedback) * 1.05f; // the top of the knob self-oscillates
        const float toneTarget = clamp01 (t.echoTone), wowTarget = clamp01 (t.echoWow), mixTarget = clamp01 (t.echoMix);

        for (int f = 0; f < n; ++f)
        {
            enable = smooth (enable, enTarget, ctrlCoef);
            timeSamples = smooth (timeSamples, timeTarget, timeCoef);
            feedback = smooth (feedback, fbTarget, ctrlCoef);
            tone = smooth (tone, toneTarget, ctrlCoef);
            wow = smooth (wow, wowTarget, ctrlCoef);
            mix = smooth (mix, mixTarget, ctrlCoef);

            wowPhase += wowInc;
            if (wowPhase > 2.0f * pi) wowPhase -= 2.0f * pi;
            flutPhase += flutInc;
            if (flutPhase > 2.0f * pi) flutPhase -= 2.0f * pi;
            const float delay = timeSamples + (std::sin (wowPhase) * 0.7f + std::sin (flutPhase) * 0.3f) * wow * maxMod;

            const float dl = tap (bufL, delay), dr = tap (bufR, delay);
            const float lp = 0.05f + 0.94f * tone; // loop damping: 0 dark ... 1 open
            dampL += lp * (dl - dampL);
            dampR += lp * (dr - dampR);

            const float inL = l[f], inR = r[f];
            bufL[(size_t) writeIdx] = inL + softClip (feedback * dampL);
            bufR[(size_t) writeIdx] = inR + softClip (feedback * dampR);
            if (++writeIdx >= size)
                writeIdx = 0;

            const float wet = enable * mix;
            l[f] = inL + (dl - inL) * wet;
            r[f] = inR + (dr - inR) * wet;
        }
    }

    //==============================================================================
    void DubRack::Spring::prepare (float sampleRate)
    {
        // Short, coprime-ish lengths: a dense spring "drip" without a room tail.
        constexpr float seconds[] { 0.0197f, 0.0259f, 0.0331f, 0.0413f };
        for (size_t k = 0; k < 4; ++k)
            lines[k].assign ((size_t) std::max (4, (int) (seconds[k] * sampleRate)), 0.0f);
        ctrlCoef = onePoleCoef (0.015f, sampleRate);
        reset();
    }

    void DubRack::Spring::reset() noexcept
    {
        for (auto& line : lines)
            std::fill (line.begin(), line.end(), 0.0f);
        idx = {};
        damp = {};
        enable = 0.0f;
    }

    void DubRack::Spring::process (float* l, float* r, int n, const RackTargets& t) noexcept
    {
        const float enTarget = t.springOn ? 1.0f : 0.0f;
        const float mixTarget = clamp01 (t.springMix), decayTarget = clamp01 (t.springDecay), toneTarget = clamp01 (t.springTone);

        for (int f = 0; f < n; ++f)
        {
            enable = smooth (enable, enTarget, ctrlCoef);
            mix = smooth (mix, mixTarget, ctrlCoef);
            decay = smooth (decay, decayTarget, ctrlCoef);
            tone = smooth (tone, toneTarget, ctrlCoef);

            const float g = 0.5f + 0.36f * decay; // <= 0.86: the tail always decays
            const float lp = 0.15f + 0.8f * tone;
            const float inL = l[f], inR = r[f];
            const float input = (inL + inR) * 0.5f;

            float y[4];
            for (size_t k = 0; k < 4; ++k)
            {
                y[k] = lines[k][(size_t) idx[k]];
                damp[k] += lp * (y[k] - damp[k]);
            }

            // Normalised 4x4 Hadamard mix of the damped outputs.
            const float m0 = 0.5f * (damp[0] + damp[1] + damp[2] + damp[3]);
            const float m1 = 0.5f * (damp[0] - damp[1] + damp[2] - damp[3]);
            const float m2 = 0.5f * (damp[0] + damp[1] - damp[2] - damp[3]);
            const float m3 = 0.5f * (damp[0] - damp[1] - damp[2] + damp[3]);
            const float mixed[] { m0, m1, m2, m3 };

            for (size_t k = 0; k < 4; ++k)
            {
                lines[k][(size_t) idx[k]] = input + g * mixed[k];
                if (++idx[k] >= (int) lines[k].size())
                    idx[k] = 0;
            }

            const float springL = (y[0] + y[2]) * 0.5f, springR = (y[1] + y[3]) * 0.5f; // decorrelated pickup
            const float wet = enable * mix;
            l[f] = inL + (springL - inL) * wet;
            r[f] = inR + (springR - inR) * wet;
        }
    }

    //==============================================================================
    void DubRack::Sweep::prepare (float sampleRate)
    {
        sr = sampleRate;
        ctrlCoef = onePoleCoef (0.020f, sr);
        reset();
    }

    void DubRack::Sweep::reset() noexcept
    {
        ic1L = ic2L = ic1R = ic2R = 0.0f;
        enable = 0.0f;
    }

    void DubRack::Sweep::process (float* l, float* r, int n, const RackTargets& t) noexcept
    {
        // Block-rate smoothing, scaled to the block length (the Mac's bug).
        const float blockCoef = std::pow (ctrlCoef, (float) std::max (1, n));
        enable = smooth (enable, t.sweepOn ? 1.0f : 0.0f, blockCoef);
        macro = smooth (macro, clamp01 (t.sweepMacro), blockCoef);
        reso = smooth (reso, clamp01 (t.sweepReso), blockCoef);

        // Log sweep 60 Hz -> 12 kHz (under Nyquist), resonance -> Q 0.5...8, TPT SVF.
        const float fc = std::min (60.0f * std::pow (200.0f, macro), sr * 0.45f);
        const float g = std::tan (pi * fc / sr);
        const float k = 1.0f / (0.5f + reso * 7.5f);
        const float a1 = 1.0f / (1.0f + g * (g + k)), a2 = g * a1, a3 = g * a2;

        // LP -> BP -> HP morph (BP peaks at macro 0.5).
        const float wHP = std::max (0.0f, 2.0f * macro - 1.0f);
        const float wLP = std::max (0.0f, 1.0f - 2.0f * macro);
        const float wBP = 1.0f - std::abs (2.0f * macro - 1.0f);

        auto run = [&] (float* x, float& ic1, float& ic2)
        {
            for (int f = 0; f < n; ++f)
            {
                const float v0 = x[f];
                const float v3 = v0 - ic2;
                const float v1 = a1 * ic1 + a2 * v3;
                const float v2 = ic2 + a2 * ic1 + a3 * v3;
                ic1 = 2.0f * v1 - ic1;
                ic2 = 2.0f * v2 - ic2;
                const float filtered = wLP * v2 + wBP * v1 + wHP * (v0 - k * v1 - v2);
                x[f] = v0 + (filtered - v0) * enable;
            }
        };
        run (l, ic1L, ic2L);
        run (r, ic1R, ic2R);
    }

    //==============================================================================
    void DubRack::LowEnd::prepare (float sampleRate)
    {
        sr = sampleRate;
        ctrlCoef = onePoleCoef (0.020f, sr);
        reset();
    }

    void DubRack::LowEnd::reset() noexcept
    {
        x1L = x2L = y1L = y2L = x1R = x2R = y1R = y2R = 0.0f;
        enable = 0.0f;
    }

    void DubRack::LowEnd::process (float* l, float* r, int n, const RackTargets& t) noexcept
    {
        const float blockCoef = std::pow (ctrlCoef, (float) std::max (1, n));
        enable = smooth (enable, t.lowOn ? 1.0f : 0.0f, blockCoef);
        amount = smooth (amount, clamp01 (t.lowAmount), blockCoef);

        // RBJ low shelf at 120 Hz, 0...+6 dB, slope 0.7: bounded, never runaway.
        const float a = std::pow (10.0f, amount * 6.0f / 40.0f);
        const float w0 = 2.0f * pi * 120.0f / sr;
        const float cosw = std::cos (w0), sinw = std::sin (w0);
        const float alpha = sinw / 2.0f * std::sqrt ((a + 1.0f / a) * (1.0f / 0.7f - 1.0f) + 2.0f);
        const float twoSqrtAAlpha = 2.0f * std::sqrt (a) * alpha;

        const float a0 = (a + 1) + (a - 1) * cosw + twoSqrtAAlpha;
        const float b0 = a * ((a + 1) - (a - 1) * cosw + twoSqrtAAlpha) / a0;
        const float b1 = 2 * a * ((a - 1) - (a + 1) * cosw) / a0;
        const float b2 = a * ((a + 1) - (a - 1) * cosw - twoSqrtAAlpha) / a0;
        const float c1 = -2 * ((a - 1) + (a + 1) * cosw) / a0;
        const float c2 = ((a + 1) + (a - 1) * cosw - twoSqrtAAlpha) / a0;

        auto run = [&] (float* x, float& x1, float& x2, float& y1, float& y2)
        {
            for (int f = 0; f < n; ++f)
            {
                const float in = x[f];
                const float out = b0 * in + b1 * x1 + b2 * x2 - c1 * y1 - c2 * y2;
                x2 = x1; x1 = in; y2 = y1; y1 = out;
                x[f] = in + (out - in) * enable;
            }
        };
        run (l, x1L, x2L, y1L, y2L);
        run (r, x1R, x2R, y1R, y2R);
    }

    //==============================================================================
    double beatFraction (EchoDivision d)
    {
        switch (d)
        {
            case EchoDivision::eighth:        return 0.5;
            case EchoDivision::dottedEighth:  return 0.75;
            case EchoDivision::quarter:       return 1.0;
            case EchoDivision::tripletEighth: return 1.0 / 3.0;
        }
        return 0.5;
    }

    const char* label (EchoDivision d)
    {
        switch (d)
        {
            case EchoDivision::eighth:        return "1/8";
            case EchoDivision::dottedEighth:  return "1/8.";
            case EchoDivision::quarter:       return "1/4";
            case EchoDivision::tripletEighth: return "1/8T";
        }
        return "";
    }

    double effectiveEchoMs (bool sync, EchoDivision division, std::optional<double> bpm, double freeMs)
    {
        if (sync && bpm && *bpm > 0.0)
            return std::clamp (60000.0 / *bpm * beatFraction (division), 40.0, 1200.0);
        return std::clamp (freeMs, 40.0, 1200.0);
    }

    std::optional<double> TapTempo::tap (double now)
    {
        if (count > 0 && now - times[(size_t) count - 1] > 2.0)
            count = 0;
        if (count == (int) times.size())
        {
            std::rotate (times.begin(), times.begin() + 1, times.end());
            --count;
        }
        times[(size_t) count++] = now;
        if (count < 2)
            return current;

        const double mean = (times[(size_t) count - 1] - times[0]) / (count - 1);
        if (mean > 0.0)
        {
            const double bpm = std::round (60.0 / mean);
            if (bpm >= 30.0 && bpm <= 300.0)
                current = bpm;
        }
        return current;
    }
}
