#include "Device/Ep40Backup.h"
#include "App/HaloPaths.h"

namespace halo
{
    namespace
    {
        // A plain RIFF/WAVE file around raw s16 little-endian PCM: bit-exact.
        bool writeRawWav (const juce::File& file, const te::Bytes& pcm, int channels, int sampleRate)
        {
            juce::MemoryOutputStream out;
            const auto dataSize = (juce::uint32) pcm.size();
            out.write ("RIFF", 4);
            out.writeInt ((int) (36 + dataSize));
            out.write ("WAVEfmt ", 8);
            out.writeInt (16);
            out.writeShort (1); // PCM
            out.writeShort ((short) channels);
            out.writeInt (sampleRate);
            out.writeInt (sampleRate * channels * 2);
            out.writeShort ((short) (channels * 2));
            out.writeShort (16);
            out.write ("data", 4);
            out.writeInt ((int) dataSize);
            out.write (pcm.data(), pcm.size());
            return file.replaceWithData (out.getData(), out.getDataSize());
        }

        juce::String safe (const juce::String& s)
        {
            return s.retainCharacters ("ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789 ._-").trim();
        }
    }

    Ep40Backup::Ep40Backup (Ep40Library& l) : lib (l)
    {
        // One synchronous scan at start-up, so the very first gate check is
        // right; from then on the disk is only read off the message thread.
        cache = list();
        startTimer (10000);
    }

    Ep40Backup::~Ep40Backup()
    {
        *alive = false;
        stopTimer();
    }

    juce::File Ep40Backup::backupsFolder()
    {
        return HaloPaths::backups();
    }

    juce::Time Ep40Backup::Entry::createdTime() const
    {
        const auto t = juce::Time::fromISO8601 (created);
        return t.toMilliseconds() != 0 ? t : folder.getCreationTime();
    }

    juce::String Ep40Backup::describe (const Entry& e)
    {
        const auto t = e.createdTime();
        return "backup " + juce::String (t.getDayOfMonth()) + " " + t.getMonthName (true).toLowerCase() + " "
               + juce::String (t.getYear()) + " " + t.formatted ("%H:%M") + juce::String::fromUTF8 (" \xc2\xb7 ")
               + (e.complete ? "complete" : "incomplete") + juce::String::fromUTF8 (" \xc2\xb7 ")
               + juce::String (e.sounds) + " sounds";
    }

    std::optional<Ep40Backup::Entry> Ep40Backup::latestComplete (const juce::String& serial) const
    {
        if (serial.isEmpty())
            return std::nullopt;
        for (const auto& e : cache) // newest first
            if (e.complete && e.serial == serial)
                return e;
        return std::nullopt;
    }

    void Ep40Backup::adopt (std::vector<Entry> fresh)
    {
        if (fresh == cache)
            return;
        cache = std::move (fresh);
        sendChangeMessage();
    }

    void Ep40Backup::rescanAsync()
    {
        if (scanPending)
        {
            rescanAgain = true; // the running scan may predate this change
            return;
        }
        scanPending = true;
        rescanAgain = false;
        juce::Thread::launch ([this, token = std::weak_ptr<bool> (alive)]
        {
            auto fresh = list();
            juce::MessageManager::callAsync ([this, token, fresh = std::move (fresh)]() mutable
            {
                if (token.expired())
                    return;
                scanPending = false;
                adopt (std::move (fresh));
                if (rescanAgain)
                    rescanAsync();
            });
        });
    }

    void Ep40Backup::forceRescan()
    {
        ++syncScans;
        adopt (list());
    }

    std::vector<Ep40Backup::Entry> Ep40Backup::list()
    {
        std::vector<Entry> out;
        for (const auto& item : juce::RangedDirectoryIterator (backupsFolder(), false, "*", juce::File::findDirectories))
        {
            Entry e;
            e.folder = item.getFile();
            const auto meta = juce::JSON::parse (e.folder.getChildFile ("backup.json"));
            if (meta.isObject())
            {
                e.serial = meta["serial"].toString();
                e.os = meta["os_version"].toString();
                e.created = meta["created"].toString();
                e.sounds = (int) meta["sounds"];
                e.projects = (int) meta["projects"];
                e.complete = (bool) meta["complete"];
            }
            out.push_back (e);
        }
        std::sort (out.begin(), out.end(), [] (const Entry& a, const Entry& b)
        {
            return a.folder.getCreationTime() > b.folder.getCreationTime();
        });
        return out;
    }

    void Ep40Backup::setStatus (const juce::String& s)
    {
        statusLine = s;
        sendChangeMessage();
    }

    void Ep40Backup::start()
    {
        if (running || lib.state() != Ep40Library::State::ready)
            return;
        const auto& info = lib.info();
        folder = backupsFolder().getChildFile (safe ("EP-40 " + info.serial + " " + juce::Time::getCurrentTime().formatted ("%Y-%m-%d %H-%M")));
        folder = folder.getNonexistentSibling (false);
        if (! folder.getChildFile ("sounds").createDirectory() || ! folder.getChildFile ("projects").createDirectory())
            return setStatus ("Can't create " + folder.getFullPathName());

        running = true;
        cancelled = false;
        problems.clear();
        projectsSaved = soundsSaved = 0;
        doneBytes = 0;
        totalBytes = 0;
        for (const auto& s : lib.sounds())
            totalBytes += s.bytes;
        startedAt = juce::Time::getMillisecondCounterHiRes();
        generation = lib.currentGeneration();
        rescanAsync();
        nextProject (1);
    }

    void Ep40Backup::cancel()
    {
        cancelled = true;
    }

    void Ep40Backup::nextProject (int project)
    {
        if (cancelled || lib.currentGeneration() != generation)
            return finish (false, cancelled ? "Backup cancelled (incomplete, so writes stay locked)" : "EP-40 disconnected during the backup");
        if (project > 9)
            return nextSound (0);

        setStatus ("Saving project " + juce::String (project) + " of 9...");
        lib.downloadRaw (te::projectFid (project), [this, project] (std::optional<te::Bytes> tar, juce::String error)
        {
            if (tar && ! tar->empty())
            {
                folder.getChildFile ("projects").getChildFile ("P" + juce::String (project).paddedLeft ('0', 2) + ".tar")
                    .replaceWithData (tar->data(), tar->size());
                ++projectsSaved;
            }
            else
            {
                problems.add ("project " + juce::String (project) + ": " + (error.isNotEmpty() ? error : juce::String ("empty")));
            }
            nextProject (project + 1);
        }, 15000); // the device builds the archive on request
    }

    void Ep40Backup::nextSound (size_t index)
    {
        if (cancelled || lib.currentGeneration() != generation)
            return finish (false, cancelled ? "Backup cancelled (incomplete, so writes stay locked)" : "EP-40 disconnected during the backup");
        const auto& sounds = lib.sounds();
        if (index >= sounds.size())
            return finish (problems.isEmpty(), {});

        const auto sound = sounds[index];
        fraction = totalBytes > 0 ? (double) doneBytes / (double) totalBytes : 0.0;
        const double elapsed = (juce::Time::getMillisecondCounterHiRes() - startedAt) / 1000.0;
        juce::String eta;
        if (doneBytes > 0)
        {
            const int left = (int) (elapsed / (double) doneBytes * (double) (totalBytes - doneBytes));
            eta = ", about " + (left >= 60 ? juce::String (left / 60) + " min" : juce::String (left) + " s") + " left";
        }
        setStatus ("Saving sound " + juce::String ((int) index + 1) + " of " + juce::String ((int) sounds.size()) + eta);

        lib.downloadRaw ((std::uint16_t) sound.slot, [this, index, sound] (std::optional<te::Bytes> pcm, juce::String error)
        {
            const auto base = juce::String (sound.slot).paddedLeft ('0', 3);
            if (pcm)
            {
                const auto name = safe (sound.name);
                writeRawWav (folder.getChildFile ("sounds").getChildFile (base + (name.isNotEmpty() ? " " + name : juce::String()) + ".wav"),
                             *pcm, sound.channels, (int) sound.sampleRate);
                folder.getChildFile ("sounds").getChildFile (base + ".json").replaceWithText (juce::JSON::toString (sound.meta));
                ++soundsSaved;
            }
            else
            {
                problems.add ("sound " + base + ": " + error);
            }
            doneBytes += sound.bytes;
            nextSound (index + 1);
        });
    }

    void Ep40Backup::finish (bool ok, const juce::String& message)
    {
        running = false;
        fraction = ok ? 1.0 : fraction;
        const auto& info = lib.info();

        auto* meta = new juce::DynamicObject();
        meta->setProperty ("device", info.product);
        meta->setProperty ("sku", info.sku);
        meta->setProperty ("serial", info.serial);
        meta->setProperty ("os_version", info.os);
        meta->setProperty ("created", juce::Time::getCurrentTime().toISO8601 (true));
        meta->setProperty ("projects", projectsSaved);
        meta->setProperty ("sounds", soundsSaved);
        meta->setProperty ("complete", ok);
        meta->setProperty ("problems", problems.joinIntoString ("\n"));
        meta->setProperty ("made_by", "Halo for Windows");
        folder.getChildFile ("backup.json").replaceWithText (juce::JSON::toString (juce::var (meta)));
        rescanAsync();

        const double minutes = (juce::Time::getMillisecondCounterHiRes() - startedAt) / 60000.0;
        setStatus (ok ? "Backup complete: " + juce::String (soundsSaved) + " sounds, " + juce::String (projectsSaved)
                            + " projects in " + juce::String (minutes, 1) + " min"
                      : message.isNotEmpty() ? message
                                             : "Backup finished with problems (" + juce::String (problems.size()) + "), so writes stay locked: "
                                                   + problems[0]);
    }
}
