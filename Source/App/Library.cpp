#include "App/Library.h"
#include "Audio/SampleIO.h"
#include "Audio/SessionRecorder.h"
#include "Core/GrabMath.h"

namespace halo
{
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
