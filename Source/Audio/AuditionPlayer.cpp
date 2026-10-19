#include "Audio/AuditionPlayer.h"

namespace halo
{
    AuditionPlayer::AuditionPlayer()
    {
        type.reset (juce::AudioIODeviceType::createAudioIODeviceType_WASAPI (juce::WASAPIDeviceMode::shared));
    }

    AuditionPlayer::~AuditionPlayer()
    {
        stopTimer();
        if (device != nullptr)
        {
            device->stop();
            device->close();
        }
    }

    juce::String AuditionPlayer::play (std::shared_ptr<const AudioData> data, const juce::String& outputName, int token)
    {
        stop();
        if (data == nullptr || data->frameCount() == 0 || data->sampleRate <= 0.0)
            return "Nothing to play";
        if (type == nullptr)
            return "Windows audio isn't available";

        type->scanForDevices();
        auto name = outputName;
        if (name.isEmpty())
        {
            const auto names = type->getDeviceNames (false);
            const int index = type->getDefaultDeviceIndex (false);
            name = juce::isPositiveAndBelow (index, names.size()) ? names[index] : juce::String();
        }

        device.reset (type->createDevice (name, {}));
        if (device == nullptr)
            return "Couldn't open " + name;

        juce::BigInteger outs;
        outs.setRange (0, 2, true);
        const auto error = device->open ({}, outs, device->getCurrentSampleRate(), device->getDefaultBufferSize());
        if (error.isNotEmpty())
        {
            device.reset();
            return error;
        }

        sample = std::move (data);
        ratio = sample->sampleRate / device->getCurrentSampleRate();
        position = 0.0;
        currentToken = token;
        playing = true;
        device->start (this);
        startTimerHz (20);
        return {};
    }

    void AuditionPlayer::stop()
    {
        // The audio thread clears `playing` at the end of the sample, so "was
        // active" is whether a device is still open.
        const bool was = device != nullptr;
        playing = false;
        stopTimer();
        if (device != nullptr)
        {
            device->stop();
            device->close();
            device.reset();
        }
        sample.reset();
        if (was && onStopped)
            onStopped();
    }

    double AuditionPlayer::progress() const noexcept
    {
        const auto s = sample;
        if (s == nullptr || s->frameCount() == 0)
            return 0.0;
        return juce::jlimit (0.0, 1.0, position.load() / s->frameCount());
    }

    void AuditionPlayer::timerCallback()
    {
        if (! playing.load())
            stop(); // reached the end: release the device and notify
    }

    void AuditionPlayer::audioDeviceIOCallbackWithContext (const float* const*, int, float* const* outputs, int numOutputs,
                                                           int numSamples, const juce::AudioIODeviceCallbackContext&)
    {
        const auto* s = sample.get();
        double pos = position.load (std::memory_order_relaxed);
        const int frames = s != nullptr ? s->frameCount() : 0;

        for (int i = 0; i < numSamples; ++i)
        {
            const auto i0 = (int) pos;
            const bool inRange = playing.load (std::memory_order_relaxed) && s != nullptr && i0 < frames;
            for (int ch = 0; ch < numOutputs; ++ch)
            {
                if (outputs[ch] == nullptr)
                    continue;
                float v = 0.0f;
                if (inRange)
                {
                    // Linear interpolation is plenty for auditioning a pre-rendered sample.
                    const auto& src = s->channels[(size_t) juce::jmin (ch, s->channelCount() - 1)];
                    const float a = src[(size_t) i0];
                    const float b = i0 + 1 < frames ? src[(size_t) i0 + 1] : 0.0f;
                    v = a + (b - a) * (float) (pos - i0);
                }
                outputs[ch][i] = v;
            }
            if (inRange)
                pos += ratio;
        }

        position.store (pos, std::memory_order_relaxed);
        if (s != nullptr && pos >= frames)
            playing.store (false); // the timer releases the device on the message thread
    }
}
