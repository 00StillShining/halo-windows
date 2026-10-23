#pragma once

#include "App/Settings.h"

#include <juce_audio_formats/juce_audio_formats.h>

#include <map>
#include <set>

namespace halo
{
    // Local samples halo knows about: takes and grabs in Music\Halo\Recordings,
    // plus files you've imported (referenced in place, never copied or moved).
    // Port of TakesStore + the Mac's local library, without any device data.
    class Library : public juce::ChangeBroadcaster
    {
    public:
        struct Item
        {
            enum class Kind { take, grab, imported };
            juce::File file;
            Kind kind = Kind::imported;
            juce::String title;
            double seconds = 0.0;
            double sampleRate = 0.0;
            int channels = 0;
            juce::Time modified;
        };

        explicit Library (Settings&);
        ~Library() override;

        const std::vector<Item>& items() const noexcept { return all; }
        void refresh();

        // Adds supported files (others are skipped). Returns how many were added.
        int importFiles (const juce::StringArray& paths);
        // Forget an imported file (the file itself is left alone).
        void forget (const juce::File&);

        static juce::String kindLabel (Item::Kind);

        // Lazy thumbnails (CAPTURE): `peakCount` peaks per file, read on a
        // background thread with the format reader's own max levels. Returns
        // nullptr until ready; a change message follows when it is.
        static constexpr int peakCount = 30;
        const std::vector<float>* peaks (const juce::File&);

        // CAPTURE's view: indices into items(), filtered and sorted.
        enum class Filter { all, takes, grabs, files };
        enum class Sort { date, name, length };
        std::vector<int> view (Filter, Sort, const juce::String& search) const;

    private:
        void saveImported();

        Settings& settings;
        juce::StringArray imported;
        std::vector<Item> all;
        std::map<juce::String, std::vector<float>> peakCache;
        std::set<juce::String> peakPending;
        juce::ThreadPool peakPool { 1 };
        std::shared_ptr<bool> alive = std::make_shared<bool> (true);
    };
}
