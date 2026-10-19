#include "Audio/SampleIO.h"
#include "App/HaloPaths.h"

namespace halo
{
    juce::AudioFormatManager& SampleIO::formats()
    {
        static juce::AudioFormatManager manager;
        static const bool registered = [] { manager.registerBasicFormats(); return true; }(); // + MP3 and Media Foundation
        juce::ignoreUnused (registered);
        return manager;
    }

    bool SampleIO::isSupported (const juce::File& f)
    {
        return formats().findFormatForFileExtension (f.getFileExtension()) != nullptr;
    }

    juce::String SampleIO::supportedPattern()
    {
        return formats().getWildcardForAllFormats();
    }

    std::optional<AudioData> SampleIO::decode (const juce::File& file, juce::String& error, double maxSeconds)
    {
        std::unique_ptr<juce::AudioFormatReader> reader (formats().createReaderFor (file));
        if (reader == nullptr)
        {
            error = file.existsAsFile() ? "Can't decode this file" : "File not found";
            return std::nullopt;
        }
        if (reader->sampleRate <= 0.0 || reader->numChannels == 0)
        {
            error = "Not an audio file Halo understands";
            return std::nullopt;
        }
        if ((double) reader->lengthInSamples / reader->sampleRate > maxSeconds)
        {
            error = "Longer than " + juce::String ((int) (maxSeconds / 60.0)) + " minutes";
            return std::nullopt;
        }

        const int frames = (int) reader->lengthInSamples;
        const int channels = (int) juce::jmin (2u, reader->numChannels); // the EP-40 is stereo at most
        juce::AudioBuffer<float> buffer (channels, frames);
        if (frames > 0 && ! reader->read (&buffer, 0, frames, 0, true, channels > 1))
        {
            error = "Read failed";
            return std::nullopt;
        }

        AudioData data;
        data.sampleRate = reader->sampleRate;
        for (int c = 0; c < channels; ++c)
            data.channels.emplace_back (buffer.getReadPointer (c), buffer.getReadPointer (c) + frames);
        return data;
    }

    std::optional<SampleIO::Info> SampleIO::readInfo (const juce::File& file)
    {
        std::unique_ptr<juce::AudioFormatReader> reader (formats().createReaderFor (file));
        if (reader == nullptr)
            return std::nullopt;
        return Info { reader->sampleRate, (int) reader->numChannels, reader->lengthInSamples };
    }

    bool SampleIO::writeWav16 (const AudioData& data, const juce::File& file)
    {
        if (data.channelCount() == 0 || data.sampleRate <= 0.0)
            return false;

        file.deleteFile();
        std::unique_ptr<juce::OutputStream> out (file.createOutputStream());
        if (out == nullptr)
            return false;

        juce::WavAudioFormat wav;
        auto writer = wav.createWriterFor (out, juce::AudioFormatWriterOptions{}
                                                    .withSampleRate (data.sampleRate)
                                                    .withNumChannels (data.channelCount())
                                                    .withBitsPerSample (deviceBitDepth));
        if (writer == nullptr)
            return false;

        std::vector<const float*> channels;
        for (const auto& c : data.channels)
            channels.push_back (c.data());
        return data.frameCount() == 0 || writer->writeFromFloatArrays (channels.data(), data.channelCount(), data.frameCount());
        // writer goes out of scope here, finalising the header
    }

    juce::File SampleIO::exportFolder()
    {
        return HaloPaths::exports();
    }
}
