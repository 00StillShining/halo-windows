#include "Core/HonestState.h"

namespace halo::honest
{
    Flags flags (const Inputs& in)
    {
        Flags f;
        f.mock = in.source == Source::mock;
        // Disconnected means the stage shows the demo frames: that's PREVIEW too.
        f.preview = in.feed == Feed::preview || ! in.connected;
        f.waiting = in.connected && in.feed == Feed::waiting;
        f.live = in.connected && in.feed == Feed::live;
        return f;
    }

    std::vector<std::string> honestTags (const Inputs& in)
    {
        const auto f = flags (in);
        std::vector<std::string> tags;
        if (f.mock)
            tags.emplace_back ("MOCK");
        if (f.preview)
            tags.emplace_back ("PREVIEW");
        return tags;
    }

    WriteView writeView (const Inputs& in)
    {
        WriteView v;
        v.backup = in.latestBackup;
        if (in.source == Source::mock || ! in.connected)
        {
            v.state = WriteState::notAvailable;
            v.word = "n/a";
            v.reason = "connect the ep-40 to send. export works now."; // the same words for mock (no mock marking)
            return v;
        }
        if (! in.writesUnlocked)
        {
            v.state = WriteState::locked;
            v.word = "locked";
            v.reason = in.libraryReady ? "back up the ep-40 first (backups, ctrl 6)"
                                       : "waiting for the ep-40's library";
            return v;
        }
        if (in.busy)
        {
            v.state = WriteState::busy;
            v.word = "unlocked";
            v.reason = "busy: sending to the ep-40";
            return v;
        }
        v.state = WriteState::unlocked;
        v.word = "unlocked";
        v.available = true;
        return v;
    }

    Notice pickNotice (const std::string& blocker, const std::string& info, const std::string& plain)
    {
        if (! blocker.empty()) return { NoticeKind::blocker, blocker };
        if (! info.empty())    return { NoticeKind::info, info };
        if (! plain.empty())   return { NoticeKind::plain, plain };
        return {};
    }
}
