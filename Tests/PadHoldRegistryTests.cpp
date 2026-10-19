// Ported from HaloTests/PadHoldRegistryTests.swift.
#include <doctest/doctest.h>

#include "Core/PadHoldRegistry.h"

#include <set>

using namespace halo;
using Kind = PadHoldRegistry::Kind;

namespace
{
    constexpr int sharedPad = 9; // notes 36 and 48 are both the "." pad
}

TEST_CASE ("two groups sharing a physical pad are refcounted")
{
    PadHoldRegistry r;
    const auto a = r.noteOn (0, 36, 0.5f);
    REQUIRE (a.has_value());
    CHECK (a->pad == sharedPad);
    CHECK (a->kind == Kind::press);
    CHECK (a->velocity == 0.5f);

    const auto b = r.noteOn (0, 48, 0.9f);
    REQUIRE (b.has_value());
    CHECK (b->pad == sharedPad);
    CHECK (b->kind == Kind::restrike);
    CHECK (b->velocity == 0.9f);

    CHECK (r.noteOff (0, 36)->kind == Kind::sustain);
    CHECK (r.noteOff (0, 48)->kind == Kind::release);
}

TEST_CASE ("a duplicate Note On restrikes and a single Note Off releases")
{
    PadHoldRegistry r;
    CHECK (r.noteOn (0, 40, 0.3f)->kind == Kind::press);
    CHECK (r.noteOn (0, 40, 0.7f)->kind == Kind::restrike);
    CHECK (r.noteOff (0, 40)->kind == Kind::release);
}

TEST_CASE ("an orphan Note Off is ignored")
{
    PadHoldRegistry r;
    CHECK_FALSE (r.noteOff (0, 40).has_value());
}

TEST_CASE ("out-of-range notes do nothing")
{
    PadHoldRegistry r;
    for (std::uint8_t n : { 35, 84, 0 })
    {
        CHECK_FALSE (r.noteOn (0, n, 1.0f).has_value());
        CHECK_FALSE (r.noteOff (0, n).has_value());
    }
}

TEST_CASE ("distinct pads each press")
{
    PadHoldRegistry r;
    for (std::uint8_t n : { 36, 37, 38 })
        CHECK (r.noteOn (0, n, 1.0f)->kind == Kind::press);
}

TEST_CASE ("releaseAll returns the held pads and empties the registry")
{
    PadHoldRegistry r;
    r.noteOn (0, 36, 1.0f); // pad 9
    r.noteOn (0, 37, 1.0f); // pad 10
    const auto released = r.releaseAll();
    CHECK (std::set<int> (released.begin(), released.end()) == std::set<int> { 9, 10 });
    CHECK_FALSE (r.noteOff (0, 36).has_value());
    CHECK (r.noteOn (0, 36, 1.0f)->kind == Kind::press);
}

TEST_CASE ("channels are isolated on a shared pad")
{
    PadHoldRegistry r;
    CHECK (r.noteOn (0, 36, 1.0f)->kind == Kind::press);
    CHECK (r.noteOn (1, 36, 1.0f)->kind == Kind::restrike);
    CHECK (r.noteOff (0, 36)->kind == Kind::sustain);
    CHECK (r.noteOff (1, 36)->kind == Kind::release);
}
