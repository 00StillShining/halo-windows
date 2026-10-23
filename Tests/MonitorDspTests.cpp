// Ported from SafetyLimiterTests / MonitorGainPersistenceTests / AudioMeterTests,
// plus a test for the per-block meter ballistics fix.
#include <doctest/doctest.h>

#include "Core/MonitorDsp.h"

#include <cstring>
#include <vector>

using namespace halo;

namespace
{
    std::vector<float> sine (int frames, float amplitude, double hz = 440.0, double sr = 48000.0)
    {
        std::vector<float> v ((size_t) frames);
        for (int i = 0; i < frames; ++i)
            v[(size_t) i] = amplitude * (float) std::sin (2.0 * 3.14159265358979 * hz * i / sr);
        return v;
    }
}

TEST_CASE ("limiter: no sample ever exceeds -1 dBFS, even on a hot transient")
{
    SafetyLimiter limiter;
    auto l = sine (4800, 4.0f), r = sine (4800, 2.5f, 220.0);
    limiter.process (l.data(), r.data(), (int) l.size());
    for (size_t i = 0; i < l.size(); ++i)
    {
        CHECK (std::abs (l[i]) <= SafetyLimiter::ceiling + 1.0e-6f);
        CHECK (std::abs (r[i]) <= SafetyLimiter::ceiling + 1.0e-6f);
    }
}

TEST_CASE ("limiter: quiet material passes bit-for-bit")
{
    SafetyLimiter limiter;
    auto l = sine (2048, 0.5f), r = sine (2048, 0.25f);
    const auto l0 = l, r0 = r;
    limiter.process (l.data(), r.data(), (int) l.size());
    CHECK (l == l0);
    CHECK (r == r0);
    CHECK (limiter.currentGain() == 1.0f);
}

TEST_CASE ("limiter: releases back toward unity after a peak")
{
    SafetyLimiter limiter;
    std::vector<float> l (1, 2.0f), r (1, 2.0f);
    limiter.process (l.data(), r.data(), 1);
    CHECK (limiter.currentGain() < 0.5f);

    std::vector<float> quietL (48000, 0.1f), quietR (48000, 0.1f);
    limiter.process (quietL.data(), quietR.data(), 48000);
    CHECK (limiter.currentGain() == doctest::Approx (1.0f).epsilon (0.001));
}

TEST_CASE ("gain: default is -12 dB, and the floor is true silence")
{
    CHECK (MonitorGain().currentLinear() == doctest::Approx (std::pow (10.0, -12.0 / 20.0)));
    CHECK (MonitorGain::linearFromDb (-60.0) == 0.0f);
    CHECK (MonitorGain::linearFromDb (-80.0) == 0.0f);
    CHECK (MonitorGain::linearFromDb (0.0) == 1.0f);
}

TEST_CASE ("gain: ramps smoothly and settles exactly on the target")
{
    MonitorGain gain (-12.0, 48000.0);
    gain.setTargetDb (0.0);
    std::vector<float> l (64, 1.0f), r (64, 1.0f);
    gain.process (l.data(), r.data(), 64);
    // Smoothed: no jump straight to unity on the first sample...
    CHECK (l[0] < 0.3f);
    for (size_t i = 1; i < l.size(); ++i)
        CHECK (l[i] >= l[i - 1]);

    // ...but it gets there.
    std::vector<float> longL (48000, 1.0f), longR (48000, 1.0f);
    gain.process (longL.data(), longR.data(), 48000);
    CHECK (gain.currentLinear() == 1.0f);
}

TEST_CASE ("meter: peak, rms and dB maths")
{
    const float ones[] { 0.5f, -1.0f, 0.25f };
    CHECK (MeterMath::peak (ones, 3) == 1.0f);
    CHECK (MeterMath::rms (ones, 3) == doctest::Approx (std::sqrt ((0.25 + 1.0 + 0.0625) / 3.0)));
    CHECK (MeterMath::linearToDb (1.0f) == 0.0f);
    CHECK (std::isinf (MeterMath::linearToDb (0.0f)));
}

TEST_CASE ("meter: clip flags latch until read")
{
    AudioMeter meter;
    std::vector<float> l (256, 0.5f), r (256, 0.5f);
    meter.publish (l.data(), r.data(), 256, 1.2f, 0.5f);
    auto s = meter.snapshot();
    CHECK (s.clippedL);
    CHECK_FALSE (s.clippedR);
    CHECK_FALSE (meter.snapshot().clippedL);
}

TEST_CASE ("meter: release takes ~300 ms whatever the block size (Mac applied per-sample coefficients per block)")
{
    for (int block : { 64, 256, 1024 })
    {
        AudioMeter meter (48000.0);
        std::vector<float> loud ((size_t) block, 1.0f), silent ((size_t) block, 0.0f);
        meter.publish (loud.data(), loud.data(), block, 1.0f, 1.0f);

        // 0.3 s of silence = one release time constant: the peak should fall to ~1/e.
        const int blocks = 14400 / block;
        for (int i = 0; i < blocks; ++i)
            meter.publish (silent.data(), silent.data(), block, 0.0f, 0.0f);

        const float peakDb = meter.snapshot().peakL;
        CHECK (peakDb == doctest::Approx (20.0 * std::log10 (std::exp (-1.0))).epsilon (0.05));
    }
}

TEST_CASE ("meter: the limit lamp lights from the pre-limiter peak above -1 dBFS")
{
    std::vector<float> l (256, 0.1f), r (256, 0.1f);
    AudioMeter meter;
    // 0.95 is above the limiter's ceiling (-1 dBFS = 0.891) but not a clip.
    meter.publish (l.data(), r.data(), 256, 0.95f, 0.5f);
    auto s = meter.snapshot();
    CHECK (s.limitedL);
    CHECK_FALSE (s.limitedR);
    CHECK_FALSE (s.clippedL);
    // Reading clears it, like the clip flag.
    CHECK_FALSE (meter.snapshot().limitedL);
    // Exactly at the ceiling is not "limited".
    meter.publish (l.data(), r.data(), 256, SafetyLimiter::ceiling, 0.0f);
    CHECK_FALSE (meter.snapshot().limitedL);
}

TEST_CASE ("in tap: reads the raw input and leaves the monitored output bit-identical")
{
    // The chain as MonitorEngine runs it (gain -> limiter -> out meter), once
    // with CAPTURE's `in` tap publishing the raw block first and once without.
    auto run = [] (bool tapOn, AudioMeter& in)
    {
        MonitorGain gain (0.0, 48000.0);
        SafetyLimiter limiter;
        AudioMeter out;
        std::vector<float> l = sine (4096, 0.98f, 220.0), r = sine (4096, 0.6f, 330.0), result;
        for (int offset = 0; offset < 4096; offset += 512)
        {
            float* bl = l.data() + offset;
            float* br = r.data() + offset;
            if (tapOn)
                in.publish (bl, br, 512, MeterMath::peak (bl, 512), MeterMath::peak (br, 512));
            gain.process (bl, br, 512);
            limiter.process (bl, br, 512);
            out.publish (bl, br, 512, MeterMath::peak (bl, 512), MeterMath::peak (br, 512));
        }
        result = l;
        result.insert (result.end(), r.begin(), r.end());
        return result;
    };
    AudioMeter tap, unused;
    const auto withTap = run (true, tap);
    const auto without = run (false, unused);
    REQUIRE (withTap.size() == without.size());
    CHECK (std::memcmp (withTap.data(), without.data(), withTap.size() * sizeof (float)) == 0);

    // The tap shows the raw input (its 0.98 peak), not the limited output.
    const auto s = tap.snapshot();
    CHECK (std::pow (10.0, s.peakL / 20.0) == doctest::Approx (0.98).epsilon (0.01));
    CHECK (std::pow (10.0, s.peakR / 20.0) == doctest::Approx (0.6).epsilon (0.01));
    CHECK (s.limitedL); // above the limiter's ceiling on the way in
}
