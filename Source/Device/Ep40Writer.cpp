#include "Device/Ep40Writer.h"

namespace halo
{
    namespace
    {
        bool okFrame (const std::optional<te::Frame>& f) { return f && f->status == te::ok; }

        juce::String why (const std::optional<te::Frame>& f)
        {
            if (! f)
                return "no reply from the EP-40";
            juce::String text;
            for (auto b : f->body)
                if (b >= 0x20 && b < 0x7F)
                    text += (char) b;
            return text.isNotEmpty() ? text : "status " + juce::String (f->status);
        }

        // Interleaved signed 16-bit little-endian: the EP-40's storage format.
        te::Bytes toPcm16 (const AudioData& a)
        {
            te::Bytes out;
            const int frames = a.frameCount(), channels = a.channelCount();
            out.reserve ((size_t) frames * (size_t) channels * 2);
            for (int i = 0; i < frames; ++i)
                for (int c = 0; c < channels; ++c)
                {
                    const float v = juce::jlimit (-1.0f, 1.0f, a.channels[(size_t) c][(size_t) i]);
                    const auto s = (std::int16_t) juce::roundToInt (v * 32767.0f);
                    out.push_back ((std::uint8_t) (s & 0xFF));
                    out.push_back ((std::uint8_t) ((s >> 8) & 0xFF));
                }
            return out;
        }
    }

    Ep40Writer::Ep40Writer (Ep40Library& l) : lib (l) {}

    void Ep40Writer::upload (const AudioData& input, const juce::String& rawName, int requestedSlot, std::function<void (Result)> done)
    {
        if (busy)
            return done ({ false, 0, "Another transfer is still running" });
        if (lib.state() != Ep40Library::State::ready)
            return done ({ false, 0, "The EP-40 isn't synced" });
        if (input.frameCount() == 0 || input.channelCount() < 1 || input.channelCount() > 2)
            return done ({ false, 0, "Nothing to send (empty, or more than 2 channels)" });

        // Never above the device's native rate (46,875 Hz on the EP-40).
        const double maxRate = lib.info().nativeRate > 0.0 ? lib.info().nativeRate : 46875.0;
        const AudioData audio = input.sampleRate > maxRate ? resample (input, maxRate) : input;
        const int rate = juce::roundToInt (audio.sampleRate);
        auto pcm = std::make_shared<te::Bytes> (toPcm16 (audio));

        const int slot = requestedSlot > 0 ? requestedSlot : lib.firstFreeSlot (userFirstSlot, userLastSlot);
        if (slot <= 0)
            return done ({ false, 0, "No empty slot in USER (900-999)" });
        if (lib.sound (slot) != nullptr)
            return done ({ false, slot, "Slot " + juce::String (slot) + " isn't empty; Halo never overwrites sounds" });
        if (lib.info().freeBytes >= 0 && (juce::int64) pcm->size() > lib.info().freeBytes)
            return done ({ false, slot, "Not enough space on the EP-40" });

        const auto name = juce::String (te::normalizeName (rawName.toStdString()));
        const auto json = "{\"channels\":" + juce::String (audio.channelCount()) + ",\"samplerate\":" + juce::String (rate) + "}";
        auto& client = lib.deviceClient();
        busy = true;
        auto finish = [this, done] (Result r) { busy = false; done (std::move (r)); };

        // 1. PUT INIT: announce the file.
        client.request (te::file, te::putInit ((std::uint16_t) slot, (std::uint32_t) pcm->size(), name.toStdString(), json.toStdString()),
            [this, pcm, slot, name, json, finish] (const std::optional<te::Frame>& init) mutable
        {
            if (! okFrame (init))
                return finish ({ false, slot, "The EP-40 refused the upload: " + why (init) });

            // 2. PUT DATA pages, then the empty final page.
            const int chunk = (int) lib.info().chunkSize;
            const int pageSize = chunk >= 64 ? juce::jmin (te::maxPutDataBytes, ((chunk - 6 - 10) * 7) / 8) : te::maxPutDataBytes;
            sendPages (pcm, 0, pageSize, [this, pcm, slot, name, json, finish] (bool ok, juce::String error) mutable
            {
                if (! ok)
                    return finish ({ false, slot, "Upload stopped: " + error + ". Power-cycle the EP-40 before trying again." });

                // 3. Metadata, 4. FILE INIT, 5. verify by reading the size back.
                auto& c = lib.deviceClient();
                const auto meta = json.dropLastCharacters (1) + ",\"name\":\"" + name + "\"}";
                c.request (te::file, te::metaSet ((std::uint16_t) slot, meta.toStdString()), [this, pcm, slot, finish] (const std::optional<te::Frame>& m) mutable
                {
                    if (! okFrame (m))
                        return finish ({ false, slot, "Sound uploaded, but its settings were refused: " + why (m) });
                    lib.deviceClient().request (te::file, te::fileInit(), [this, pcm, slot, finish] (const std::optional<te::Frame>&) mutable
                    {
                        lib.refreshSound (slot, [this, pcm, slot, finish] (bool found) mutable
                        {
                            const auto* s = lib.sound (slot);
                            if (! found || s == nullptr || s->bytes != pcm->size())
                                return finish ({ false, slot, "Verification failed: slot " + juce::String (slot) + " reads back "
                                                                  + juce::String (s != nullptr ? (int) s->bytes : 0) + " bytes, expected "
                                                                  + juce::String ((int) pcm->size()) });
                            if (lib.info().freeBytes >= 0)
                                lib.noteFreeBytes (lib.info().freeBytes - (juce::int64) pcm->size());
                            finish ({ true, slot, "Sent to slot " + juce::String (slot) + " and verified (" + juce::String ((int) pcm->size()) + " bytes)" });
                        });
                    }, 2000, "FILE INIT");
                }, 2000, "META SET " + juce::String (slot));
            });
        }, 2000, "PUT INIT " + juce::String (slot));
    }

    void Ep40Writer::sendPages (std::shared_ptr<te::Bytes> pcm, int page, int pageSize, std::function<void (bool, juce::String)> done)
    {
        const size_t offset = (size_t) page * (size_t) pageSize;
        const size_t count = offset < pcm->size() ? std::min<size_t> ((size_t) pageSize, pcm->size() - offset) : 0;
        // count == 0 is the required empty terminating page.
        lib.deviceClient().request (te::file, te::putData ((std::uint16_t) page, count > 0 ? pcm->data() + offset : nullptr, count),
            [this, pcm, page, pageSize, count, done] (const std::optional<te::Frame>& f)
        {
            if (! okFrame (f))
                return done (false, "page " + juce::String (page) + ": " + why (f));
            if (count == 0)
                return done (true, {});
            sendPages (pcm, page + 1, pageSize, done);
        }, 4000, count > 0 ? "PUT DATA p" + juce::String (page) : juce::String ("PUT DATA end"));
    }

    void Ep40Writer::deleteSound (int slot, std::function<void (Result)> done)
    {
        if (busy)
            return done ({ false, slot, "Another transfer is still running" });
        if (lib.state() != Ep40Library::State::ready || slot < 1 || slot > 999 || lib.sound (slot) == nullptr)
            return done ({ false, slot, "No sound in slot " + juce::String (slot) });

        busy = true;
        const auto bytes = (juce::int64) lib.sound (slot)->bytes;
        lib.deviceClient().request (te::file, te::deleteFile ((std::uint16_t) slot), [this, slot, bytes, done] (const std::optional<te::Frame>& f)
        {
            if (! okFrame (f))
            {
                busy = false;
                return done ({ false, slot, "The EP-40 refused the delete: " + why (f) });
            }
            lib.deviceClient().request (te::file, te::fileInit(), [this, slot, bytes, done] (const std::optional<te::Frame>&)
            {
                // Confirm with a fresh read: metadata can lag, the file node can't.
                lib.refreshSound (slot, [this, slot, bytes, done] (bool stillThere)
                {
                    busy = false;
                    if (stillThere)
                        return done ({ false, slot, "Slot " + juce::String (slot) + " still reads back after the delete" });
                    if (lib.info().freeBytes >= 0)
                        lib.noteFreeBytes (lib.info().freeBytes + bytes);
                    done ({ true, slot, "Deleted slot " + juce::String (slot) });
                });
            }, 2000, "FILE INIT");
        }, 2000, "DELETE " + juce::String (slot));
    }

    void Ep40Writer::readPad (int project, int group, int gridIndex, std::function<void (int)> done)
    {
        lib.deviceClient().request (te::file, te::metaGet (te::padFid (project, group, gridIndex), 0), [done] (const std::optional<te::Frame>& f)
        {
            if (! okFrame (f))
                return done (-1);
            const auto page = te::parseMetaPage (f->body);
            const auto meta = page ? juce::JSON::parse (juce::String (page->text)) : juce::var();
            done (meta.hasProperty ("sym") ? (int) meta["sym"] : 0);
        }, 2000, "META GET pad");
    }

    void Ep40Writer::assignPad (int project, int group, int gridIndex, int slot, std::function<void (Result)> done)
    {
        if (lib.state() != Ep40Library::State::ready)
            return done ({ false, slot, "The EP-40 isn't synced" });
        if (slot <= 0 || lib.sound (slot) == nullptr)
            return done ({ false, slot, "Slot " + juce::String (slot) + " has no sound" });

        const auto fid = te::padFid (project, group, gridIndex);
        lib.deviceClient().request (te::file, te::metaSet (fid, "{\"sym\":" + std::to_string (slot) + "}"),
            [this, project, group, gridIndex, slot, done] (const std::optional<te::Frame>& f)
        {
            if (! okFrame (f))
                return done ({ false, slot, "The EP-40 refused the pad assignment: " + why (f) });
            lib.notePadAssignment (project, group, gridIndex, slot);
            done ({ true, slot, "Pad assigned" });
        }, 2000, "META SET pad " + juce::String (fid) + " sym " + juce::String (slot));
    }

    void Ep40Writer::uploadToPads (std::vector<PadJob> jobs, int project, std::function<void (const juce::String&)> onProgress,
                                   std::function<void (Result)> done)
    {
        runPadJobs (std::make_shared<std::vector<PadJob>> (std::move (jobs)), 0, project, std::move (onProgress), std::move (done));
    }

    void Ep40Writer::runPadJobs (std::shared_ptr<std::vector<PadJob>> jobs, size_t index, int project,
                                 std::function<void (const juce::String&)> onProgress, std::function<void (Result)> done)
    {
        if (index >= jobs->size())
            return done ({ true, 0, "Sent " + juce::String ((int) jobs->size()) + " sounds and assigned their pads" });

        auto& job = (*jobs)[index];
        onProgress ("Sending " + juce::String ((int) index + 1) + " of " + juce::String ((int) jobs->size()) + "...");
        upload (job.audio, job.name, 0, [this, jobs, index, project, onProgress, done] (Result r)
        {
            if (! r.ok)
                return done ({ false, r.slot, "Stopped at " + juce::String ((int) index + 1) + " of " + juce::String ((int) jobs->size()) + ": " + r.message });
            const auto& j = (*jobs)[index];
            assignPad (project, j.group, j.gridIndex, r.slot, [this, jobs, index, project, onProgress, done] (Result a)
            {
                if (! a.ok)
                    return done ({ false, a.slot, a.message });
                runPadJobs (jobs, index + 1, project, onProgress, done);
            });
        });
    }
}
