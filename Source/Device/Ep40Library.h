#pragma once

#include "Core/SampleCore.h"
#include "Device/DeviceClient.h"

#include <juce_events/juce_events.h>

#include <array>

namespace halo
{
    // The EP-40's own contents, read over TE's protocol: device info, free
    // space, every sound slot with its metadata, and the active project's pad
    // assignments. This replaces the Mac app's MockDeviceLibrary with the real
    // thing. Everything here is READ-ONLY; writes live in their own guarded
    // operations.
    class Ep40Library : public juce::ChangeBroadcaster
    {
    public:
        struct Sound
        {
            int slot = 0;
            juce::String name;
            std::uint32_t bytes = 0;
            int channels = 1;
            double sampleRate = 46875.0;
            juce::var meta;
            double seconds() const { return sampleRate > 0.0 && channels > 0 ? bytes / (2.0 * channels * sampleRate) : 0.0; }
        };

        struct Category
        {
            juce::String name;
            int firstSlot = 0, lastSlot = 0;
        };

        struct Info
        {
            juce::String product, sku, os, serial, mode;
            std::uint32_t chunkSize = 0;
            juce::int64 freeBytes = -1, capacityBytes = -1;
            double nativeRate = 46875.0;
            std::vector<Category> categories; // the device's own slot ranges (KICKS 1-99 ...)
        };

        enum class State { idle, syncing, ready, failed };

        explicit Ep40Library (DeviceClient&);

        void sync();   // full read-only refresh
        void clear();  // on disconnect

        State state() const noexcept { return current; }
        const juce::String& error() const noexcept { return lastError; }
        const juce::String& progress() const noexcept { return progressText; }
        const Info& info() const noexcept { return deviceInfo; }
        const std::vector<Sound>& sounds() const noexcept { return soundList; }
        const Sound* sound (int slot) const;

        int activeProject() const noexcept { return project; }        // 1-9
        int padSound (int group, int gridIndex) const noexcept;       // slot, 0 = empty, -1 = unknown
        void readPads (int projectNumber);

        // Downloads a sound's PCM (s16 LE) and converts it to float audio.
        void download (int slot, std::function<void (std::optional<AudioData>, juce::String error)>);
        // Downloads any file's raw bytes (sounds, or a project as the device's TAR).
        void downloadRaw (std::uint16_t fid, std::function<void (std::optional<te::Bytes>, juce::String error)>, int timeoutMs = 2000);

        // Lowest empty slot in [first, last], or 0 if none.
        int firstFreeSlot (int first, int last) const;
        // Re-read one slot after a write (adds, updates or removes it).
        void refreshSound (int slot, std::function<void (bool)> done);
        // Record a pad assignment Halo just wrote (no device read needed).
        void notePadAssignment (int projectNumber, int group, int gridIndex, int slot);
        void noteFreeBytes (juce::int64 bytes) { deviceInfo.freeBytes = bytes; sendChangeMessage(); }

        // Low-level access for guarded operations (backup, upload).
        DeviceClient& deviceClient() noexcept { return client; }
        int currentGeneration() const noexcept { return generation; }

        // Firmware error text seen on the wire: all FILE traffic stops.
        void deviceReportedError (const std::string& text);

    private:
        using Done = std::function<void (bool ok)>;
        void fail (const juce::String&);
        void setProgress (const juce::String&);
        void metaGetAll (std::uint16_t fid, std::function<void (std::optional<juce::String>)>, int page = 0, juce::String acc = {});
        void listAll (std::uint16_t node, std::function<void (std::optional<std::vector<te::Entry>>)>, int page = 0,
                      std::vector<te::Entry> acc = {});
        void readSoundMeta (size_t index, Done);
        void readPad (int group, int grid, Done);
        bool stale (int gen) const { return gen != generation; }

        DeviceClient& client;
        State current = State::idle;
        juce::String lastError, progressText;
        Info deviceInfo;
        std::vector<Sound> soundList;
        int project = 1;
        std::array<std::array<int, 12>, 4> pads {};
        int generation = 0;
    };
}
