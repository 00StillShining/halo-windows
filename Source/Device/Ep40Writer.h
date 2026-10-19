#pragma once

#include "Core/Chop.h"
#include "Device/Ep40Library.h"

#include <juce_events/juce_events.h>

namespace halo
{
    // Guarded writes to the EP-40, following docs/protocol.md:
    //  - only to a synced device with a complete backup (the caller checks
    //    AppModel::writesUnlocked());
    //  - uploads only into EMPTY slots (never overwrites);
    //  - one step at a time, each waiting for the device's OK; stop at the
    //    first error; finish with FILE INIT; verify uploads by reading the
    //    slot's size back (STAT).
    class Ep40Writer
    {
    public:
        explicit Ep40Writer (Ep40Library&);

        struct Result
        {
            bool ok = false;
            int slot = 0;
            juce::String message;
        };

        // Upload one sound (any rate/channels; it's converted to s16 at the
        // device's native rate cap) into `slot`, or the first free USER slot
        // when slot is 0. Mono or stereo.
        void upload (const AudioData&, const juce::String& name, int slot, std::function<void (Result)> done);

        // Delete one sound (DELETE `06 <fid>`, then FILE INIT, then confirm it's gone).
        void deleteSound (int slot, std::function<void (Result)> done);

        // Read a pad's assigned sound straight from the device (META GET on the pad).
        void readPad (int project, int group, int gridIndex, std::function<void (int slot)> done);

        // Point a pad at a sound: META SET {"sym": slot} on the pad.
        void assignPad (int project, int group, int gridIndex, int slot, std::function<void (Result)> done);

        // Upload several sounds and assign each to a pad (CHOP's SEND TO PADS).
        struct PadJob
        {
            AudioData audio;
            juce::String name;
            int group = 0, gridIndex = 0;
        };
        void uploadToPads (std::vector<PadJob>, int project, std::function<void (const juce::String& progress)> onProgress,
                           std::function<void (Result)> done);

        bool isBusy() const noexcept { return busy; }

        // USER category on the EP-40 (the device's own range for user sounds).
        static constexpr int userFirstSlot = 900, userLastSlot = 999;

    private:
        void sendPages (std::shared_ptr<te::Bytes> pcm, int page, int pageSize, std::function<void (bool, juce::String)> done);
        void runPadJobs (std::shared_ptr<std::vector<PadJob>>, size_t index, int project,
                         std::function<void (const juce::String&)> onProgress, std::function<void (Result)> done);

        Ep40Library& lib;
        bool busy = false;
    };
}
