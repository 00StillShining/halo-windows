#pragma once

#include "Core/SampleCore.h"

#include <juce_audio_devices/juce_audio_devices.h>

#include <atomic>
#include <functional>
#include <memory>

namespace halo
{
    // Plays a rendered sample once through a PC output, for auditioning edits
    // and slices (port of AuditionPlayer.swift). It opens its own output stream
    // only while playing; WASAPI shared mode mixes it with the monitor.
    // Playback converts the sample's rate (e.g. a LO-FI 11 kHz render) to the
    // device rate, so what you hear is what would be exported.
    class AuditionPlayer : private juce::AudioIODeviceCallback, private juce::Timer
    {
    public:
        AuditionPlayer();
        ~AuditionPlayer() override;

        // Message thread. An empty outputName means the Windows default.
        juce::String play (std::shared_ptr<const AudioData> data, const juce::String& outputName, int token = 0);
        void stop();

        bool isPlaying() const noexcept { return playing.load(); }
        int token() const noexcept { return currentToken; }
        // 0..1 through the sample, for a playhead.
        double progress() const noexcept;

        std::function<void()> onStopped; // message thread

    private:
        void audioDeviceIOCallbackWithContext (const float* const*, int, float* const* outputs, int numOutputs,
                                               int numSamples, const juce::AudioIODeviceCallbackContext&) override;
        void audioDeviceAboutToStart (juce::AudioIODevice*) override {}
        void audioDeviceStopped() override {}
        void timerCallback() override;

        std::unique_ptr<juce::AudioIODeviceType> type;
        std::unique_ptr<juce::AudioIODevice> device;
        std::shared_ptr<const AudioData> sample;
        std::atomic<double> position { 0.0 }; // in sample frames
        double ratio = 1.0;                   // sample frames per device frame
        std::atomic<bool> playing { false };
        int currentToken = 0;
    };
}
