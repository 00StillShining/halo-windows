// Ported from HaloTests/DubRackTests.swift (+ RackModel tempo tests), plus a
// test for the per-block smoothing fix.
#include <doctest/doctest.h>

#include "Core/DubRack.h"

#include <algorithm>
#include <cstdint>
#include <vector>

using namespace halo;

namespace
{
    constexpr double sr = 48000.0;
    constexpr int block = 512;

    // Deterministic pseudo-random block in [-0.5, 0.5].
    std::vector<float> noise (std::uint64_t seed)
    {
        std::uint64_t s = seed + 0x9E3779B97F4A7C15ull;
        std::vector<float> out (block);
        for (auto& v : out)
        {
            s ^= s >> 12; s ^= s << 25; s ^= s >> 27;
            v = (float) ((s * 0x2545F4914F6CDD1Dull) >> 40) / (float) (1 << 24) - 0.5f;
        }
        return out;
    }

    void run (DubRack& rack, const RackParameters& p, std::uint64_t seed, std::vector<float>* outL = nullptr)
    {
        auto l = noise (seed), r = noise (seed + 7777);
        rack.process (l.data(), r.data(), block, p);
        if (outL != nullptr)
            *outL = l;
    }
}

TEST_CASE ("rack: disengaged bypass is bit-transparent (null test)")
{
    DubRack rack;
    rack.prepare (sr, block);
    RackParameters p;
    for (std::uint64_t seed = 0; seed < 40; ++seed)
    {
        auto l = noise (seed), r = noise (seed + 1);
        const auto l0 = l, r0 = r;
        rack.process (l.data(), r.data(), block, p);
        CHECK (l == l0);
        CHECK (r == r0);
    }
}

TEST_CASE ("rack: returns to bit-transparency after disengaging")
{
    DubRack rack;
    rack.prepare (sr, block);
    RackParameters p;
    p.engaged = true;
    for (std::uint64_t s = 0; s < 40; ++s) run (rack, p, s);
    p.engaged = false;
    for (std::uint64_t s = 100; s < 140; ++s) run (rack, p, s);
    CHECK (rack.isSettledBypass());

    for (std::uint64_t seed = 200; seed < 220; ++seed)
    {
        auto l = noise (seed), r = noise (seed + 1);
        const auto l0 = l;
        rack.process (l.data(), r.data(), block, p);
        CHECK (l == l0);
    }
}

TEST_CASE ("rack: no clicks when engaging or sweeping")
{
    DubRack rack;
    rack.prepare (sr, block);
    RackParameters p;
    p.springOn = true;
    p.sweepOn = true;

    // A quiet sine: any discontinuity would show as a big sample-to-sample jump.
    float phase = 0.0f, previous = 0.0f, worstJump = 0.0f;
    for (int b = 0; b < 200; ++b)
    {
        if (b == 20) p.engaged = true;
        p.sweepMacro = 0.5f + 0.45f * std::sin ((float) b * 0.05f);
        std::vector<float> l (block), r (block);
        for (int i = 0; i < block; ++i)
        {
            l[(size_t) i] = r[(size_t) i] = 0.2f * std::sin (phase);
            phase += 2.0f * 3.14159265f * 220.0f / (float) sr;
        }
        rack.process (l.data(), r.data(), block, p);
        for (float v : l)
        {
            worstJump = std::max (worstJump, std::abs (v - previous));
            previous = v;
        }
    }
    CHECK (worstJump < 0.15f);
}

TEST_CASE ("rack: echo self-oscillation stays bounded")
{
    DubRack rack;
    rack.prepare (sr, block);
    RackParameters p;
    p.engaged = true;
    p.echoFeedback = 1.0f; // 1.05 internally: self-oscillates
    p.echoMix = 1.0f;
    p.echoTimeMs = 80.0f;

    float peak = 0.0f;
    for (int b = 0; b < 600; ++b) // ~6.4 s
    {
        auto l = b < 5 ? noise ((std::uint64_t) b) : std::vector<float> (block, 0.0f);
        auto r = l;
        rack.process (l.data(), r.data(), block, p);
        for (float v : l)
        {
            CHECK_FALSE (std::isnan (v));
            peak = std::max (peak, std::abs (v));
        }
    }
    // tanh in the loop bounds the line; the -1 dBFS limiter after it catches the rest.
    CHECK (peak < 2.5f);
}

TEST_CASE ("rack: engaged with every module off is transparent once settled")
{
    DubRack rack;
    rack.prepare (sr, block);
    RackParameters p;
    p.engaged = true;
    p.echoOn = false;
    for (std::uint64_t s = 0; s < 40; ++s) run (rack, p, s); // let enables fade out

    auto l = noise (999), r = noise (1000);
    const auto l0 = l;
    rack.process (l.data(), r.data(), block, p);
    for (size_t i = 0; i < l.size(); ++i)
        CHECK (l[i] == doctest::Approx (l0[i]).epsilon (1.0e-4));
}

TEST_CASE ("rack: SWEEP glides in ~20 ms whatever the block size (Mac smoothed once per block)")
{
    // Engage SWEEP at a fixed setting, then compare how much of the change has
    // happened after 100 ms with small and large blocks: it should be ~all of it.
    for (int blockSize : { 64, 480, 1024 })
    {
        DubRack rack;
        rack.prepare (sr, blockSize);
        RackParameters p;
        p.engaged = true;
        p.echoOn = false;
        p.sweepOn = true;
        p.sweepMacro = 0.0f; // low-pass at 60 Hz: kills a 5 kHz tone once it's in
        int processed = 0;
        float lastPeak = 1.0f;
        float phase = 0.0f;
        while (processed < (int) (0.3 * sr))
        {
            std::vector<float> l ((size_t) blockSize), r ((size_t) blockSize);
            for (auto& v : l) { v = std::sin (phase); phase += 2.0f * 3.14159265f * 5000.0f / (float) sr; }
            r = l;
            rack.process (l.data(), r.data(), blockSize, p);
            lastPeak = 0.0f;
            for (float v : l) lastPeak = std::max (lastPeak, std::abs (v));
            processed += blockSize;
        }
        CHECK (lastPeak < 0.05f); // filtered after 300 ms, not still fading in
    }
}

TEST_CASE ("echo time: synced from tempo, free otherwise, clamped")
{
    CHECK (effectiveEchoMs (true, EchoDivision::eighth, 120.0, 360.0) == doctest::Approx (250.0));
    CHECK (effectiveEchoMs (true, EchoDivision::dottedEighth, 120.0, 360.0) == doctest::Approx (375.0));
    CHECK (effectiveEchoMs (true, EchoDivision::quarter, 120.0, 360.0) == doctest::Approx (500.0));
    CHECK (effectiveEchoMs (true, EchoDivision::tripletEighth, 120.0, 360.0) == doctest::Approx (166.667).epsilon (0.001));
    CHECK (effectiveEchoMs (true, EchoDivision::eighth, std::nullopt, 360.0) == 360.0);
    CHECK (effectiveEchoMs (false, EchoDivision::eighth, 120.0, 360.0) == 360.0);
    CHECK (effectiveEchoMs (true, EchoDivision::quarter, 30.0, 360.0) == 1200.0);
    CHECK (effectiveEchoMs (false, EchoDivision::eighth, std::nullopt, 5.0) == 40.0);
}

TEST_CASE ("tap tempo: averages taps and restarts after a 2 s gap")
{
    TapTempo tap;
    CHECK_FALSE (tap.tap (10.0).has_value());
    CHECK (tap.tap (10.5) == 120.0);
    CHECK (tap.tap (11.0) == 120.0);
    CHECK (tap.tap (11.5) == 120.0);
    // After a long gap, a new run at 100 BPM takes over.
    tap.tap (20.0);
    CHECK (tap.tap (20.6) == 100.0);
}
