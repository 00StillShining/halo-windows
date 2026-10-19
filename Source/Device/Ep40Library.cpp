#include "Device/Ep40Library.h"
#include "Core/Ep40MidiMapping.h"

namespace halo
{
    namespace
    {
        bool okFrame (const std::optional<te::Frame>& f) { return f && f->status == te::ok; }

        juce::String statusText (const std::optional<te::Frame>& f)
        {
            if (! f)
                return "no reply (timed out)";
            juce::String text;
            for (auto b : f->body)
                if (b >= 0x20 && b < 0x7F)
                    text += (char) b;
            return "status " + juce::String (f->status) + (text.isNotEmpty() ? ": " + text : juce::String());
        }

        juce::var parseJson (const juce::String& text)
        {
            auto v = juce::JSON::parse (text);
            return v.isObject() ? v : juce::var();
        }

        // OS 2.x uses "samplerate"; OS 1.x used "sample_rate".
        double sampleRateOf (const juce::var& meta)
        {
            for (auto key : { "samplerate", "sample_rate" })
                if (meta.hasProperty (key))
                    return (double) meta[key];
            return 46875.0;
        }
    }

    Ep40Library::Ep40Library (DeviceClient& c) : client (c)
    {
        for (auto& g : pads)
            g.fill (-1);
    }

    const Ep40Library::Sound* Ep40Library::sound (int slot) const
    {
        for (const auto& s : soundList)
            if (s.slot == slot)
                return &s;
        return nullptr;
    }

    int Ep40Library::padSound (int group, int gridIndex) const noexcept
    {
        if (group < 0 || group > 3 || gridIndex < 0 || gridIndex > 11)
            return -1;
        return pads[(size_t) group][(size_t) gridIndex];
    }

    void Ep40Library::clear()
    {
        ++generation;
        current = State::idle;
        soundList.clear();
        deviceInfo = {};
        lastError.clear();
        progressText.clear();
        for (auto& g : pads)
            g.fill (-1);
        sendChangeMessage();
    }

    void Ep40Library::fail (const juce::String& message)
    {
        current = State::failed;
        lastError = message;
        progressText.clear();
        sendChangeMessage();
    }

    void Ep40Library::setProgress (const juce::String& text)
    {
        progressText = text;
        sendChangeMessage();
    }

    void Ep40Library::deviceReportedError (const std::string& text)
    {
        client.reset();
        ++generation;
        fail ("The EP-40 reported an internal error (" + juce::String (text).trim().substring (0, 80)
              + "). Power-cycle it before any more transfers.");
    }

    //==============================================================================
    void Ep40Library::metaGetAll (std::uint16_t fid, std::function<void (std::optional<juce::String>)> done, int page, juce::String acc)
    {
        if (page > 64) // safety: no metadata is this long
            return done (acc);
        client.request (te::file, te::metaGet (fid, (std::uint16_t) page), [this, fid, done, page, acc] (const std::optional<te::Frame>& f) mutable
        {
            if (! okFrame (f))
                return done (std::nullopt);
            const auto p = te::parseMetaPage (f->body);
            if (! p)
                return done (std::nullopt);
            acc += juce::String (p->text);
            if (p->last)
                return done (acc);
            metaGetAll (fid, std::move (done), page + 1, std::move (acc));
        }, 2000, "META GET " + juce::String (fid) + " p" + juce::String (page));
    }

    void Ep40Library::listAll (std::uint16_t node, std::function<void (std::optional<std::vector<te::Entry>>)> done, int page,
                               std::vector<te::Entry> acc)
    {
        if (page > 256)
            return done (acc);
        client.request (te::file, te::list (node, (std::uint16_t) page), [this, node, done, page, acc] (const std::optional<te::Frame>& f) mutable
        {
            if (! okFrame (f))
                return done (std::nullopt);
            const auto p = te::parseList (f->body);
            if (! p)
                return done (std::nullopt);
            acc.insert (acc.end(), p->entries.begin(), p->entries.end());
            if (p->last || p->entries.empty())
                return done (acc);
            listAll (node, std::move (done), page + 1, std::move (acc));
        }, 2000, "LIST " + juce::String (node) + " p" + juce::String (page));
    }

    //==============================================================================
    void Ep40Library::sync()
    {
        if (current == State::syncing)
            return;
        const int gen = ++generation;
        current = State::syncing;
        lastError.clear();
        setProgress ("Saying hello...");

        // 1. GREET: who and what mode. Only "normal" mode is safe for FILE work.
        client.request (te::greet, {}, [this, gen] (const std::optional<te::Frame>& f)
        {
            if (stale (gen)) return;
            if (! okFrame (f))
                return fail ("The EP-40 didn't answer Halo's hello (" + statusText (f) + ").");

            const auto kv = te::parseGreet (f->body);
            auto get = [&kv] (const char* k) { const auto it = kv.find (k); return it != kv.end() ? juce::String (it->second) : juce::String(); };
            deviceInfo.product = get ("product");
            deviceInfo.sku = get ("sku");
            deviceInfo.os = get ("os_version");
            deviceInfo.serial = get ("serial");
            deviceInfo.mode = get ("mode");
            const auto base = get ("base_sku").isNotEmpty() ? get ("base_sku") : deviceInfo.sku;

            if (deviceInfo.mode.isNotEmpty() && deviceInfo.mode != "normal")
                return fail ("The EP-40 is in " + deviceInfo.mode + " mode. Restart it normally first.");
            if (base.isNotEmpty() && base != "TE032AS006" && base != "TE032AS001" && base != "TE032AS005")
                return fail ("Unexpected device (" + base + "). Halo only talks to EP-series samplers.");

            // 2. FILE INIT: open the file session, learn the chunk size.
            setProgress ("Opening the file system...");
            client.request (te::file, te::fileInit(), [this, gen] (const std::optional<te::Frame>& init)
            {
                if (stale (gen)) return;
                if (! okFrame (init))
                    return fail ("File session failed (" + statusText (init) + ").");
                if (const auto r = te::parseInit (init->body))
                    deviceInfo.chunkSize = r->chunkSize;

                // 3. Storage: free space and capacity.
                setProgress ("Reading storage...");
                metaGetAll (te::soundsNode, [this, gen] (std::optional<juce::String> json)
                {
                    if (stale (gen)) return;
                    if (json)
                    {
                        const auto meta = parseJson (*json);
                        if (meta.hasProperty ("free_space_in_bytes")) deviceInfo.freeBytes = (juce::int64) meta["free_space_in_bytes"];
                        if (meta.hasProperty ("max_capacity")) deviceInfo.capacityBytes = (juce::int64) meta["max_capacity"];
                        deviceInfo.categories.clear();
                        if (const auto* tabs = meta["tabs"].getArray())
                            for (const auto& tab : *tabs)
                                if (const auto* range = tab["range"].getArray(); range != nullptr && range->size() == 2)
                                    deviceInfo.categories.push_back ({ tab["name"].toString(), (int) (*range)[0], (int) (*range)[1] });
                        if (const auto* formats = meta["formats"].getArray(); formats != nullptr && ! formats->isEmpty())
                            if (const auto* inner = (*formats)[0]["formats"].getArray(); inner != nullptr && ! inner->isEmpty())
                                if ((*inner)[0].hasProperty ("samplerate.native"))
                                    deviceInfo.nativeRate = (double) (*inner)[0]["samplerate.native"];
                    }

                    // 4. Every sound slot.
                    setProgress ("Listing sounds...");
                    listAll (te::soundsNode, [this, gen] (std::optional<std::vector<te::Entry>> entries)
                    {
                        if (stale (gen)) return;
                        if (! entries)
                            return fail ("Couldn't list the EP-40's sounds.");
                        soundList.clear();
                        for (const auto& e : *entries)
                            if (e.fid >= 1 && e.fid <= 999 && ! e.isDirectory())
                                soundList.push_back ({ e.fid, juce::String (e.name), e.size });

                        // 5. Metadata for each, then 6. the active project's pads.
                        readSoundMeta (0, [this, gen] (bool)
                        {
                            if (stale (gen)) return;
                            metaGetAll (te::projectsNode, [this, gen] (std::optional<juce::String> json)
                            {
                                if (stale (gen)) return;
                                if (json)
                                {
                                    const auto meta = parseJson (*json);
                                    const int active = meta.hasProperty ("active") ? (int) meta["active"] : 0;
                                    if (active >= 3000 && active < 12000)
                                        project = (active - 3000) / 1000 + 1;
                                }
                                readPads (project);
                            });
                        });
                    });
                });
            }, 2000, "FILE INIT");
        }, 2000, "GREET");
    }

    void Ep40Library::readSoundMeta (size_t index, Done done)
    {
        if (index >= soundList.size())
            return done (true);
        const int gen = generation;
        setProgress ("Reading sound " + juce::String (index + 1) + " of " + juce::String ((int) soundList.size()) + "...");
        metaGetAll ((std::uint16_t) soundList[index].slot, [this, gen, index, done] (std::optional<juce::String> json)
        {
            if (stale (gen)) return;
            if (json && index < soundList.size())
            {
                auto& s = soundList[index];
                s.meta = parseJson (*json);
                if (s.meta.hasProperty ("name"))
                    s.name = s.meta["name"].toString();
                if (s.meta.hasProperty ("channels"))
                    s.channels = juce::jmax (1, (int) s.meta["channels"]);
                s.sampleRate = sampleRateOf (s.meta);
            }
            readSoundMeta (index + 1, done);
        });
    }

    void Ep40Library::readPads (int projectNumber)
    {
        const int gen = generation;
        project = juce::jlimit (1, 9, projectNumber);
        for (auto& g : pads)
            g.fill (-1);
        current = State::syncing;
        setProgress ("Reading project " + juce::String (project) + " pads...");
        readPad (0, 0, [this, gen] (bool)
        {
            if (stale (gen)) return;
            current = State::ready;
            progressText.clear();
            sendChangeMessage();
        });
    }

    void Ep40Library::readPad (int group, int grid, Done done)
    {
        if (group > 3)
            return done (true);
        const int gen = generation;
        metaGetAll (te::padFid (project, group, grid), [this, gen, group, grid, done] (std::optional<juce::String> json)
        {
            if (stale (gen)) return;
            if (json)
            {
                const auto meta = parseJson (*json);
                pads[(size_t) group][(size_t) grid] = meta.hasProperty ("sym") ? (int) meta["sym"] : 0;
            }
            const int nextGrid = (grid + 1) % 12;
            readPad (nextGrid == 0 ? group + 1 : group, nextGrid, done);
        });
    }

    //==============================================================================
    void Ep40Library::download (int slot, std::function<void (std::optional<AudioData>, juce::String)> done)
    {
        const auto* s = sound (slot);
        if (s == nullptr)
            return done (std::nullopt, "No sound in slot " + juce::String (slot));
        const int channels = s->channels;
        const double rate = s->sampleRate;
        const int gen = generation;

        client.request (te::file, te::getInit ((std::uint16_t) slot), [this, gen, channels, rate, done] (const std::optional<te::Frame>& f)
        {
            if (stale (gen)) return done (std::nullopt, "Disconnected");
            if (! okFrame (f))
                return done (std::nullopt, "Download failed (" + statusText (f) + ")");
            const auto info = te::parseGetInit (f->body);
            if (! info)
                return done (std::nullopt, "Download failed (bad reply)");

            auto pcm = std::make_shared<te::Bytes>();
            pcm->reserve (info->size);
            auto size = std::make_shared<std::uint32_t> (info->size);

            // Read pages until exactly `size` bytes (never past EOF). The step
            // function holds only a weak reference to itself; each in-flight
            // request holds the strong one, so the chain frees itself when done
            // (it used to clear itself while still running, which crashed).
            auto next = std::make_shared<std::function<void (int)>>();
            std::weak_ptr<std::function<void (int)>> weakNext = next;
            *next = [this, gen, pcm, size, channels, rate, done, weakNext] (int page)
            {
                auto next = weakNext.lock();
                if (pcm->size() >= *size)
                {
                    AudioData a;
                    a.sampleRate = rate;
                    const int frames = (int) (*size / (2u * (std::uint32_t) channels));
                    a.channels.assign ((size_t) channels, std::vector<float> ((size_t) frames));
                    for (int i = 0; i < frames; ++i)
                        for (int c = 0; c < channels; ++c)
                        {
                            const size_t at = ((size_t) i * (size_t) channels + (size_t) c) * 2;
                            const auto v = (std::int16_t) ((*pcm)[at] | ((*pcm)[at + 1] << 8)); // s16 little-endian
                            a.channels[(size_t) c][(size_t) i] = (float) v / 32768.0f;
                        }
                    return done (std::move (a), {});
                }
                client.request (te::file, te::getData ((std::uint16_t) page), [this, gen, pcm, size, done, next, page] (const std::optional<te::Frame>& d)
                {
                    if (stale (gen))
                        return done (std::nullopt, "Disconnected");
                    const auto p = d && d->status == te::ok ? te::parseDataPage (d->body) : std::nullopt;
                    if (! p || p->page != (std::uint16_t) page)
                        return done (std::nullopt, "Download failed at page " + juce::String (page));
                    if (p->data.empty())
                        *size = (std::uint32_t) pcm->size(); // an empty page ends the stream: keep what arrived
                    const auto take = std::min<size_t> (p->data.size(), *size - pcm->size());
                    pcm->insert (pcm->end(), p->data.begin(), p->data.begin() + (long) take);
                    (*next) (page + 1);
                }, 2000, "GET DATA p" + juce::String (page));
            };
            (*next) (0);
        }, 2000, "GET INIT " + juce::String (slot));
    }
}
