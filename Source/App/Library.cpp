#include "App/Library.h"
#include "Audio/SampleIO.h"
#include "Audio/SessionRecorder.h"
#include "Core/GrabMath.h"

namespace halo
{
    Library::~Library()
    {
        *alive = false;
        peakPool.removeAllJobs (true, 4000);
    }

    const std::vector<float>* Library::peaks (const juce::File& file)
    {
        const auto key = file.getFullPathName();
        if (const auto it = peakCache.find (key); it != peakCache.end())
            return &it->second;
        if (peakPending.count (key) > 0)
            return nullptr;
        peakPending.insert (key);
        std::weak_ptr<bool> token = alive;
        peakPool.addJob ([this, file, key, token]
        {
            std::vector<float> result ((size_t) peakCount, 0.0f);
            if (std::unique_ptr<juce::AudioFormatReader> reader { SampleIO::formats().createReaderFor (file) })
            {
                const auto total = reader->lengthInSamples;
                const int channels = (int) juce::jmin (2u, reader->numChannels);
                for (int i = 0; i < peakCount && total > 0; ++i)
                {
                    const auto start = total * i / peakCount, end = total * (i + 1) / peakCount;
                    juce::Range<float> levels[2];
                    reader->readMaxLevels (start, end - start, levels, channels);
                    float peak = 0.0f;
                    for (int c = 0; c < channels; ++c)
                        peak = juce::jmax (peak, std::abs (levels[c].getStart()), std::abs (levels[c].getEnd()));
                    result[(size_t) i] = peak;
                }
            }
            juce::MessageManager::callAsync ([this, key, token, result = std::move (result)]
            {
                if (token.expired() || ! *token.lock())
                    return;
                peakCache[key] = result;
                peakPending.erase (key);
                sendChangeMessage();
            });
        });
        return nullptr;
    }

    std::vector<int> Library::view (Filter filter, Sort sort, const juce::String& search) const
    {
        std::vector<int> out;
        for (int i = 0; i < (int) all.size(); ++i)
        {
            const auto& it = all[(size_t) i];
            const bool kindOk = filter == Filter::all || (filter == Filter::takes && it.kind == Item::Kind::take)
                             || (filter == Filter::grabs && it.kind == Item::Kind::grab)
                             || (filter == Filter::files && it.kind == Item::Kind::imported);
            if (kindOk && (search.isEmpty() || it.title.containsIgnoreCase (search) || it.file.getFileName().containsIgnoreCase (search)))
                out.push_back (i);
        }
        std::stable_sort (out.begin(), out.end(), [this, sort] (int a, int b)
        {
            const auto& x = all[(size_t) a];
            const auto& y = all[(size_t) b];
            switch (sort)
            {
                case Sort::name:   return x.title.compareNatural (y.title) < 0;
                case Sort::length: return x.seconds > y.seconds;
                case Sort::date:
                default:           return x.modified > y.modified;
            }
        });
        return out;
    }

    Library::Library (Settings& s) : settings (s)
    {
        imported.addLines (settings.getString ("importedFiles"));
        imported.removeEmptyStrings();
        refresh();
    }

    juce::String Library::kindLabel (Item::Kind k)
    {
        switch (k)
        {
            case Item::Kind::take:     return "TAKE";
            case Item::Kind::grab:     return "GRAB";
            case Item::Kind::imported: return "FILE";
        }
        return {};
    }

    void Library::refresh()
    {
        std::vector<Item> found;
        auto add = [&found] (const juce::File& f, Item::Kind kind)
        {
            const auto info = SampleIO::readInfo (f);
            if (! info)
                return;
            Item item;
            item.file = f;
            item.kind = kind;
            item.seconds = info->seconds();
            item.sampleRate = info->sampleRate;
            item.channels = info->channels;
            item.modified = f.getLastModificationTime();

            const auto name = f.getFileName();
            if (const auto grabTitle = grab::titleFromFilename (name.toStdString()))
            {
                item.kind = Item::Kind::grab;
                item.title = juce::String::fromUTF8 (grabTitle->c_str())
                                 .fromFirstOccurrenceOf (juce::String::fromUTF8 ("GRAB \xc2\xb7 "), false, false);
            }
            else if (name.startsWith ("Halo Take "))
            {
                item.title = f.getFileNameWithoutExtension().fromFirstOccurrenceOf ("Halo Take ", false, false);
            }
            else
            {
                item.title = f.getFileNameWithoutExtension();
            }
            found.push_back (std::move (item));
        };

        const auto recordings = SessionRecorder::recordingsFolder();
        for (const auto& entry : juce::RangedDirectoryIterator (recordings, false, "*.wav", juce::File::findFiles))
            add (entry.getFile(), Item::Kind::take);
        for (const auto& path : imported)
            if (juce::File (path).existsAsFile())
                add (juce::File (path), Item::Kind::imported);

        std::sort (found.begin(), found.end(), [] (const Item& a, const Item& b) { return a.modified > b.modified; });
        all = std::move (found);
        sendChangeMessage();
    }

    int Library::importFiles (const juce::StringArray& paths)
    {
        int added = 0;
        for (const auto& path : paths)
        {
            const juce::File f (path);
            if (f.existsAsFile() && SampleIO::isSupported (f) && ! imported.contains (f.getFullPathName())
                && ! f.isAChildOf (SessionRecorder::recordingsFolder()))
            {
                imported.add (f.getFullPathName());
                ++added;
            }
        }
        if (added > 0)
        {
            saveImported();
            refresh();
        }
        return added;
    }

    void Library::forget (const juce::File& f)
    {
        imported.removeString (f.getFullPathName());
        saveImported();
        refresh();
    }

    void Library::saveImported()
    {
        settings.set ("importedFiles", imported.joinIntoString ("\n"));
    }
}
