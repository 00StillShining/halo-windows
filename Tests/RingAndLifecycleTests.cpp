// Ported from HaloTests/HaloRingStateTests.swift and HaloLifecycleTests.swift.
#include <doctest/doctest.h>

#include "Core/RingState.h"

using namespace halo;
using RK = RingState::Kind;
using LK = LifecyclePhase::Kind;

TEST_CASE ("ring: derivation follows the priority order")
{
    CHECK (RingState::derive ({}).kind == RK::disconnected);
    CHECK (RingState::derive ({ .observerRunning = true }).kind == RK::discovering);
    CHECK (RingState::derive ({ .observerRunning = true, .endpointConnected = true }).kind == RK::connected);
    CHECK (RingState::derive ({ .observerRunning = true, .endpointConnected = true, .monitorEngaged = true }).kind == RK::monitoring);
    CHECK (RingState::derive ({ .monitorEngaged = true, .recordingStartedAt = 1.0 }).kind == RK::recording);
    CHECK (RingState::derive ({ .recordingStartedAt = 1.0, .transferProgress = 0.5 }).kind == RK::transfer);
    CHECK (RingState::derive ({ .errorLabel = "MIDI", .transferProgress = 0.5 }).kind == RK::error);
}

TEST_CASE ("ring: transfer progress is clamped and shown as a percentage")
{
    CHECK (RingState::derive ({ .transferProgress = 1.7 }).transferProgress == 1.0);
    CHECK (RingState::derive ({ .transferProgress = -0.2 }).transferProgress == 0.0);
    CHECK (RingState::derive ({ .transferProgress = 0.426 }).statusWord() == "TX 43%");
}

TEST_CASE ("ring: status words")
{
    CHECK (RingState::derive ({}).statusWord() == "DARK");
    CHECK (RingState::derive ({ .observerRunning = true }).statusWord() == "SCAN");
    CHECK (RingState::derive ({ .endpointConnected = true }).statusWord() == "LINK");
    CHECK (RingState::derive ({ .monitorEngaged = true }).statusWord() == "MON");
    CHECK (RingState::derive ({ .recordingStartedAt = 0.0 }).statusWord() == "REC");

    const auto error = RingState::derive ({ .errorLabel = "MIDI" });
    CHECK (error.statusWord() == "ERR MIDI");
    CHECK (error.isError());
}

TEST_CASE ("lifecycle: derivation and words")
{
    CHECK (LifecyclePhase::derive ({}).word() == "STARTING");
    CHECK (LifecyclePhase::derive ({ .observerRunning = true }).word() == "WAIT");
    CHECK (LifecyclePhase::derive ({ .observerRunning = true, .endpointConnected = true }).word() == "READY");
    CHECK (LifecyclePhase::derive ({ .endpointConnected = true, .displayLive = true }).kind == LK::live);
    CHECK (LifecyclePhase::derive ({ .monitorEngaged = true }).kind == LK::live);
    CHECK (LifecyclePhase::derive ({ .displayLive = true, .systemAsleep = true }).word() == "SLEEP");
    CHECK (LifecyclePhase::derive ({ .systemAsleep = true, .errorLabel = "MIDI" }).word() == "ERR MIDI");
}
