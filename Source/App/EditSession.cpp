#include "App/EditSession.h"
#include "Audio/MonitorEngine.h"
#include "Audio/SampleIO.h"
#include "Core/Ep40MidiMapping.h"

namespace halo
{
    namespace
    {
        juce::String safeName (const juce::String& s)
        {
            auto out = s.retainCharacters ("ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789 ._-").trim();
            return out.isEmpty() ? juce::String ("sample") : out;
        }

        juce::String treatmentTag (Treatment t)
        {
            return juce::String (label (t)).replace (" ", "").toLowerCase();
        }
    }

    EditSession::EditSession (MonitorEngine& e) : engine (e)
    {
        audition_.onStopped = [this] { sendChangeMessage(); };
    }

    EditSession::~EditSession()
    {
        *alive = false;
    }

    template <typename Work, typename Done>
    void EditSession::runInBackground (Work work, Done done)
    {
        juce::Thread::launch ([work = std::move (work), done = std::move (done), alive = alive]() mutable
        {
            auto result = work();
            juce::MessageManager::callAsync ([done = std::move (done), result = std::move (result), alive]() mutable
            {
                if (*alive)
                    done (std::move (result));
            });
        });
    }

    void EditSession::setStatus (const juce::String& s)
    {
        statusLine = s;
        sendChangeMessage();
    }

    //==============================================================================
    void EditSession::open (const Library::Item& item, bool thenChop)
    {
        audition_.stop();
        current = item;
        source.reset();
        mono.clear();
        wave = {};
        prep = {};
        treatment = Treatment::original;
        slices = {};
        selectedSlice.reset();
        chopOpen = false;
        lastError.clear();
        statusLine.clear();
        loading = true;
        const int gen = ++generation;
        sendChangeMessage();

        runInBackground ([file = item.file]
        {
            juce::String error;
            auto data = SampleIO::decode (file, error);
            return std::make_pair (std::move (data), error);
        },
        [this, gen, thenChop] (std::pair<std::optional<AudioData>, juce::String> result)
        {
            if (gen != generation)
                return; // superseded by a newer open
            loading = false;
            if (! result.first)
            {
                lastError = result.second;
                sendChangeMessage();
                return;
            }
            source = std::make_shared<const AudioData> (std::move (*result.first));
            mono = source->monoMixdown();
            wave = WaveformSummary::build (mono, 1024);
            baseName = current.title.toUpperCase().retainCharacters ("ABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789").substring (0, 8);
            if (baseName.isEmpty())
                baseName = "CHOP";
            sendChangeMessage();
            if (thenChop)
                openChop();
        });
    }

    void EditSession::close()
    {
        audition_.stop();
        ++generation;
        source.reset();
        current = {};
        chopOpen = false;
        sendChangeMessage();
    }

    SizeEstimate EditSession::estimate() const
    {
        if (source == nullptr)
            return {};
        return estimateSize (source->frameCount(), source->channelCount(), source->sampleRate, prep, treatment);
    }

    void EditSession::play (std::shared_ptr<const AudioData> data, int token)
    {
        const auto error = audition_.play (std::move (data), engine.chosenOutputName(), token);
        if (error.isNotEmpty())
            setStatus ("Audition: " + error);
        else
            sendChangeMessage();
    }

    void EditSession::audition()
    {
        if (source == nullptr)
            return;
        if (audition_.isPlaying() && audition_.token() == -1)
        {
            audition_.stop();
            return;
        }
        // Render exactly what would be exported, so you hear the treatment too.
        runInBackground ([src = source, p = prep, t = treatment] { return std::make_shared<const AudioData> (render (*src, p, t)); },
                         [this] (std::shared_ptr<const AudioData> data) { play (std::move (data), -1); });
    }

    void EditSession::exportEdit()
    {
        if (source == nullptr)
            return;
        const auto folder = SampleIO::exportFolder();
        folder.createDirectory();
        const auto file = folder.getNonexistentChildFile (safeName (current.title) + "-" + treatmentTag (treatment), ".wav");
        setStatus ("Exporting...");
        runInBackground ([src = source, p = prep, t = treatment, file]
        {
            return SampleIO::writeWav16 (render (*src, p, t), file);
        },
        [this, file] (bool ok)
        {
            if (ok)
                exported = file;
            setStatus (ok ? "Exported " + file.getFileName() : "Export failed: couldn't write " + file.getFullPathName());
        });
    }

    //==============================================================================
    void EditSession::openChop()
    {
        if (source == nullptr)
            return;
        chopOpen = true;
        selectedSlice.reset();
        detect();
    }

    void EditSession::closeChop()
    {
        audition_.stop();
        chopOpen = false;
        sendChangeMessage();
    }

    int EditSession::minGap() const
    {
        const double rate = source != nullptr ? source->sampleRate : 0.0;
        return std::max ({ wave.framesPerBucket, (int) (onsetParameters.minSliceMilliseconds / 1000.0 * rate), 1 });
    }

    void EditSession::detect()
    {
        if (source == nullptr)
            return;
        const int gen = ++generation;
        setStatus ("Detecting slices...");
        runInBackground ([m = mono, rate = source->sampleRate, p = onsetParameters]
        {
            return chop::detectOnsetsWithStrength (m, rate, p);
        },
        [this, gen] (std::vector<chop::Onset> onsets)
        {
            if (gen != generation || source == nullptr)
                return;
            detected = std::move (onsets);
            applySliceChoice();
        });
    }

    int EditSession::targetSliceCount() const
    {
        if (sliceChoice == sliceFit)
            return std::max (1, ep40::padsPerGroup - startPad);
        if (sliceChoice == sliceAll)
            return evenSlices ? 16 : 0;
        return sliceChoice;
    }

    void EditSession::setStartPad (int pad)
    {
        startPad = juce::jlimit (0, ep40::padsPerGroup - 1, pad);
        if (sliceChoice == sliceFit)
            applySliceChoice(); // FIT follows the pads that are left
        else
            sendChangeMessage();
    }

    void EditSession::applySliceChoice()
    {
        if (source == nullptr)
            return;
        const int n = source->frameCount();
        const int target = targetSliceCount();

        std::vector<int> cuts;
        if (evenSlices)
            cuts = chop::evenCuts (n, target);
        else if (target == 0)
            for (const auto& o : detected)
                cuts.push_back (o.frame);
        else
            cuts = chop::strongestCuts (detected, target - 1, minGap());

        slices = chop::SliceSet (n, cuts);
        selectedSlice.reset();
        juce::String line = juce::String (slices.sliceCount()) + " slices";
        if (! evenSlices && target > 0 && (int) detected.size() + 1 > target)
            line << " (the " << (target - 1) << " strongest of " << (int) detected.size() << " cuts found)";
        setStatus (line);
    }

    chop::PadPlan EditSession::plan() const
    {
        if (source == nullptr)
            return {};
        return chop::planPads (slices.slices(), prep.outputChannelCount (source->channelCount()), treatment,
                               source->sampleRate, group, startPad, baseName.toStdString());
    }

    void EditSession::auditionSlice (int index)
    {
        const auto all = slices.slices();
        if (source == nullptr || index < 0 || index >= (int) all.size())
            return;
        selectedSlice = index;
        auto p = chop::microFadePrep (source->sampleRate);
        p.trimStartFrame = all[(size_t) index].startFrame;
        p.trimEndFrame = all[(size_t) index].endFrame;
        p.channelMode = prep.channelMode;
        runInBackground ([src = source, p, t = treatment] { return std::make_shared<const AudioData> (render (*src, p, t)); },
                         [this, index] (std::shared_ptr<const AudioData> data) { play (std::move (data), index); });
    }

    void EditSession::exportSlices()
    {
        if (source == nullptr || slices.sliceCount() == 0)
            return;
        const auto padPlan = plan();
        const auto all = slices.slices();
        const auto folder = SampleIO::exportFolder().getNonexistentChildFile (safeName (baseName), {});
        setStatus ("Exporting slices...");

        runInBackground ([src = source, padPlan, all, folder, mode = prep.channelMode, t = treatment]
        {
            if (! folder.createDirectory())
                return juce::String ("Export failed: couldn't create " + folder.getFullPathName());

            // Every slice is exported (not just the ones that fit on pads),
            // plus a pads.txt that says where each one is meant to go.
            juce::String guide;
            guide << "Halo CHOP export\n"
                  << "Load these into the EP-40 with Teenage Engineering's EP Sample Tool.\n\n";
            for (size_t i = 0; i < all.size(); ++i)
            {
                auto p = chop::microFadePrep (src->sampleRate);
                p.trimStartFrame = all[i].startFrame;
                p.trimEndFrame = all[i].endFrame;
                p.channelMode = mode;
                const auto name = i < padPlan.writes.size() ? juce::String (padPlan.writes[i].name)
                                                            : juce::String::formatted ("SLICE.%02d", (int) i + 1);
                const auto file = folder.getChildFile (name + ".wav");
                if (! SampleIO::writeWav16 (render (*src, p, t), file))
                    return "Export failed: couldn't write " + file.getFileName();

                guide << file.getFileName();
                if (i < padPlan.writes.size())
                    guide << "  ->  group " << ep40::groupLabels[(size_t) padPlan.writes[i].group].data()
                          << ", pad " << ep40::gridLabels[(size_t) padPlan.writes[i].gridIndex].data();
                else
                    guide << "  ->  (no pad left in the group)";
                guide << "\n";
            }
            folder.getChildFile ("pads.txt").replaceWithText (guide);
            return juce::String();
        },
        [this, folder, count = (int) all.size()] (juce::String error)
        {
            if (error.isEmpty())
                exported = folder;
            setStatus (error.isNotEmpty() ? error : "Exported " + juce::String (count) + " slices to " + folder.getFileName());
        });
    }
}
