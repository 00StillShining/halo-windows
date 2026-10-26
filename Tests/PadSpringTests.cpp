#include <doctest/doctest.h>

#include "Core/PadSpring.h"

#include <algorithm>

using namespace halo;

namespace
{
    constexpr float travel = 0.0013f; // metres

    // Seconds until the spring stays within 2 % of travel of the target.
    float settleTime (PadSpring s, float target, float dt, float limit = 0.5f)
    {
        float t = 0.0f, lastOutside = 0.0f;
        while (t < limit)
        {
            s.step (target, dt);
            t += dt;
            if (std::abs (s.depth - target) > 0.02f * travel)
                lastOutside = t;
        }
        return lastOutside;
    }
}

TEST_CASE ("pad spring: a press settles within 90 ms")
{
    for (float dt : { 1.0f / 30.0f, 1.0f / 60.0f, 1.0f / 144.0f, 0.001f })
        CHECK_MESSAGE (settleTime ({}, travel, dt) <= 0.090f, "dt " << dt);
}

TEST_CASE ("pad spring: the release overshoots rest by at most 6 % of travel")
{
    PadSpring s;
    s.depth = travel;
    float highest = 0.0f;   // above rest: negative depth
    for (int i = 0; i < 2000; ++i)
    {
        s.step (0.0f, 0.0005f);
        highest = std::max (highest, -s.depth);
    }
    CHECK (highest > 0.0f);                 // a barely perceptible bounce...
    CHECK (highest <= 0.06f * travel);      // ...within 6 %
    CHECK (s.settled (0.0f));
}

TEST_CASE ("pad spring: 30 fps and 144 fps frames agree within 0.1 mm")
{
    PadSpring a, b;
    // Press for 100 ms, release for 200 ms, comparing at shared instants.
    for (int ms = 0; ms < 300; ms += 100)
    {
        const float target = ms < 100 ? travel : 0.0f;
        for (int i = 0; i < 3; ++i) a.step (target, 1.0f / 30.0f);
        for (int i = 0; i < 14; ++i) b.step (target, 1.0f / 144.0f);
        b.step (target, 0.1f - 14.0f / 144.0f);
        CHECK (std::abs (a.depth - b.depth) <= 0.0001f);
    }
}

TEST_CASE ("pad spring: travel does not depend on velocity")
{
    // The target is the key's one travel: the spring has no velocity input,
    // and its settled depth is the target exactly.
    PadSpring s;
    for (int i = 0; i < 400; ++i)
        s.step (travel, 0.001f);
    CHECK (s.depth == doctest::Approx (travel).epsilon (0.001));
    CHECK (s.settled (travel));
}
