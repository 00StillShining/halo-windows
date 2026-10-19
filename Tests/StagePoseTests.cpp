#include <doctest/doctest.h>

#include "Core/StagePose.h"

using namespace halo;

TEST_CASE ("stage pose: empty means no pose")
{
    CHECK_FALSE (StagePose::parse ("").has_value());
    CHECK_FALSE (StagePose::parse ("   ").has_value());
}

TEST_CASE ("stage pose: rest has no pads, a steady ring and a live demo LCD")
{
    const auto pose = StagePose::parse ("rest");
    REQUIRE (pose.has_value());
    CHECK (pose->errors.empty());
    CHECK_FALSE (pose->anyPadDown());
    CHECK (pose->ring.kind == RingState::Kind::connected);
    const auto d = pose->display();
    CHECK (d.feedMode == FeedMode::live);
    CHECK (d.value == 120);
    CHECK (d.activeGroup == 1);
    CHECK (d.isPlaying);
    CHECK_FALSE (d.activePadIndex.has_value());
}

TEST_CASE ("stage pose: the plan's example parses fully")
{
    const auto pose = StagePose::parse ("pad7=1.0,pad5=0.8,group=1,lcd=demo,ring=steady,feed=live");
    REQUIRE (pose.has_value());
    CHECK (pose->errors.empty());
    CHECK (pose->pads[0] == doctest::Approx (1.0f)); // grid 0 is "7"
    CHECK (pose->pads[4] == doctest::Approx (0.8f)); // grid 4 is "5"
    CHECK (pose->group == 1);
    CHECK (pose->display().activePadIndex == 0);
}

TEST_CASE ("stage pose: labels, groups, rings and feeds")
{
    const auto pose = StagePose::parse ("pad_dot, padENTER=0.5, pad0, group=d, ring=discover, feed=preview, lcd=idle");
    REQUIRE (pose.has_value());
    CHECK (pose->errors.empty());
    CHECK (pose->pads[9] == doctest::Approx (1.0f));
    CHECK (pose->pads[11] == doctest::Approx (0.5f));
    CHECK (pose->pads[10] == doctest::Approx (1.0f));
    CHECK (pose->group == 3);
    CHECK (pose->ring.kind == RingState::Kind::discovering);
    CHECK (pose->display().feedMode == FeedMode::preview);
    CHECK (pose->display().value == 0);

    CHECK (StagePose::parse ("ring=off")->ring.kind == RingState::Kind::disconnected);
    CHECK (StagePose::parse ("ring=transfer")->ring.transferProgress == doctest::Approx (0.6));
    CHECK (StagePose::parse ("ring=error")->ring.isError());
}

TEST_CASE ("stage pose: bad tokens are reported, not fatal")
{
    const auto pose = StagePose::parse ("pad7=loud,pad12,group=9,ring=party,wobble,pad8=2");
    REQUIRE (pose.has_value());
    CHECK (pose->errors.size() == 5);
    CHECK (pose->pads[1] == doctest::Approx (1.0f)); // velocity clamps to 1
}
