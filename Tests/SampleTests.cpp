// Ported from SamplePrepTests / SampleProcessorTests / OnsetDetectorTests.swift
// (the pure parts: prep, treatment maths, size estimates, waveform summary,
// onset detection, slices and the pad plan).
#include <doctest/doctest.h>

#include "Core/Chop.h"
#include "Core/SampleCore.h"

#include <cmath>
#include <numeric>

using namespace halo;

namespace
{
    AudioData stereoRamp (int frames, double sr = 48000.0)
    {
        AudioData a;
        a.sampleRate = sr;
        a.channels.assign (2, std::vector<float> ((size_t) frames));
        for (int i = 0; i < frames; ++i)
        {
            a.channels[0][(size_t) i] = (float) i / (float) frames;
            a.channels[1][(size_t) i] = -(float) i / (float) frames;
        }
        return a;
    }

    // Decaying noise bursts at known positions over a quiet bed.
    std::vector<float> clickTrack (double sr, const std::vector<double>& seconds, double total)
    {
        std::vector<float> v ((size_t) (sr * total), 0.0f);
        unsigned seed = 1;
        for (auto& x : v)
        {
            seed = seed * 1664525u + 1013904223u;
            x = 0.002f * ((float) (seed >> 8) / 16777216.0f - 0.5f);
        }
        for (double t : seconds)
            for (int k = 0; k < (int) (sr * 0.08); ++k)
            {
                seed = seed * 1664525u + 1013904223u;
                const size_t i = (size_t) (t * sr) + (size_t) k;
                if (i < v.size())
                    v[i] += 0.8f * std::exp (-(float) k / (float) (sr * 0.015)) * ((float) (seed >> 8) / 16777216.0f - 0.5f) * 2.0f;
            }
        return v;
    }
}

TEST_CASE ("prep: trim selects the window and leaves the source untouched")
{
    const auto src = stereoRamp (1000);
    SamplePrep p;
    p.trimStartFrame = 100;
    p.trimEndFrame = 300;
    const auto out = p.apply (src);
    CHECK (out.frameCount() == 200);
    CHECK (out.channels[0][0] == src.channels[0][100]);
    CHECK (src.frameCount() == 1000);
}

TEST_CASE ("prep: trim clamps out-of-range and empty selections")
{
    const auto src = stereoRamp (100);
    SamplePrep p;
    p.trimStartFrame = 90;
    p.trimEndFrame = 500;
    CHECK (p.outputFrameCount (100) == 10);
    p.trimEndFrame = 50; // end before start -> empty
    CHECK (p.apply (src).frameCount() == 0);
    CHECK (p.apply (src).channelCount() == 2);
}

TEST_CASE ("prep: identity is a passthrough and gain scales linearly")
{
    const auto src = stereoRamp (64);
    CHECK (SamplePrep{}.apply (src).channels == src.channels);

    SamplePrep p;
    p.gainDecibels = -6.0f;
    const auto out = p.apply (src);
    CHECK (out.channels[0][32] == doctest::Approx (src.channels[0][32] * std::pow (10.0f, -6.0f / 20.0f)));
}

TEST_CASE ("prep: equal-power fades go 0 -> 1 and 1 -> 0, and clamp to the buffer")
{
    AudioData ones;
    ones.sampleRate = 48000.0;
    ones.channels.assign (1, std::vector<float> (100, 1.0f));

    SamplePrep p;
    p.fadeInFrames = 11;
    p.fadeOutFrames = 11;
    auto out = p.apply (ones);
    CHECK (out.channels[0][0] == doctest::Approx (0.0f));
    CHECK (out.channels[0][10] == doctest::Approx (1.0f));
    CHECK (out.channels[0][5] == doctest::Approx (std::sin (0.25f * 3.14159265f)));
    CHECK (out.channels[0][89] == doctest::Approx (1.0f));
    CHECK (out.channels[0][99] == doctest::Approx (0.0f).epsilon (1e-6));

    p.fadeInFrames = 1000;
    p.fadeOutFrames = 0;
    out = p.apply (ones);
    CHECK (out.channels[0][99] == doctest::Approx (1.0f));
}

TEST_CASE ("prep: normalise scales the peak to -1 dBFS and never invents gain on silence")
{
    auto src = stereoRamp (100);
    SamplePrep p;
    p.normalize = true;
    auto out = p.apply (src);
    float peak = 0.0f;
    for (const auto& c : out.channels)
        for (float v : c)
            peak = std::max (peak, std::abs (v));
    CHECK (peak == doctest::Approx (std::pow (10.0f, -1.0f / 20.0f)));

    AudioData silence;
    silence.sampleRate = 48000.0;
    silence.channels.assign (2, std::vector<float> (100, 0.0f));
    out = p.apply (silence);
    CHECK (out.channels[0][50] == 0.0f);
}

TEST_CASE ("prep: equal-power mono downmix, and mono -> stereo copies")
{
    AudioData st;
    st.sampleRate = 48000.0;
    st.channels = { { 1.0f, 0.0f }, { 1.0f, 1.0f } };
    SamplePrep p;
    p.channelMode = SamplePrep::Channels::mono;
    const auto mono = p.apply (st);
    REQUIRE (mono.channelCount() == 1);
    CHECK (mono.channels[0][0] == doctest::Approx (2.0f / std::sqrt (2.0f)));
    CHECK (mono.channels[0][1] == doctest::Approx (1.0f / std::sqrt (2.0f))); // not a naive discard

    AudioData m;
    m.sampleRate = 48000.0;
    m.channels = { { 0.5f, -0.5f } };
    p.channelMode = SamplePrep::Channels::stereo;
    const auto s = p.apply (m);
    REQUIRE (s.channelCount() == 2);
    CHECK (s.channels[1] == m.channels[0]);
}

TEST_CASE ("treatment: rates and frame counts")
{
    CHECK (resolvedSampleRate (Treatment::original, 44100.0) == 44100.0);
    CHECK (resolvedSampleRate (Treatment::original, 96000.0) == deviceMaxSampleRate);
    CHECK (resolvedSampleRate (Treatment::high, 44100.0) == deviceMaxSampleRate);
    CHECK (resolvedSampleRate (Treatment::balanced, 44100.0) == 32000.0);
    CHECK (resolvedSampleRate (Treatment::loFi22k, 44100.0) == 22050.0);
    CHECK (resolvedSampleRate (Treatment::loFi11k, 44100.0) == 11025.0);
    CHECK (outputFrameCount (Treatment::loFi22k, 44100, 44100.0) == 22050);
    CHECK (outputFrameCount (Treatment::original, 1234, 44100.0) == 1234);
}

TEST_CASE ("size estimate reflects trim, mono and treatment exactly")
{
    SamplePrep p;
    p.trimStartFrame = 0;
    p.trimEndFrame = 44100;
    p.channelMode = SamplePrep::Channels::mono;
    const auto e = estimateSize (88200, 2, 44100.0, p, Treatment::loFi22k);
    CHECK (e.frames == 22050);
    CHECK (e.channels == 1);
    CHECK (e.payloadBytes == 22050 * 1 * 2);
    CHECK (e.durationSeconds() == doctest::Approx (1.0));

    const auto full = estimateSize (88200, 2, 44100.0, SamplePrep{}, Treatment::original);
    CHECK (full.payloadBytes == 88200LL * 2 * 2);
    CHECK (formatBytes (full.payloadBytes) == "344.5 KB"); // 352800 B
    CHECK (formatBytes (3 * 1024 * 1024) == "3.00 MB");
}

TEST_CASE ("resample: exact length, keeps an in-band tone, removes what can't fit")
{
    const double src = 44100.0, dst = 11025.0;
    auto tone = [src] (double hz, int frames)
    {
        std::vector<float> v ((size_t) frames);
        for (int i = 0; i < frames; ++i)
            v[(size_t) i] = 0.5f * (float) std::sin (2.0 * 3.141592653589793 * hz * i / src);
        return v;
    };
    auto rms = [] (const std::vector<float>& v, size_t from, size_t to)
    {
        double s = 0.0;
        for (size_t i = from; i < to; ++i) s += (double) v[i] * v[i];
        return std::sqrt (s / (double) (to - from));
    };

    // 1 kHz survives 44.1 -> 11.025 kHz at its level (0.5 / sqrt 2).
    const auto low = resample (tone (1000.0, 44100), src, dst);
    CHECK (low.size() == 11025);
    CHECK (rms (low, 1000, 10000) == doctest::Approx (0.5 / std::sqrt (2.0)).epsilon (0.02));

    // 9 kHz is above the new Nyquist (5.5 kHz): it must be filtered, not aliased to 2 kHz.
    const auto high = resample (tone (9000.0, 44100), src, dst);
    CHECK (rms (high, 1000, 10000) < 0.01);

    // The length always matches the size estimate.
    CHECK ((int) resample (tone (440.0, 12345), src, 46875.0).size() == outputFrameCount (Treatment::high, 12345, src));
}

TEST_CASE ("render = prep then treatment rate")
{
    const auto src = stereoRamp (44100, 44100.0);
    SamplePrep p;
    p.channelMode = SamplePrep::Channels::mono;
    const auto out = render (src, p, Treatment::loFi22k);
    CHECK (out.sampleRate == 22050.0);
    CHECK (out.channelCount() == 1);
    CHECK (out.frameCount() == 22050);
}

TEST_CASE ("waveform summary: min/max per bucket, tiles the signal, never more buckets than frames")
{
    std::vector<float> v { 0.1f, -0.5f, 0.3f, 0.9f, -0.2f };
    auto s = WaveformSummary::build (v, 2);
    CHECK (s.bucketCount() == 2);
    CHECK (s.framesPerBucket == 3);
    CHECK (s.minima[0] == -0.5f);
    CHECK (s.maxima[0] == 0.3f);
    CHECK (s.maxima[1] == 0.9f);
    CHECK (s.peakMagnitude() == 0.9f);
    CHECK (WaveformSummary::build (v, 100).bucketCount() == 5);
    CHECK (WaveformSummary::build ({}, 10).bucketCount() == 0);
}

TEST_CASE ("onsets: planted transients are found within one hop")
{
    const double sr = 48000.0;
    const std::vector<double> planted { 0.25, 0.75, 1.25, 1.75 };
    const auto mono = clickTrack (sr, planted, 2.2);
    const auto onsets = chop::detectOnsets (mono, sr, {});
    REQUIRE (onsets.size() == planted.size());
    for (size_t i = 0; i < planted.size(); ++i)
        CHECK (std::abs (onsets[i] - (int) (planted[i] * sr)) <= 1024);
}

TEST_CASE ("onsets: noisy snare tails don't add extra cuts, and no sliver slice at the start")
{
    // 16 hits on 8ths at 120 BPM, the first at 0 s, each a long noisy decay.
    const double sr = 44100.0;
    std::vector<double> hits;
    for (int i = 0; i < 16; ++i)
        hits.push_back (i * 0.25);
    const auto mono = clickTrack (sr, hits, 4.0);
    const auto onsets = chop::detectOnsets (mono, sr, {});
    CHECK (onsets.size() == 15); // every hit after the first, nothing else
    for (int o : onsets)
        CHECK (o >= (int) (0.040 * sr));
}

TEST_CASE ("onsets: higher sensitivity never finds fewer; silence and short input find none")
{
    const double sr = 48000.0;
    const auto mono = clickTrack (sr, { 0.2, 0.5, 0.6, 0.9, 1.3, 1.31 }, 1.6);
    chop::OnsetParameters low, high;
    low.sensitivity = 0.1;
    high.sensitivity = 0.9;
    CHECK (chop::detectOnsets (mono, sr, high).size() >= chop::detectOnsets (mono, sr, low).size());

    CHECK (chop::detectOnsets (std::vector<float> (48000, 0.0f), sr, {}).empty());
    CHECK (chop::detectOnsets (std::vector<float> (500, 0.5f), sr, {}).empty());
}

TEST_CASE ("slices tile the whole signal; no onsets means one slice")
{
    chop::SliceSet set (1000, { 300, 100, 300, 0, 1000, 700 });
    CHECK (set.cuts() == std::vector<int> { 100, 300, 700 });
    const auto s = set.slices();
    REQUIRE (s.size() == 4);
    CHECK (s.front().startFrame == 0);
    CHECK (s.back().endFrame == 1000);
    int total = 0;
    for (const auto& x : s)
        total += x.frameCount();
    CHECK (total == 1000);

    CHECK (chop::SliceSet (1000, {}).sliceCount() == 1);
    CHECK (set.sliceAt (50) == 0);
    CHECK (set.sliceAt (300) == 2);
}

TEST_CASE ("slice edits: add respects the min gap, move clamps, remove merges")
{
    chop::SliceSet set (1000, { 500 });
    CHECK_FALSE (set.addCut (520, 50));
    CHECK_FALSE (set.addCut (20, 50));
    CHECK (set.addCut (200, 50));
    CHECK (set.cuts() == std::vector<int> { 200, 500 });

    set.moveCut (0, 480, 50); // can't pass the next cut minus the gap
    CHECK (set.cuts()[0] == 450);
    set.removeCut (0);
    CHECK (set.sliceCount() == 2);
}

TEST_CASE ("pad plan: names, pads, exact bytes, overflow reported not wrapped")
{
    std::vector<chop::Slice> slices;
    for (int i = 0; i < 5; ++i)
        slices.push_back ({ i, i * 44100, (i + 1) * 44100 });

    auto plan = chop::planPads (slices, 2, Treatment::loFi22k, 44100.0, 1, 9, " amen ");
    CHECK (plan.writes.size() == 3); // pads 9, 10, 11 only
    CHECK (plan.overflowCount == 2);
    CHECK (plan.writes[0].name == "AMEN.01");
    CHECK (plan.writes[2].gridIndex == 11);
    CHECK (plan.writes[0].group == 1);
    CHECK (plan.writes[0].payloadBytes == 22050LL * 2 * 2);
    CHECK (plan.totalBytes == 3 * 22050LL * 2 * 2);

    plan = chop::planPads (slices, 1, Treatment::original, 44100.0, 0, 0, "");
    CHECK (plan.overflowCount == 0);
    CHECK (plan.writes[4].name == "CHOP.05");
}
