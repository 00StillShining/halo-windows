#pragma once

#include "Audio/SessionRecorder.h"
#include "Core/DubRack.h"
#include "Core/MonitorDsp.h"
#include "Core/RollingCaptureBuffer.h"

#include <juce_audio_devices/juce_audio_devices.h>

#include <atomic>
#include <functional>

namespace halo
{
    // The monitor route: EP-40 USB input -> gain -> safety limiter -> a PC
    // output, with meters, plus a raw tap for the recorder. A port of
    // EP40AudioRouter + MonitorRenderContext + MonitorController, using one
    // JUCE WASAPI device (input and output in one callback) instead of two
    // AUHALs bridged by a ring buffer.
    //
    // Rules carried over from the Mac app:
    //  - it starts only on an explicit action, at -12 dB;
    //  - it never changes the Windows default devices;
    //  - it refuses EP-40-in to EP-40-out (feedback);
    //  - it stops promptly if a device disappears, finalising any take.
    class MonitorEngine : private juce::AudioIODeviceCallback,
                          private juce::AudioIODeviceType::Listener
    {
    public:
        enum class State { off, running, failed };

        struct RouteInfo
        {
            juce::String inputName, outputName;
            double sampleRate = 0.0;
            int bufferSize = 0;
            double latencyMs = 0.0;
        };

        MonitorEngine();
        ~MonitorEngine() override;

        // Message thread.
        std::function<void()> onStateChanged;
        std::function<void (const SessionRecorder::Take&)> onTakeFinished;

        juce::StringArray outputNames() const;
        juce::String ep40InputName() const;   // empty when the EP-40 has no audio input here
        juce::String defaultOutputName() const;

        void setOutputName (const juce::String& name); // empty = Windows default
        const juce::String& chosenOutputName() const noexcept { return chosenOutput; }

        void start();
        void stop();
        bool isRunning() const noexcept { return state == State::running; }
        State currentState() const noexcept { return state; }
        const juce::String& lastError() const noexcept { return error; }
        const RouteInfo& route() const noexcept { return info; }

        void setGainDb (double db);
        double gainDb() const noexcept { return gainDecibels; }

        StereoLevels levels() { return meter.snapshot(); }

        // The dub rack's parameters (written by the UI, read by the audio thread).
        RackParameters& rackParameters() noexcept { return rackParams; }

        // PRINT FX: takes record post-rack instead of raw. Only changes between takes.
        void setPrintFx (bool shouldPrint) noexcept { if (! isRecording()) printFx = shouldPrint; }
        bool isPrintingFx() const noexcept { return printFx.load(); }

        // The always-on raw history for GRAB (60 s+), only while the route runs.
        std::shared_ptr<RollingCaptureBuffer> history() const { return isRunning() ? historyBuffer : nullptr; }

        // Recording needs a running route (as on the Mac).
        juce::String startRecording();
        void stopRecording();
        bool isRecording() const noexcept { return recorder.isRecording(); }
        double recordingSeconds() const { return recorder.elapsedSeconds(); }
        juce::int64 recordingDroppedSamples() const noexcept { return recorder.droppedSamples(); }
        // Call ~1x a second: stops a take before the disk fills.
        void checkDiskSpace();

        static bool isEp40Name (const juce::String& name);

    private:
        void audioDeviceIOCallbackWithContext (const float* const* inputs, int numInputs,
                                               float* const* outputs, int numOutputs,
                                               int numSamples, const juce::AudioIODeviceCallbackContext&) override;
        void audioDeviceAboutToStart (juce::AudioIODevice*) override;
        void audioDeviceStopped() override {}
        void audioDeviceError (const juce::String& message) override;
        void audioDeviceListChanged() override;

        void fail (const juce::String& message);
        void setState (State);

        std::unique_ptr<juce::AudioIODeviceType> type;
        std::unique_ptr<juce::AudioIODevice> device;
        juce::String chosenOutput;
        State state = State::off;
        juce::String error;
        RouteInfo info;
        double gainDecibels = MonitorGain::defaultDb;

        // Audio-thread state; prepared in audioDeviceAboutToStart.
        MonitorGain gain;
        DubRack rack;
        RackParameters rackParams;
        std::atomic<bool> printFx { false };
        SafetyLimiter limiter;
        AudioMeter meter;
        juce::AudioBuffer<float> scratch;
        SessionRecorder recorder;
        std::shared_ptr<RollingCaptureBuffer> historyBuffer;
        std::int64_t inputLatencyFrames = 0;

        // Lets async callbacks notice the engine has gone (message thread only).
        std::shared_ptr<bool> alive = std::make_shared<bool> (true);
    };
}
