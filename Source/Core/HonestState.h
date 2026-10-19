#pragma once

#include <optional>
#include <string>
#include <vector>

namespace halo::honest
{
    // The honest states the UI must always show (docs/redesign/plan.md section
    // 0.3, H1-H7), derived in one place from plain inputs so they can be tested
    // without JUCE. HonestState never re-derives writesUnlocked: AppModel owns
    // that formula and passes the answer in.

    enum class Source { device, mock };
    enum class Feed { live, waiting, preview };

    struct BackupInfo
    {
        std::string when;   // "18 oct 2026 21:04"
        int sounds = 0, projects = 0;
        bool complete = false;
        bool operator== (const BackupInfo&) const = default;
    };

    struct Inputs
    {
        Source source = Source::device;
        Feed feed = Feed::preview;
        bool connected = false;
        bool portBusy = false;
        bool libraryReady = false;
        bool writesUnlocked = false;            // AppModel::writesUnlocked(), passed through
        std::optional<BackupInfo> latestBackup; // AppModel::latestBackup(), passed through
        bool busy = false;                      // a device write is running
    };

    struct Flags
    {
        bool mock = false;     // MOCK tag, caution band
        bool preview = false;  // PREVIEW tag, violet frame (demo frames, or no device)
        bool waiting = false;  // WAIT: blinking hollow lamps
        bool live = false;
    };

    Flags flags (const Inputs&);

    // The strip's honest slot, left to right ("MOCK", "PREVIEW"). Its width is
    // reserved for both, whatever is showing.
    std::vector<std::string> honestTags (const Inputs&);

    // What a device-write key and the status strip's padlock say.
    enum class WriteState { unlocked, locked, notAvailable, busy };
    struct WriteView
    {
        WriteState state = WriteState::notAvailable;
        bool available = false;     // the confirm may be offered
        std::string word;           // strip: "unlocked" / "locked" / "n/a"
        std::string reason;         // printed beside a dead write key; empty when available
        std::optional<BackupInfo> backup; // passed through unchanged
    };
    WriteView writeView (const Inputs&);

    // One notice per place, by priority: blocker > info > plain. Empty strings
    // are "no such notice".
    enum class NoticeKind { none, blocker, info, plain };
    struct Notice
    {
        NoticeKind kind = NoticeKind::none;
        std::string text;
        bool operator== (const Notice&) const = default;
    };
    Notice pickNotice (const std::string& blocker, const std::string& info, const std::string& plain);
}
