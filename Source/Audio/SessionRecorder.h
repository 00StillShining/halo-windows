#pragma once

#include <juce_audio_formats/juce_audio_formats.h>

#include <atomic>

namespace halo
{
    // Records the raw (pre-gain, pre-limiter) EP-40 input to 24-bit WAV takes.
    // Port of SessionRecorder.swift + WAVFileWriter.swift, fixing three Mac
    // audit findings:
    //  #1 the file is fully closed (header written) before the take is read back;
    //  #2 dropped audio and low disk space are reported, never silently lost;
    //  #3 the WAV header uses the rate the route actually runs at.
    //
    // Threading: start/stop on the message thread; write() on the audio thread
    // (lock-free: JUCE's ThreadedWriter FIFO, drained by a background thread).
    class SessionRecorder
    {
    public:
        struct Take
        {
            juce::File file;
            double seconds = 0.0;
            double sampleRate = 0.0;
            juce::int64 droppedSamples = 0;
        };

        SessionRecorder();
        ~SessionRecorder();

        static juce::File recordingsFolder();

        // Returns an error message, or an empty string on success.
        juce::String start (double sampleRate);

        // Stops, finalises the file and returns what was really written.
        std::optional<Take> stop();

        bool isRecording() const noexcept { return writer.load() != nullptr; }
        double elapsedSeconds() const;
        juce::int64 droppedSamples() const noexcept { return dropped.load(); }
        const juce::File& currentFile() const noexcept { return file; }

        // Audio thread only.
        void write (const float* left, const float* right, int frames) noexcept;

        // Free space below which a take is stopped rather than truncated.
        static constexpr juce::int64 minimumFreeBytes = 200ll * 1024 * 1024;
        bool isLowOnDisk() const;

    private:
        juce::TimeSliceThread thread { "halo recorder" };
        std::unique_ptr<juce::AudioFormatWriter::ThreadedWriter> threaded;
        std::atomic<juce::AudioFormatWriter::ThreadedWriter*> writer { nullptr };
        std::atomic<int> writersInFlight { 0 };
        std::atomic<juce::int64> dropped { 0 };
        juce::File file;
        double rate = 0.0;
        double startedAt = 0.0;
    };
}
