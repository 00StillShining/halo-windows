// Display reducer behaviour, ported from the MIDI half of HaloShellTests /
// HaloLifecycleTests, plus tests for the stale-clock fix.
#include <doctest/doctest.h>

#include "Core/DisplayReducer.h"

using namespace halo;

namespace
{
    MidiEvent note (bool on, std::uint8_t n, std::uint8_t vel = 100, std::uint8_t ch = 0)
    {
        MidiEvent e;
        e.kind = on ? MidiKind::noteOn : MidiKind::noteOff;
        e.channel = ch;
        e.data1 = n;
        e.data2 = on ? vel : 0;
        return e;
    }

    MidiEvent simple (MidiKind kind, double t = 0.0)
    {
        MidiEvent e;
        e.kind = kind;
        e.timeSeconds = t;
        return e;
    }
}

TEST_CASE ("starts in preview, connecting waits, the first event goes live")
{
    DisplayReducer r;
    CHECK (r.state().feedMode == FeedMode::preview);

    r.connectionChanged (true);
    CHECK (r.state().feedMode == FeedMode::waiting);

    CHECK (r.apply (note (true, 40, 127), 0.0));
    CHECK (r.state().feedMode == FeedMode::live);
}

TEST_CASE ("a note shows its value, group, pad and velocity")
{
    DisplayReducer r;
    r.connectionChanged (true);
    r.apply (note (true, 52, 127), 0.0); // group B, offset 4 = pad "2"
    const auto& s = r.state();
    CHECK (s.mode == DisplayMode::sound);
    CHECK (s.value == 52);
    CHECK (s.activeGroup == 1);
    CHECK (s.activePadIndex == 7);
    CHECK (s.velocity == doctest::Approx (1.0));
}

TEST_CASE ("releasing the frontmost key falls back to the previous held key")
{
    DisplayReducer r;
    r.connectionChanged (true);
    r.apply (note (true, 36), 0.0);
    r.apply (note (true, 40), 0.1);
    r.apply (note (false, 40), 0.2);
    CHECK (r.state().value == 36);
    CHECK (r.heldKeyCount() == 1);

    r.apply (note (false, 36), 0.3);
    CHECK_FALSE (r.state().activePadIndex.has_value());
    CHECK (r.heldKeyCount() == 0);
}

TEST_CASE ("disconnecting releases every held key and returns to preview")
{
    DisplayReducer r;
    r.connectionChanged (true);
    r.apply (note (true, 36), 0.0);
    r.apply (note (true, 37), 0.0);
    r.connectionChanged (false);
    CHECK (r.heldKeyCount() == 0);
    CHECK (r.state().feedMode == FeedMode::preview);
}

TEST_CASE ("MIDI clock produces a tempo reading")
{
    DisplayReducer r;
    r.connectionChanged (true);
    const double perClock = 60.0 / (120.0 * 24.0);
    double t = 1.0;
    for (int i = 0; i < 96; ++i, t += perClock)
        r.apply (simple (MidiKind::timingClock, t), t);

    CHECK (r.state().mode == DisplayMode::tempo);
    CHECK (r.state().value == 120);
    REQUIRE (r.clock().bpm (t).has_value());
    CHECK (*r.clock().bpm (t) == doctest::Approx (120.0).epsilon (0.01));
}

TEST_CASE ("Stop forgets the clock (Mac audit finding #5)")
{
    DisplayReducer r;
    r.connectionChanged (true);
    const double perClock = 60.0 / (100.0 * 24.0);
    double t = 0.0;
    for (int i = 0; i < 48; ++i, t += perClock)
        r.apply (simple (MidiKind::timingClock, t), t);
    REQUIRE (r.clock().bpm (t).has_value());

    r.apply (simple (MidiKind::stop, t), t);
    CHECK_FALSE (r.clock().bpm (t).has_value());
    CHECK_FALSE (r.state().isPlaying);
}

TEST_CASE ("a clock that stops arriving goes stale")
{
    DisplayReducer r;
    r.connectionChanged (true);
    const double perClock = 60.0 / (90.0 * 24.0);
    double t = 0.0;
    for (int i = 0; i < 48; ++i, t += perClock)
        r.apply (simple (MidiKind::timingClock, t), t);

    CHECK (r.clock().bpm (t).has_value());
    CHECK_FALSE (r.clock().bpm (t + 1.0).has_value());
    CHECK (r.expire (t + 1.0));
    CHECK_FALSE (r.expire (t + 2.0)); // only once
}

TEST_CASE ("clock redraws are thinned to every third tick")
{
    DisplayReducer r;
    r.connectionChanged (true);
    r.apply (simple (MidiKind::start, 0.0), 0.0);
    int redraws = 0;
    for (int i = 1; i <= 24; ++i)
        redraws += r.apply (simple (MidiKind::timingClock, i * 0.02), i * 0.02) ? 1 : 0;
    CHECK (redraws == 8);
}

TEST_CASE ("song position sets the bar phase")
{
    DisplayReducer r;
    r.connectionChanged (true);
    MidiEvent e = simple (MidiKind::songPosition);
    e.songPosition = 18;
    r.apply (e, 0.0);
    CHECK (r.clock().clockCount() == 12);
    CHECK (r.state().value == 18);
}

TEST_CASE ("preview frames only apply while previewing")
{
    DisplayReducer r;
    CHECK (r.showPreview (DisplayState::previewFrame (4)));
    r.connectionChanged (true);
    CHECK_FALSE (r.showPreview (DisplayState::previewFrame (5)));
}
