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
