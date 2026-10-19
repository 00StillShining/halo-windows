#pragma once

#include "App/Library.h"
#include "Audio/AuditionPlayer.h"
#include "Core/Chop.h"

#include <juce_events/juce_events.h>

#include <memory>

namespace halo
{
    class MonitorEngine;

    // The sample being edited or chopped (port of EditSession.swift and
    // ChopSession.swift). Decoding, detection, rendering and export run on
    // background threads; results land on the message thread with a change
    // message. Nothing here touches the EP-40: device writes need the
    // protocol (Phase 0B), so they are reported as unavailable, never faked.
    class EditSession : public juce::ChangeBroadcaster
    {
    public:
        explicit EditSession (MonitorEngine&);
        ~EditSession() override;

        // ---- Source -------------------------------------------------------
        // thenChop opens the CHOP surface as soon as decoding finishes.
        void open (const Library::Item&, bool thenChop = false);
        void close();
        bool hasSource() const noexcept { return source != nullptr; }
        bool isLoading() const noexcept { return loading; }
        const juce::String& error() const noexcept { return lastError; }
        const Library::Item& item() const noexcept { return current; }
        std::shared_ptr<const AudioData> audio() const noexcept { return source; }
        const WaveformSummary& summary() const noexcept { return wave; }

        // ---- EDIT ---------------------------------------------------------
        SamplePrep prep;
        Treatment treatment = Treatment::original;
        void changed() { sendChangeMessage(); }

        SizeEstimate estimate() const;
        void audition();
        void exportEdit();

        // ---- CHOP ---------------------------------------------------------
        bool chopOpen = false;
        chop::OnsetParameters onsetParameters;
        chop::SliceSet slices;
        std::optional<int> selectedSlice;
        int group = 0, startPad = 0;
        juce::String baseName;

        void openChop();
        void closeChop();
        void detect();
        int minGap() const;
        chop::PadPlan plan() const;
        void auditionSlice (int index);
        void exportSlices();

        // ---- Shared -------------------------------------------------------
        AuditionPlayer& player() noexcept { return audition_; }
        // Short status line ("Exported 8 slices to ...") for the UI.
        const juce::String& status() const noexcept { return statusLine; }
        const juce::File& lastExport() const noexcept { return exported; }

    private:
        void setStatus (const juce::String&);
        void play (std::shared_ptr<const AudioData>, int token);
        template <typename Work, typename Done>
        void runInBackground (Work work, Done done);

        MonitorEngine& engine;
        AuditionPlayer audition_;
        Library::Item current;
        std::shared_ptr<const AudioData> source;
        std::vector<float> mono;
        WaveformSummary wave;
        bool loading = false;
        juce::String lastError, statusLine;
        juce::File exported;
        int generation = 0;
        std::shared_ptr<bool> alive = std::make_shared<bool> (true);
    };
}
