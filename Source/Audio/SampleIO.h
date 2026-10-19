#pragma once

#include "Core/SampleCore.h"

#include <juce_audio_formats/juce_audio_formats.h>

namespace halo
{
    // File side of the sample features (port of SampleProcessor.swift's decode
    // and SampleTreatmentEncoder's WAV writing).
    struct SampleIO
    {
        // WAV, AIFF, FLAC, Ogg, MP3 and (via Windows Media Foundation) M4A/AAC/WMA.
        static juce::AudioFormatManager& formats();
        static bool isSupported (const juce::File&);
        static juce::String supportedPattern(); // "*.wav;*.aif;..."

        // Whole-file decode to float. Empty optional plus a reason on failure.
        static std::optional<AudioData> decode (const juce::File&, juce::String& error,
                                                double maxSeconds = 15.0 * 60.0);

        struct Info
        {
            double sampleRate = 0.0;
            int channels = 0;
            juce::int64 frames = 0;
            double seconds() const { return sampleRate > 0.0 ? (double) frames / sampleRate : 0.0; }
        };
        static std::optional<Info> readInfo (const juce::File&);

        // 16-bit PCM WAV, the EP-40's storage depth. Returns false on failure.
        static bool writeWav16 (const AudioData&, const juce::File&);

        static juce::File exportFolder(); // Music\Halo\Export
    };
}
