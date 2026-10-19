#include "Core/HonestState.h"

#include <doctest/doctest.h>

using namespace halo::honest;

namespace
{
    Inputs liveDevice()
    {
        Inputs in;
        in.source = Source::device;
        in.feed = Feed::live;
        in.connected = true;
        in.libraryReady = true;
        return in;
    }
}

TEST_CASE ("honest: mock is flagged whatever else is true")
{
    for (auto feed : { Feed::live, Feed::waiting, Feed::preview })
        for (bool connected : { false, true })
        {
            Inputs in;
            in.source = Source::mock;
            in.feed = feed;
            in.connected = connected;
            CHECK (flags (in).mock);
            const auto tags = honestTags (in);
            REQUIRE (! tags.empty());
            CHECK (tags.front() == "MOCK");
        }
}

TEST_CASE ("honest: preview when the feed is preview or the device is gone")
{
    auto in = liveDevice();
    CHECK_FALSE (flags (in).preview);
    CHECK (flags (in).live);

    in.feed = Feed::preview;
    CHECK (flags (in).preview);

    in = liveDevice();
    in.connected = false;
    CHECK (flags (in).preview);
    CHECK_FALSE (flags (in).live);

    in = liveDevice();
    in.feed = Feed::waiting;
    CHECK (flags (in).waiting);
    CHECK_FALSE (flags (in).preview);

    Inputs both;
    both.source = Source::mock;
    both.feed = Feed::preview;
    CHECK (honestTags (both) == std::vector<std::string> { "MOCK", "PREVIEW" });
}

TEST_CASE ("honest: writesUnlocked and the backup pass through unchanged")
{
    auto in = liveDevice();
    in.latestBackup = BackupInfo { "18 oct 2026 21:04", 477, 9, true };

    in.writesUnlocked = true;
    auto v = writeView (in);
    CHECK (v.available);
    CHECK (v.state == WriteState::unlocked);
    CHECK (v.word == "unlocked");
    CHECK (v.reason.empty());
    CHECK (v.backup == in.latestBackup);

    // HonestState never re-derives the gate: a backup alone doesn't unlock.
    in.writesUnlocked = false;
    v = writeView (in);
    CHECK_FALSE (v.available);
    CHECK (v.state == WriteState::locked);
    CHECK (v.word == "locked");
    CHECK (v.reason == "back up the ep-40 first (backups, ctrl 6)");
    CHECK (v.backup == in.latestBackup);

    in.writesUnlocked = true;
    in.busy = true;
    v = writeView (in);
    CHECK_FALSE (v.available);
    CHECK (v.state == WriteState::busy);
    CHECK (v.reason == "busy: sending to the ep-40");

    in = liveDevice();
    in.connected = false;
    in.writesUnlocked = true; // stale: a disconnected device is never writable
    v = writeView (in);
    CHECK_FALSE (v.available);
    CHECK (v.word == "n/a");

    Inputs mock;
    mock.source = Source::mock;
    mock.connected = true;
    mock.writesUnlocked = true;
    CHECK_FALSE (writeView (mock).available);
    CHECK (writeView (mock).word == "n/a");
}

TEST_CASE ("honest: pickNotice is blocker > info > plain, verbatim")
{
    // Today's EDIT copy (EditPanel.cpp).
    const std::string editBlocker = "Needs 20.2 MB but the EP-40 has 14.9 MB free.";
    const std::string editTrim = "AUTO-TRIM: over 20 s, so SEND sends the first 20 s of the selection, with a fade-out. "
                                 "Drag the trim handles to pick another part, or CHOP it.";
    const std::string editUnlocked = "SEND TO EP-40 uploads into the first empty USER slot (900-999). It never overwrites a sound.";
    const std::string editLocked = "Back up the EP-40 in BACKUPS (Ctrl+6) to unlock SEND. Until then, EXPORT works.";
    const std::string editDisconnected = "Connect the EP-40 to send directly. Until then, EXPORT works.";

    CHECK (pickNotice (editBlocker, editTrim, editUnlocked) == Notice { NoticeKind::blocker, editBlocker });
    CHECK (pickNotice ({}, editTrim, editUnlocked) == Notice { NoticeKind::info, editTrim });
    CHECK (pickNotice ({}, {}, editUnlocked) == Notice { NoticeKind::plain, editUnlocked });
    CHECK (pickNotice ({}, {}, editLocked).kind == NoticeKind::plain);
    CHECK (pickNotice ({}, {}, editDisconnected).text == editDisconnected);

    // Today's CHOP copy (ChopSurface.cpp).
    const std::string chopTooLong = "16 slices are over 20 s (longest 31.5 s), the EP-40's limit per sound. "
                                    "Turn on AUTO-TRIM, use more slices, or CLOSE and trim in EDIT.";
    const std::string chopTooBig = "These slices need 20.2 MB but the EP-40 has 14.9 MB free. Turn on AUTO-TRIM, use fewer slices, or pick a LO quality.";
    const std::string chopFull = "The EP-40 is nearly full: AUTO-TRIM would cut the slices to 180 ms. Delete sounds in LOAD, use fewer slices, or pick a LO quality.";
    const std::string chopLength = "LENGTH: 7 slices cut to 4.0 s (your LENGTH), from each slice's start, with a fade-out. The shaded tails are dropped.";
    const std::string chopAutoTrim = "AUTO-TRIM: 3 slices cut to 6.5 s to fit the EP-40's free memory, from each slice's start, with a fade-out. The shaded tails are dropped.";
    const std::string chopUnlocked = "SEND TO PADS uploads the slices into empty USER slots and assigns them to these pads in project 3 (the project selected in LOAD).";
    const std::string chopLocked = "Back up the EP-40 in BACKUPS (Ctrl+6) to unlock SEND TO PADS. EXPORT works now.";
    const std::string chopDisconnected = "Connect the EP-40 to send slices to its pads. EXPORT works now.";

    for (const auto& blocker : { chopTooLong, chopTooBig, chopFull })
        for (const auto& info : { std::string(), chopLength, chopAutoTrim })
            for (const auto& plain : { chopUnlocked, chopLocked, chopDisconnected })
                CHECK (pickNotice (blocker, info, plain) == Notice { NoticeKind::blocker, blocker });

    for (const auto& info : { chopLength, chopAutoTrim })
        for (const auto& plain : { chopUnlocked, chopLocked, chopDisconnected })
            CHECK (pickNotice ({}, info, plain) == Notice { NoticeKind::info, info });

    for (const auto& plain : { chopUnlocked, chopLocked, chopDisconnected })
        CHECK (pickNotice ({}, {}, plain) == Notice { NoticeKind::plain, plain });

    CHECK (pickNotice ({}, {}, {}).kind == NoticeKind::none);
}
