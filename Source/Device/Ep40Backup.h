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
    class Ep40Backup : public juce::ChangeBroadcaster
    {
    public:
        explicit Ep40Backup (Ep40Library&);

        struct Entry
        {
            juce::File folder;
            juce::String serial, os, created;
            int sounds = 0, projects = 0;
            bool complete = false;
        };

        static juce::File backupsFolder();
        static std::vector<Entry> list();
        static bool hasCompleteBackup (const juce::String& serial);

        void start();
        void cancel();
        bool isRunning() const noexcept { return running; }
        double progress() const noexcept { return fraction; }
        const juce::String& status() const noexcept { return statusLine; }

    private:
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
        juce::StringArray problems;
        double startedAt = 0.0;
        int generation = 0;
    };
}
