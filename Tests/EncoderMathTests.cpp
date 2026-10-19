#include "Core/EncoderMath.h"

#include <doctest/doctest.h>

using namespace halo::encoder;

TEST_CASE ("encoder: one wheel notch is one detent; touchpads accumulate")
{
    DetentAccumulator acc;
    CHECK (acc.feed (kNotch) == 1);
    CHECK (acc.feed (-kNotch) == -1);
    CHECK (acc.feed (3.0 * kNotch) == 3);

    acc.reset();
    // A touchpad sends small deltas: nothing until about a notch has passed.
    int total = 0;
    for (int i = 0; i < 10; ++i)
        total += acc.feed (kNotch / 10.0);
    CHECK (total == 1);

    // Many slow notches never drift into an extra step.
    acc.reset();
    total = 0;
    for (int i = 0; i < 100; ++i)
        total += acc.feed (kNotch);
    CHECK (total == 100);

    // A tiny wiggle back and forth does nothing.
    acc.reset();
    CHECK (acc.feed (kNotch * 0.3) == 0);
    CHECK (acc.feed (-kNotch * 0.3) == 0);
}

TEST_CASE ("encoder: alt is fine (1/10), ctrl is coarse (x10)")
{
    CHECK (stepFor (Speed::normal, 0.02) == doctest::Approx (0.02));
    CHECK (stepFor (Speed::fine, 0.02) == doctest::Approx (0.002));
    CHECK (stepFor (Speed::coarse, 0.02) == doctest::Approx (0.2));
    // A binding may name its own fine and coarse steps.
    CHECK (stepFor (Speed::fine, 0.02, 0.005) == doctest::Approx (0.005));
    CHECK (stepFor (Speed::coarse, 0.02, 0.0, 0.25) == doctest::Approx (0.25));

    CHECK (turn (0.5, 1, 0.02) == doctest::Approx (0.52));
    CHECK (turn (0.5, -3, 0.02, Speed::fine) == doctest::Approx (0.494));
    CHECK (turn (0.5, 1, 0.02, Speed::coarse) == doctest::Approx (0.7));
    // Clamped to the range.
    CHECK (turn (0.99, 5, 0.02) == doctest::Approx (1.0));
    CHECK (turn (0.01, -5, 0.02) == doctest::Approx (0.0));
}

TEST_CASE ("encoder: discrete parameters step one position per detent")
{
    CHECK (turnDiscrete (1, 1, 5) == 2);
    CHECK (turnDiscrete (1, -1, 5) == 0);
    CHECK (turnDiscrete (0, -1, 5) == 0);
    CHECK (turnDiscrete (4, 3, 5) == 4);
    CHECK (turnDiscrete (2, 1, 5, Speed::fine) == 3);    // nothing finer than a step
    CHECK (turnDiscrete (2, 1, 5, Speed::coarse) == 4);  // coarse jumps to an end
    CHECK (turnDiscrete (2, -1, 5, Speed::coarse) == 0);
    CHECK (turnDiscrete (0, 1, 0) == 0);
}

TEST_CASE ("encoder: 180 px of drag is the full range; alt drag is fine")
{
    CHECK (drag (0.0, 180.0, false) == doctest::Approx (1.0));
    CHECK (drag (0.5, 90.0, false) == doctest::Approx (1.0));
    CHECK (drag (0.5, -45.0, false) == doctest::Approx (0.25));
    CHECK (drag (0.5, 180.0, true) == doctest::Approx (0.6));
    CHECK (drag (0.0, -10.0, false) == doctest::Approx (0.0));

    CHECK (dragDiscrete (0, 180.0, 5, false) == 4);
    CHECK (dragDiscrete (0, 45.0, 5, false) == 1);
    CHECK (dragDiscrete (2, -50.0, 5, false) == 1);
    CHECK (dragDiscrete (2, 45.0, 5, true) == 2); // fine needs 10x the travel
}

TEST_CASE ("encoder: a wheel gesture ends 250 ms after the last detent")
{
    GestureClock clock;
    CHECK (clock.event (10.0));        // begins
    CHECK_FALSE (clock.event (10.1));  // continues
    CHECK_FALSE (clock.poll (10.2));   // 100 ms after the last
    CHECK_FALSE (clock.event (10.3));  // continues
    CHECK_FALSE (clock.poll (10.5));   // 200 ms
    CHECK (clock.poll (10.56));        // 260 ms: ends, once
    CHECK_FALSE (clock.poll (11.0));
    CHECK (clock.event (11.1));        // a new gesture
}

TEST_CASE ("encoder: pointer sweeps 270 degrees")
{
    constexpr double pi = 3.14159265358979323846;
    CHECK (pointerAngle (0.0) == doctest::Approx (-135.0 * pi / 180.0));
    CHECK (pointerAngle (0.5) == doctest::Approx (0.0));
    CHECK (pointerAngle (1.0) == doctest::Approx (135.0 * pi / 180.0));
}
