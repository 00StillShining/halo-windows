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
        // The mono mixdown the waveforms draw from (shared with WaveCache's builder).
        std::shared_ptr<const std::vector<float>> monoSamples() const noexcept { return monoShared; }

        // ---- EDIT ---------------------------------------------------------
        SamplePrep prep;
        Treatment treatment = Treatment::original;
        void changed() { sendChangeMessage(); }

        SizeEstimate estimate() const;
        void audition();
        void exportEdit();
        // Renders what EXPORT would write (prep + treatment), off the UI thread.
        void renderEdit (std::function<void (std::shared_ptr<const AudioData>)>);
        // SEND TO EP-40 auto-trims too: a selection over 20 s sends its first
        // 20 s (with a fade-out). sendPrep() is what's sent; EXPORT is untouched.
        SamplePrep sendPrep() const;
        bool sendIsTrimmed() const;
        SizeEstimate sendEstimate() const;
        void renderSend (std::function<void (std::shared_ptr<const AudioData>)>);
        // Renders every slice placed on a pad by the plan, in pad order.
        void renderPlacedSlices (std::function<void (std::shared_ptr<std::vector<AudioData>>)>);

        // ---- CHOP ---------------------------------------------------------
        bool chopOpen = false;
        chop::OnsetParameters onsetParameters;

        // How many slices: FIT = the pads left in the group from the start pad
        // (the default, so a chop fits without overflow), a fixed count, or ALL
        // detected cuts. EVEN splits into equal parts instead of transients.
        static constexpr int sliceFit = 0, sliceAll = -1;
        int sliceChoice = sliceFit;
        bool evenSlices = false;
        void setSliceChoice (int choice) { sliceChoice = choice; applySliceChoice(); }
        void setEvenSlices (bool even) { evenSlices = even; applySliceChoice(); }
        void setStartPad (int pad);
        int targetSliceCount() const; // 0 = no limit (ALL transients)
        chop::SliceSet slices;
        std::optional<int> selectedSlice;
        int group = 0, startPad = 0;
        juce::String baseName;

        // AUTO-TRIM (on by default): every slice longer than the EP-40 takes is
        // cut short from its start, with a fade-out, instead of blocking SEND.
        // The cap also shrinks so the placed slices fit spaceBudgetBytes (the
        // EP-40's free memory, when known; -1 = unknown).
        bool autoTrim = true;
        long long spaceBudgetBytes = -1;
        void setAutoTrim (bool on) { autoTrim = on; sendChangeMessage(); }
        // LENGTH: how long each chop may be, 1-20 s. Each slice still starts at
        // its cut and plays for up to this long (shorter chops = one-shots).
        // Under 20 s it applies even with AUTO-TRIM off: it's your choice.
        static constexpr double minChopSeconds = 1.0;
        double chopSeconds = maxSoundSeconds;
        void setChopSeconds (double s) { chopSeconds = juce::jlimit (minChopSeconds, maxSoundSeconds, s); sendChangeMessage(); }
        bool lengthIsLimit() const noexcept { return chopSeconds < maxSoundSeconds; }
        int sliceCapFrames() const;      // 0 = no cap
        double sliceCapSeconds() const;  // 0 = no cap
        bool capLimitedBySpace() const;  // the cap is under LENGTH because of free memory
        chop::Slice playedPart (const chop::Slice&) const; // the part of a slice that's kept

        void openChop();
        void closeChop();
        void detect();
        int minGap() const;
        chop::PadPlan plan() const;
        int detectedCuts() const noexcept { return (int) detected.size(); } // every onset found, before SLICES
        void auditionSlice (int index);
        void exportSlices();

        // ---- Shared -------------------------------------------------------
        AuditionPlayer& player() noexcept { return audition_; }
        // Plays audio that isn't the open sample (e.g. a sound from the EP-40).
        void playOther (std::shared_ptr<const AudioData> data, int token) { play (std::move (data), token); }
        // Short status line ("Exported 8 slices to ...") for the UI.
        // Each page owns its status line (plan 3.8): CHOP's never shows in EDIT.
        enum class Page { edit, chop };
        const juce::String& status() const noexcept { return statusLine; }
        juce::String status (Page p) const { return p == statusPage ? statusLine : juce::String(); }
        void report (const juce::String& line, Page p = Page::chop) { setStatus (line, p); }
        const juce::File& lastExport() const noexcept { return exported; }

    private:
        void applySliceChoice();
        double capSeconds (bool& bySpace) const;
        SamplePrep slicePrep (const chop::Slice&) const;
        void setStatus (const juce::String&, Page = Page::chop);
        void play (std::shared_ptr<const AudioData>, int token);
        template <typename Work, typename Done>
        void runInBackground (Work work, Done done);

        MonitorEngine& engine;
        AuditionPlayer audition_;
        Library::Item current;
        std::shared_ptr<const AudioData> source;
        std::vector<float> mono;
        std::shared_ptr<const std::vector<float>> monoShared; // the same samples, for WaveCache
        std::vector<chop::Onset> detected; // every onset found, before the SLICES choice
        WaveformSummary wave;
        bool loading = false;
        juce::String lastError, statusLine;
        Page statusPage = Page::chop;
        juce::File exported;
        int generation = 0;
        std::shared_ptr<bool> alive = std::make_shared<bool> (true);
    };
}
