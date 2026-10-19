#include "Audio/SessionRecorder.h"

namespace halo
{
    SessionRecorder::SessionRecorder() = default;

    SessionRecorder::~SessionRecorder()
    {
        stop();
    }

    juce::File SessionRecorder::recordingsFolder()
    {
        return juce::File::getSpecialLocation (juce::File::userMusicDirectory).getChildFile ("Halo").getChildFile ("Recordings");
    }

    bool SessionRecorder::isLowOnDisk() const
    {
        return recordingsFolder().getBytesFreeOnVolume() < minimumFreeBytes;
    }

    juce::String SessionRecorder::start (double sampleRate)
    {
        if (isRecording())
            return {};
        if (sampleRate <= 0.0)
            return "No audio route is running";

        const auto folder = recordingsFolder();
        if (! folder.createDirectory())
            return "Can't create " + folder.getFullPathName();
        if (isLowOnDisk())
            return "Less than 200 MB free on the recordings drive";

        file = folder.getNonexistentChildFile ("Halo Take " + juce::Time::getCurrentTime().formatted ("%Y-%m-%d %H-%M-%S"), ".wav");
        auto stream = file.createOutputStream();
        if (stream == nullptr)
            return "Can't write " + file.getFullPathName();

        juce::WavAudioFormat wav;
        std::unique_ptr<juce::OutputStream> out (std::move (stream));
        auto raw = wav.createWriterFor (out, juce::AudioFormatWriterOptions{}
                                                 .withSampleRate (sampleRate)
                                                 .withNumChannels (2)
                                                 .withBitsPerSample (24));
        if (raw == nullptr)
            return "Can't create a WAV writer";

        thread.startThread();
        // Two seconds of FIFO between the audio thread and the disk.
        threaded = std::make_unique<juce::AudioFormatWriter::ThreadedWriter> (raw.release(), thread, (int) (sampleRate * 2.0));
        rate = sampleRate;
        dropped = 0;
        startedAt = juce::Time::getMillisecondCounterHiRes() * 0.001;
        writer.store (threaded.get());
        return {};
    }

    std::optional<SessionRecorder::Take> SessionRecorder::stop()
    {
        if (threaded == nullptr)
            return std::nullopt;

        // Stop the audio thread from writing, and wait for any write in flight.
        writer.store (nullptr); // seq_cst pairs with write(): two atomics, so no weaker order
        while (writersInFlight.load() != 0)
            juce::Thread::yield();

        // Destroying the ThreadedWriter flushes its FIFO and closes the file, so
        // the WAV header is final before anything reads the take back.
        threaded.reset();
        thread.stopThread (2000);

        Take take;
        take.file = file;
        take.sampleRate = rate;
        take.droppedSamples = dropped.load();

        juce::WavAudioFormat wav;
        if (auto in = file.createInputStream())
            if (std::unique_ptr<juce::AudioFormatReader> reader (wav.createReaderFor (in.release(), true)); reader != nullptr)
                take.seconds = (double) reader->lengthInSamples / reader->sampleRate;

        return take;
    }

    double SessionRecorder::elapsedSeconds() const
    {
        return isRecording() ? juce::Time::getMillisecondCounterHiRes() * 0.001 - startedAt : 0.0;
    }

    void SessionRecorder::write (const float* left, const float* right, int frames) noexcept
    {
        writersInFlight.fetch_add (1);
        if (auto* w = writer.load())
        {
            const float* channels[] { left, right };
            if (! w->write (channels, frames))
                dropped.fetch_add (frames, std::memory_order_relaxed);
        }
        writersInFlight.fetch_sub (1);
    }
}
