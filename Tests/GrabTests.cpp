// Ported from HaloTests/GrabMathTests.swift, plus tests for the Windows-side
// improvements: timestamp-to-frame anchoring, downbeat on tick 1/97/..., and
// bar phase surviving Stop/Continue.
#include <doctest/doctest.h>

#include "Core/DisplayReducer.h"
#include "Core/GrabMath.h"
#include "Core/RollingCaptureBuffer.h"

#include <cmath>

using namespace halo;

TEST_CASE ("bar window: exact whole bars ending at the downbeat")
{
    const double sr = 48000.0, bpm = 120.0;
    const auto perBar = (std::int64_t) std::llround (grab::framesPerBeat (sr, bpm) * 4);
    const std::int64_t down = 10'000'000;
    const auto w = grab::barWindow (sr, bpm, 4, 8, down, 0);
    CHECK (w.endFrame == down);
    CHECK (w.bars == 8);
    CHECK (w.frames() == 8 * perBar);
}

TEST_CASE ("bar window: fewer bars when history is short")
{
    const double sr = 48000.0, bpm = 120.0;
    const auto perBar = (std::int64_t) std::llround (grab::framesPerBeat (sr, bpm) * 4);
    const std::int64_t down = 20 * perBar;
    const std::int64_t oldest = down - 10 * perBar - 100;
    const auto w = grab::barWindow (sr, bpm, 4, 16, down, oldest);
    CHECK (w.bars == 10);
    CHECK (w.frames() == 10 * perBar);
    CHECK (w.startFrame >= oldest);
}

TEST_CASE ("bar window: nothing to grab when not even one bar fits")
{
    const auto w = grab::barWindow (48000.0, 120.0, 4, 4, 50'000, 40'000);
    CHECK (w.bars == 0);
    CHECK (w.startFrame == w.endFrame);
}

TEST_CASE ("seconds window: exact frames, clamped to resident history")
{
    auto w = grab::secondsWindow (48000.0, 10.0, 1'000'000, 0);
    CHECK (w.endFrame == 1'000'000);
    CHECK (w.bars == 0);
    CHECK (w.frames() == 480'000);

    w = grab::secondsWindow (48000.0, 10.0, 1'000'000, 600'000);
    CHECK (w.startFrame == 600'000);
    CHECK (w.frames() == 400'000);
}

TEST_CASE ("zero-cross snap lands on a rising crossing within the radius")
{
    // A saw that resets (falling edge) at 40, 140, 240... and rises through
    // zero at 90, 190, 290... The snap must pick a rising crossing, never a reset.
    std::vector<float> mono (1000);
    for (int i = 0; i < 1000; ++i)
        mono[(size_t) i] = (float) ((i + 60) % 100) / 50.0f - 1.0f;

    const int idx = grab::snapToZeroCrossing (mono, 230, 60);
    CHECK (idx == 190);
    CHECK (mono[(size_t) idx - 1] < 0.0f);
    CHECK (mono[(size_t) idx] >= 0.0f);

    std::vector<float> flat (500, 0.5f);
    CHECK (grab::snapToZeroCrossing (flat, 250, 40) == 250);
}

TEST_CASE ("tempo estimate: impulse train at 120 BPM, nothing for a flat signal")
{
    const double sr = 48000.0;
    std::vector<float> mono ((size_t) (sr * 8), 0.0f);
    for (size_t i = 0; i < mono.size(); i += (size_t) (sr * 0.5))
        for (size_t k = 0; k < 400 && i + k < mono.size(); ++k)
            mono[i + k] = 0.9f * (float) std::exp (-(double) k / 80.0);

    const auto bpm = grab::estimateBpm (mono, sr);
    REQUIRE (bpm.has_value());
    CHECK (*bpm == doctest::Approx (120.0).epsilon (0.025));

    std::vector<float> flat ((size_t) (sr * 4), 0.25f);
    CHECK_FALSE (grab::estimateBpm (flat, sr).has_value());
}

TEST_CASE ("file names and titles round-trip")
{
    const grab::Stamp t { 2026, 7, 13, 21, 42, 7 };

    const auto clocked = grab::filename ({ .clocked = true, .bars = 8, .bpm = 92 }, t);
    CHECK (clocked == "grab-2026-07-13-2142-07-08bars-92bpm.wav");
    const auto title = grab::titleFromFilename (clocked);
    REQUIRE (title.has_value());
    CHECK (title->rfind ("GRAB \xC2\xB7 2026-07-13 21.42", 0) == 0);
    CHECK (title->find ("8 BARS") != std::string::npos);
    CHECK (title->find ("92 BPM") != std::string::npos);

    const auto est = grab::filename ({ .seconds = 10, .estBpm = 92 }, t);
    CHECK (est == "grab-2026-07-13-2142-07-10s-est92bpm.wav");
    CHECK (grab::titleFromFilename (est)->find ("~92 BPM (EST)") != std::string::npos);

    const auto plain = grab::filename ({ .seconds = 30 }, t);
    CHECK (plain == "grab-2026-07-13-2142-07-30s.wav");
    CHECK (grab::titleFromFilename (plain)->find ("BPM") == std::string::npos);

    CHECK_FALSE (grab::titleFromFilename ("Halo Take 2026-07-13 21-42-07.wav").has_value());
}

TEST_CASE ("rolling buffer: snapshot round-trips across the wrap")
{
    RollingCaptureBuffer buffer (0.1, 48000.0); // 4800 frames -> 8192 capacity
    CHECK (buffer.capacityFrames() == 8192);

    std::vector<float> l (1000), r (1000);
    std::int64_t total = 0;
    for (int block = 0; block < 20; ++block)
    {
        for (int i = 0; i < 1000; ++i)
        {
            l[(size_t) i] = (float) (total + i);
            r[(size_t) i] = (float) (total + i) + 0.5f;
        }
        buffer.write (l.data(), r.data(), 1000);
        total += 1000;
    }
    CHECK (buffer.nowFrame() == total);

    const std::int64_t start = total - 6000, end = total - 100;
    std::vector<float> dl ((size_t) (end - start)), dr ((size_t) (end - start));
    REQUIRE (buffer.snapshot (start, end, dl.data(), dr.data()));
    for (std::int64_t f = start; f < end; f += 997)
    {
        CHECK (dl[(size_t) (f - start)] == (float) f);
        CHECK (dr[(size_t) (f - start)] == (float) f + 0.5f);
    }

    // Too old to be resident.
    CHECK_FALSE (buffer.snapshot (total - 9000, total, dl.data(), dr.data()));
}

TEST_CASE ("rolling buffer: the session floor hides the previous route")
{
    RollingCaptureBuffer buffer (0.1, 48000.0);
    std::vector<float> l (3000, 0.1f), r (3000, 0.1f);
    buffer.write (l.data(), r.data(), 3000);
    buffer.write (l.data(), r.data(), 3000);
    buffer.resetSession();
    CHECK (buffer.floor() == 6000);
    std::vector<float> dl (3000), dr (3000);
    CHECK_FALSE (buffer.snapshot (2000, 5000, dl.data(), dr.data()));
}

TEST_CASE ("rolling buffer: a MIDI timestamp maps to the audio frame captured then")
{
    RollingCaptureBuffer buffer (1.0, 48000.0);
    CHECK (buffer.frameAt (1.0, 48000.0) == -1); // nothing written yet

    std::vector<float> block (480, 0.0f);
    // Block delivered at t = 10.0 s, 480 frames, 240 frames of input latency.
    buffer.write (block.data(), block.data(), 480, 10.0, 240);
    // At 10.0 s the frame being captured is 480 + 240 = 720.
    CHECK (buffer.frameAt (10.0, 48000.0) == 720);
    // 5 ms earlier is 240 frames earlier.
    CHECK (buffer.frameAt (9.995, 48000.0) == 480);
}

TEST_CASE ("downbeats fall on clock ticks 1, 97, 193 after Start")
{
    ClockTracker clock;
    std::vector<int> downbeats;
    for (int i = 1; i <= 200; ++i)
    {
        clock.tick (i * 0.02);
        if (clock.lastTickWasDownbeat())
            downbeats.push_back (clock.clockCount());
    }
    CHECK (downbeats == std::vector<int> { 1, 97, 193 });
}

TEST_CASE ("bar phase survives Stop then Continue")
{
    DisplayReducer r;
    r.connectionChanged (true);
    MidiEvent e;
    e.kind = MidiKind::start;
    r.apply (e, 0.0);
    e.kind = MidiKind::timingClock;
    for (int i = 1; i <= 50; ++i)
    {
        e.timeSeconds = i * 0.02;
        r.apply (e, e.timeSeconds);
    }
    e.kind = MidiKind::stop;
    r.apply (e, 1.1);
    CHECK (r.clock().clockCount() == 50);
    e.kind = MidiKind::continuePlaying;
    r.apply (e, 1.2);
    e.kind = MidiKind::timingClock;
    e.timeSeconds = 1.22;
    r.apply (e, 1.22);
    CHECK (r.clock().clockCount() == 51);
}
