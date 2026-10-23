#pragma once

#include "Device/Ep40Library.h"

#include <juce_events/juce_events.h>

namespace halo
{
    // Full, read-only backup of an EP-40 (port of BackupStore.swift, whose
    // device side was a disabled seam on the Mac). Writes, under
    // Documents\Halo\Backups\EP-40 <serial> <date>\:
    //   projects\P01.tar ... P09.tar   (the device's own project archives)
    //   sounds\NNN <name>.wav          (bit-exact 16-bit, from the raw PCM)
    //   sounds\NNN.json                (the sound's full metadata)
    //   backup.json                    (written LAST, with "complete": true)
    // Halo only allows writes to a device that has a complete backup.
    //
    // The backup folder is scanned off the message thread and cached
    // (docs/redesign/plan.md section 5.1): latestComplete() and entries() never
    // touch the disk, so the status strip, write keys and paint code can ask as
    // often as they like. The cache is refreshed by start(), finish(), a 10 s
    // background rescan, window activation (rescanAsync) and forceRescan() (the
    // write confirm's last check). A change sends a change message.
    class Ep40Backup : public juce::ChangeBroadcaster, private juce::Timer
    {
    public:
        explicit Ep40Backup (Ep40Library&);
        ~Ep40Backup() override;

        struct Entry
        {
            juce::File folder;
            juce::String serial, os, created;
            int sounds = 0, projects = 0;
            bool complete = false;

            juce::Time createdTime() const;
            bool operator== (const Entry&) const = default;
        };

        static juce::File backupsFolder();
        // Scans the disk. Never call it from paint or a poll; use the cache.
        static std::vector<Entry> list();

        // ---- Cached view (message thread, no disk access) --------------------
        const std::vector<Entry>& entries() const noexcept { return cache; }
        std::optional<Entry> latestComplete (const juce::String& serial) const;
        bool hasCompleteBackup (const juce::String& serial) const { return latestComplete (serial).has_value(); }
        void rescanAsync();
        void forceRescan();
        // "backup 18 oct 2026 21:04 Â· complete Â· 477 sounds"
        static juce::String describe (const Entry&);
        // Debug stats: how many scans ran on the message thread.
        int messageThreadScans() const noexcept { return syncScans; }

        void start();
        void cancel();
        bool isRunning() const noexcept { return running; }
        double progress() const noexcept { return fraction; }
        const juce::String& status() const noexcept { return statusLine; }
        // The run as items (9 projects, then every sound), for BACKUPS' dot
        // matrix: how many are behind us, which one is in flight, how many in all.
        int itemsDone() const noexcept { return currentItem; }
        int itemsTotal() const noexcept { return 9 + itemCount; }
        // Seconds left by bytes so far; negative until the first sound lands.
        double secondsLeft() const;

    private:
        void timerCallback() override { rescanAsync(); }
        void adopt (std::vector<Entry>);
        void nextProject (int project);
        void nextSound (size_t index);
        void finish (bool ok, const juce::String& message);
        void setStatus (const juce::String&);

        Ep40Library& lib;
        juce::File folder;
        bool running = false, cancelled = false;
        double fraction = 0.0;
        juce::String statusLine;
        juce::int64 totalBytes = 0, doneBytes = 0;
        int projectsSaved = 0, soundsSaved = 0;
        int currentItem = 0, itemCount = 0;
        juce::StringArray problems;
        double startedAt = 0.0;
        int generation = 0;

        std::vector<Entry> cache;
        bool scanPending = false, rescanAgain = false;
        int syncScans = 0;
        std::shared_ptr<bool> alive = std::make_shared<bool> (true);
    };
}
