#include "Audio/MonitorEngine.h"

namespace halo
{
    namespace
    {
        constexpr int preferredBufferSize = 256; // the Mac's "balanced" profile
        constexpr int maxBlock = 4096;
    }

    MonitorEngine::MonitorEngine()
    {
        type.reset (juce::AudioIODeviceType::createAudioIODeviceType_WASAPI (juce::WASAPIDeviceMode::shared));
        if (type != nullptr)
        {
            type->addListener (this);
            type->scanForDevices();
        }
        gain.setTargetDb (gainDecibels);
    }

    MonitorEngine::~MonitorEngine()
    {
        *alive = false;
        stop();
        if (type != nullptr)
            type->removeListener (this);
    }

    bool MonitorEngine::isEp40Name (const juce::String& name)
    {
        const auto lower = name.toLowerCase();
        return lower.contains ("ep-40") || lower.contains ("ep40") || lower.contains ("riddim");
    }

    juce::StringArray MonitorEngine::outputNames() const
    {
        return type != nullptr ? type->getDeviceNames (false) : juce::StringArray();
    }

    juce::String MonitorEngine::ep40InputName() const
    {
        if (type == nullptr)
            return {};
        for (const auto& name : type->getDeviceNames (true))
            if (isEp40Name (name))
                return name;
        return {};
    }

    juce::String MonitorEngine::defaultOutputName() const
    {
        if (type == nullptr)
            return {};
        const auto names = type->getDeviceNames (false);
        const int index = type->getDefaultDeviceIndex (false);
        return juce::isPositiveAndBelow (index, names.size()) ? names[index] : juce::String();
    }

    void MonitorEngine::setOutputName (const juce::String& name)
    {
        if (chosenOutput == name)
            return;
        chosenOutput = name;
        if (isRunning())
        {
            stop();
            start();
        }
        else if (onStateChanged)
        {
            onStateChanged();
        }
    }

    void MonitorEngine::setGainDb (double db)
    {
        gainDecibels = juce::jlimit (MonitorGain::silenceFloorDb, 0.0, db);
        gain.setTargetDb (gainDecibels);
    }

    void MonitorEngine::setState (State newState)
    {
        state = newState;
        if (onStateChanged)
            onStateChanged();
    }

    void MonitorEngine::fail (const juce::String& message)
    {
        stop();
        error = message;
        setState (State::failed);
    }

    void MonitorEngine::start()
    {
        if (isRunning())
            return;
        error.clear();

        if (type == nullptr)
            return fail ("Windows audio (WASAPI) isn't available");

        type->scanForDevices();
        const auto input = ep40InputName();
        if (input.isEmpty())
            return fail ("The EP-40 has no USB audio input on this PC");

        const auto output = chosenOutput.isNotEmpty() ? chosenOutput : defaultOutputName();
        if (output.isEmpty())
            return fail ("No audio output device");
        if (isEp40Name (output))
            return fail (chosenOutput.isEmpty()
                             ? "Windows' default output is the EP-40 itself. Pick another OUTPUT to monitor."
                             : "Monitoring the EP-40 back into the EP-40 would feed back");

        device.reset (type->createDevice (output, input));
        if (device == nullptr)
            return fail ("Couldn't create the audio route");

        juce::BigInteger channels;
        channels.setRange (0, 2, true);

        // Try the EP-40's own rate first, then common rates, then the default.
        juce::String openError = "No sample rate worked";
        const auto rates = device->getAvailableSampleRates();
        for (double rate : { 44100.0, 48000.0, 0.0 })
        {
            if (rate > 0.0 && ! rates.contains (rate))
                continue;
            int buffer = preferredBufferSize;
            const auto sizes = device->getAvailableBufferSizes();
            if (! sizes.isEmpty() && ! sizes.contains (buffer))
                buffer = device->getDefaultBufferSize();
            openError = device->open (channels, channels, rate > 0.0 ? rate : device->getCurrentSampleRate(), buffer);
            if (openError.isEmpty())
                break;
        }
        if (openError.isNotEmpty())
            return fail (openError);

        info.inputName = input;
        info.outputName = output;
        info.sampleRate = device->getCurrentSampleRate();
        info.bufferSize = device->getCurrentBufferSizeSamples();
        info.latencyMs = info.sampleRate > 0.0
            ? 1000.0 * (device->getInputLatencyInSamples() + device->getOutputLatencyInSamples() + info.bufferSize) / info.sampleRate
            : 0.0;

        // GRAB history: a fresh buffer per route, so no previous session leaks in.
        historyBuffer = std::make_shared<RollingCaptureBuffer> (60.0, info.sampleRate);
        inputLatencyFrames = device->getInputLatencyInSamples();

        device->start (this);
        setState (State::running);
    }

    void MonitorEngine::stop()
    {
        // Finalise any take first, so a stop always yields a valid WAV.
        stopRecording();

        if (device != nullptr)
        {
            device->stop();
            device->close();
            device.reset();
        }
        if (state != State::off)
            setState (State::off);
    }

    juce::String MonitorEngine::startRecording()
    {
        if (! isRunning())
            return "Start monitoring first";
        auto message = recorder.start (info.sampleRate);
        if (onStateChanged)
            onStateChanged();
        return message;
    }

    void MonitorEngine::stopRecording()
    {
        if (auto take = recorder.stop())
        {
            if (onTakeFinished)
                onTakeFinished (*take);
            if (onStateChanged)
                onStateChanged();
        }
    }

    void MonitorEngine::checkDiskSpace()
    {
        if (recorder.isRecording() && recorder.isLowOnDisk())
        {
            stopRecording();
            error = "Recording stopped: less than 200 MB free";
            if (onStateChanged)
                onStateChanged();
        }
    }

    void MonitorEngine::audioDeviceListChanged()
    {
        // A device the route uses has gone: stop promptly (finalising any take).
        if (! isRunning())
        {
            if (onStateChanged)
                onStateChanged();
            return;
        }
        const auto inputs = type->getDeviceNames (true);
        const auto outputs = type->getDeviceNames (false);
        if (! inputs.contains (info.inputName) || ! outputs.contains (info.outputName))
            fail ("An audio device was disconnected");
    }

    void MonitorEngine::audioDeviceError (const juce::String& message)
    {
        juce::MessageManager::callAsync ([this, message, alive = alive]
        {
            if (*alive)
                fail (message);
        });
    }

    void MonitorEngine::audioDeviceAboutToStart (juce::AudioIODevice* d)
    {
        const double sr = d->getCurrentSampleRate();
        gain.prepare (sr);
        gain.reset (gainDecibels);
        limiter.prepare (sr);
        meter.prepare (sr);
        inMeter.prepare (sr);
        printMeter.prepare (sr);
        meter.reset();
        scratch.setSize (2, juce::jmax (maxBlock, d->getCurrentBufferSizeSamples()), false, true, false);
        rack.prepare (sr, scratch.getNumSamples()); // a prior tail never leaks across a restart
    }

    void MonitorEngine::audioDeviceIOCallbackWithContext (const float* const* inputs, int numInputs,
                                                          float* const* outputs, int numOutputs,
                                                          int numSamples, const juce::AudioIODeviceCallbackContext&)
    {
        const double blockTime = juce::Time::getMillisecondCounterHiRes() * 0.001; // same clock as MIDI timestamps
        auto* history = historyBuffer.get();

        for (int offset = 0; offset < numSamples; offset += scratch.getNumSamples())
        {
            const int n = juce::jmin (scratch.getNumSamples(), numSamples - offset);
            float* l = scratch.getWritePointer (0);
            float* r = scratch.getWritePointer (1);

            // EP-40 input (mono devices are duplicated to both sides).
            const float* inL = numInputs > 0 && inputs[0] != nullptr ? inputs[0] + offset : nullptr;
            const float* inR = numInputs > 1 && inputs[1] != nullptr ? inputs[1] + offset : inL;
            if (inL != nullptr) std::copy (inL, inL + n, l); else std::fill (l, l + n, 0.0f);
            if (inR != nullptr) std::copy (inR, inR + n, r); else std::fill (r, r + n, 0.0f);

            // Raw, pre-monitor taps: the `in` meter, the recorder (unless
            // printing FX) and GRAB history. The meter only reads.
            inMeter.publish (l, r, n, MeterMath::peak (l, n), MeterMath::peak (r, n));
            const bool print = printFx.load (std::memory_order_relaxed);
            if (! print)
                recorder.write (l, r, n);
            if (history != nullptr)
                history->write (l, r, n, blockTime, inputLatencyFrames + (numSamples - offset - n));

            // gain -> dub rack -> -1 dBFS limiter (the Mac's chain order).
            gain.process (l, r, n);
            rack.process (l, r, n, rackParams);
            if (print)
            {
                recorder.write (l, r, n); // PRINT FX: post-rack, pre-limiter
                printMeter.publish (l, r, n, MeterMath::peak (l, n), MeterMath::peak (r, n));
            }
            const float rawPeakL = MeterMath::peak (l, n), rawPeakR = MeterMath::peak (r, n);
            limiter.process (l, r, n);
            meter.publish (l, r, n, rawPeakL, rawPeakR);

            for (int ch = 0; ch < numOutputs; ++ch)
                if (outputs[ch] != nullptr)
                {
                    const float* src = ch == 0 ? l : ch == 1 ? r : nullptr;
                    if (src != nullptr) std::copy (src, src + n, outputs[ch] + offset);
                    else std::fill (outputs[ch] + offset, outputs[ch] + offset + n, 0.0f);
                }
        }
    }
}
