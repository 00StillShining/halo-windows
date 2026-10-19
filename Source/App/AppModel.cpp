#include "App/AppModel.h"
#include "Core/Ep40MidiMapping.h"

namespace halo
{
    namespace
    {
        constexpr double mockBpm = 120.0;
        constexpr double mockSecondsPerClock = 60.0 / (mockBpm * 24.0);
        constexpr std::size_t maxLogLines = 60;

        juce::String noteName (std::uint8_t note)
        {
            const auto group = ep40::groupForNote (note);
            const auto grid = ep40::gridIndexForNote (note);
            if (! group || ! grid)
                return juce::String (note);
            return juce::String (note) + " (" + juce::String (ep40::groupLabels[(std::size_t) *group].data())
                   + " " + juce::String (ep40::gridLabels[(std::size_t) *grid].data()) + ")";
        }
    }

    AppModel::AppModel (bool mockMidi) : mock (mockMidi)
    {
        connection.onStatusChanged = [this] (auto status, const auto& name) { connectionChanged (status, name); };
        connection.onEvent = [this] (const MidiEvent& e) { receive (e); };
        connection.onOverflow = [this]
        {
            appendLog ("MIDI backlog overflowed: released all pads");
            releaseAllPads();
        };

        engine.onStateChanged = [this]
        {
            if (engine.currentState() == MonitorEngine::State::failed && engine.lastError().isNotEmpty())
                appendLog ("monitor: " + engine.lastError());
            const bool recording = engine.isRecording();
            if (recording && ! recordingStartedAt)
                recordingStartedAt = now();
            else if (! recording)
                recordingStartedAt.reset();
            refreshStatus();
        };
        engine.onTakeFinished = [this] (const SessionRecorder::Take& take)
        {
            latestTake = take;
            appendLog ("take saved: " + take.file.getFileName() + " (" + juce::String (take.seconds, 1) + " s)");
            if (take.droppedSamples > 0)
                appendLog ("warning: " + juce::String (juce::roundToInt ((double) take.droppedSamples / take.sampleRate * 1000.0))
                           + " ms of audio was dropped (disk too slow)");
        };

        // Restore conveniences (never device facts).
        engine.setGainDb (settings.getDouble ("monitorGainDb", MonitorGain::defaultDb));
        engine.setOutputName (settings.getString ("monitorOutput"));
        grab.bars = settings.getInt ("grabBars", 8);
        grab.seconds = settings.getInt ("grabSeconds", 10);

        grab.onFinished = [this] (const GrabController::Result& r)
        {
            using K = GrabController::Result::Kind;
            switch (r.kind)
            {
                case K::grabbed:
                    grabStatus = "GRABBED " + juce::String (r.gotBars > 0 ? juce::String (r.gotBars) + " BARS" : juce::String (r.seconds, 1) + " S");
                    break;
                case K::truncated:
                    grabStatus = "ONLY " + juce::String (r.gotBars) + " OF " + juce::String (r.askedBars) + " BARS IN HISTORY";
                    break;
                default:
                    grabStatus = r.message.toUpperCase();
                    break;
            }
            grabStatusUntil = now() + 2.5;

            if (r.file.existsAsFile())
            {
                latestTake = SessionRecorder::Take { r.file, r.seconds, engine.route().sampleRate, 0 };
                appendLog ("grab saved: " + r.file.getFileName());
            }
            else
            {
                appendLog ("grab: " + r.message);
            }
            listeners.call ([] (Listener& l) { l.statusChanged(); });
        };
    }

    void AppModel::grabNow()
    {
        grab.grab (engine.history(), engine.route().sampleRate, clockBpm());
    }

    void AppModel::setGrabLength (int value)
    {
        if (grabUsesBars())
        {
            grab.bars = value;
            settings.set ("grabBars", value);
        }
        else
        {
            grab.seconds = value;
            settings.set ("grabSeconds", value);
        }
        listeners.call ([] (Listener& l) { l.statusChanged(); });
    }

    void AppModel::setMonitorGainDb (double db)
    {
        engine.setGainDb (db);
        settings.set ("monitorGainDb", engine.gainDb());
    }

    void AppModel::setMonitorOutput (const juce::String& name)
    {
        engine.setOutputName (name);
        settings.set ("monitorOutput", name);
    }

    void AppModel::toggleMonitor()
    {
        if (engine.isRunning())
        {
            engine.stop();
            appendLog ("monitor off");
            return;
        }
        engine.start();
        if (engine.isRunning())
        {
            const auto& r = engine.route();
            appendLog ("monitor on: " + r.outputName + " @ " + juce::String (r.sampleRate / 1000.0, 1) + " kHz, "
                       + juce::String (r.bufferSize) + " frames, ~" + juce::String (juce::roundToInt (r.latencyMs)) + " ms");
        }
    }

    void AppModel::toggleRecording()
    {
        if (engine.isRecording())
        {
            engine.stopRecording();
            return;
        }
        const auto message = engine.startRecording();
        appendLog (message.isEmpty() ? "recording: " + engine.route().inputName : "can't record: " + message);
    }

    AppModel::~AppModel()
    {
        stopTimer();
        engine.onStateChanged = nullptr;
        engine.onTakeFinished = nullptr;
        engine.stop(); // finalises any take before the model goes
    }

    void AppModel::start()
    {
        observerRunning = true;
        if (mock)
        {
            mockNextTime = now() + 0.6;
            appendLog ("MOCK MIDI: synthetic EP-40, not real hardware");
        }
        else
        {
            connection.start();
        }
        refreshStatus();
        startTimerHz (60);
    }

    void AppModel::setMode (AppMode newMode)
    {
        if (newMode == currentMode)
            return;
        currentMode = newMode;
        listeners.call ([] (Listener& l) { l.statusChanged(); });
    }

    std::optional<double> AppModel::clockBpm() const
    {
        return reducer.clock().bpm (now());
    }

    juce::String AppModel::deviceStatus() const
    {
        if (portBusy) return "IN USE";
        return endpointConnected ? "CONNECTED" : "NO DEVICE";
    }

    juce::String AppModel::usbStatus() const
    {
        if (! endpointConnected) return portBusy ? "BUSY" : "IDLE";
        return reducer.state().feedMode == FeedMode::live ? "MIDI RX" : "MIDI";
    }

    juce::String AppModel::displayStatus() const
    {
        switch (reducer.state().feedMode)
        {
            case FeedMode::preview: return "PREVIEW";
            case FeedMode::waiting: return "WAIT";
            case FeedMode::live:    return "LIVE";
        }
        return {};
    }

    juce::String AppModel::deviceName() const
    {
        return endpointName;
    }

    void AppModel::connectionChanged (Ep40MidiConnection::Status status, const juce::String& name)
    {
        const bool connected = status == Ep40MidiConnection::Status::connected;
        portBusy = status == Ep40MidiConnection::Status::busy;
        endpointName = name;

        if (portBusy)
            appendLog ("\"" + name + "\" is in use by another app. Close it, then replug the EP-40.");
        else
            appendLog (connected ? "connected: " + name : "EP-40 disconnected");

        if (connected != endpointConnected)
        {
            endpointConnected = connected;
            // A USB removal stops the monitor promptly, finalising any take.
            if (! connected && ! mock && engine.isRunning())
            {
                engine.stop();
                appendLog ("monitor stopped: EP-40 disconnected");
            }
            grab.setTransportPlaying (false);
            grab.clockLost();
            // No pad hold, LED or live display may survive a connection change.
            releaseAllPads();
            reducer.connectionChanged (connected);
            listeners.call ([this] (Listener& l) { l.displayChanged (reducer.state()); });
        }
        refreshStatus();
    }

    void AppModel::receive (const MidiEvent& event)
    {
        const bool wasLive = reducer.state().feedMode == FeedMode::live;

        if (event.kind == MidiKind::noteOn)
        {
            if (const auto t = pads.noteOn (event.channel, event.data1, (float) event.data2 / 127.0f))
                listeners.call ([&] (Listener& l) { l.padTransition (*t); });
            appendLog ("note on  " + noteName (event.data1) + "  vel " + juce::String (event.data2));
        }
        else if (event.kind == MidiKind::noteOff)
        {
            if (const auto t = pads.noteOff (event.channel, event.data1))
                listeners.call ([&] (Listener& l) { l.padTransition (*t); });
        }
        else if (event.kind == MidiKind::start)        appendLog ("START");
        else if (event.kind == MidiKind::stop)         appendLog ("STOP");
        else if (event.kind == MidiKind::continuePlaying) appendLog ("CONTINUE");
        else if (event.kind == MidiKind::controlChange)
            appendLog ("CC " + juce::String (event.data1) + " = " + juce::String (event.data2));

        if (reducer.apply (event, now()))
            listeners.call ([this] (Listener& l) { l.displayChanged (reducer.state()); });

        // GRAB's bar grid: transport state, and downbeats stamped as the exact
        // audio frame captured when the clock tick arrived.
        if (event.kind == MidiKind::start || event.kind == MidiKind::continuePlaying)
            grab.setTransportPlaying (true);
        else if (event.kind == MidiKind::stop)
            grab.setTransportPlaying (false);
        else if (event.kind == MidiKind::timingClock && reducer.state().isPlaying && reducer.clock().lastTickWasDownbeat())
            if (const auto history = engine.history())
                if (const auto frame = history->frameAt (event.timeSeconds, engine.route().sampleRate); frame >= 0)
                    grab.markDownbeat (frame);

        // The lifecycle flips to LIVE only on the first live event, so the
        // status bar isn't refreshed for every note.
        if (! wasLive)
            refreshStatus();
    }

    void AppModel::releaseAllPads()
    {
        pads.releaseAll();
        reducer.releaseAllHeldKeys();
        listeners.call ([] (Listener& l) { l.allPadsReleased(); });
    }

    void AppModel::refreshStatus()
    {
        const auto errorLabel = portBusy ? std::optional<std::string> ("BUSY") : std::nullopt;
        ringState = RingState::derive ({ .observerRunning = observerRunning,
                                         .endpointConnected = endpointConnected,
                                         .errorLabel = errorLabel,
                                         .monitorEngaged = engine.isRunning(),
                                         .recordingStartedAt = recordingStartedAt });
        lifecyclePhase = LifecyclePhase::derive ({ .observerRunning = observerRunning,
                                                   .endpointConnected = endpointConnected,
                                                   .displayLive = reducer.state().feedMode == FeedMode::live,
                                                   .monitorEngaged = engine.isRunning(),
                                                   .errorLabel = errorLabel });
        listeners.call ([] (Listener& l) { l.statusChanged(); });
    }

    void AppModel::appendLog (const juce::String& line)
    {
        log.push_back (juce::Time::getCurrentTime().formatted ("%H:%M:%S  ") + line);
        while (log.size() > maxLogLines)
            log.pop_front();
        listeners.call ([] (Listener& l) { l.eventLogged(); });
    }

    void AppModel::timerCallback()
    {
        const double t = now();

        if (mock)
            runMockStep();

        if (--diskCheckCountdown <= 0)
        {
            diskCheckCountdown = 60;
            engine.checkDiskSpace();
        }

        // Six-frames-per-second PREVIEW demo while nothing is connected.
        if (reducer.state().feedMode == FeedMode::preview && t - lastPreviewTime >= 1.0 / 6.0)
        {
            lastPreviewTime = t;
            if (reducer.showPreview (DisplayState::previewFrame (previewTick++)))
                listeners.call ([this] (Listener& l) { l.displayChanged (reducer.state()); });
        }

        if (reducer.expire (t))
        {
            grab.clockLost();
            listeners.call ([this] (Listener& l) { l.displayChanged (reducer.state()); });
            listeners.call ([] (Listener& l) { l.statusChanged(); });
        }

        if (grabStatus.isNotEmpty() && t > grabStatusUntil)
        {
            grabStatus.clear();
            listeners.call ([] (Listener& l) { l.statusChanged(); });
        }
    }

    // A deterministic synthetic EP-40: clock at 120 BPM, and a pad pattern that
    // walks through all four groups. Only used with HALO_MOCK_MIDI=1.
    void AppModel::runMockStep()
    {
        const double t = now();
        if (t < mockNextTime)
            return;

        if (! endpointConnected)
        {
            connectionChanged (Ep40MidiConnection::Status::connected, "MOCK EP-40");
            MidiEvent startEvent;
            startEvent.kind = MidiKind::start;
            startEvent.timeSeconds = t;
            receive (startEvent);
            mockNextTime = t;
            return;
        }

        // Catch up on every clock tick that's due, with exact timestamps.
        while (mockNextTime <= t)
        {
            MidiEvent clock;
            clock.kind = MidiKind::timingClock;
            clock.timeSeconds = mockNextTime;
            receive (clock);

            // Every 8th note (12 clocks): release the last pad, strike the next.
            if (mockStep % 12 == 0)
            {
                static constexpr std::uint8_t offsets[] { 7, 4, 1, 9, 10, 3, 5, 11 };
                const int beat = mockStep / 12;
                const int group = (beat / 8) % 4;
                const auto note = (std::uint8_t) (36 + group * 12 + offsets[beat % 8]);
                const auto previous = (std::uint8_t) (36 + ((beat - 1 + 32) / 8 % 4) * 12 + offsets[(beat + 7) % 8]);

                MidiEvent off;
                off.kind = MidiKind::noteOff;
                off.data1 = previous;
                off.timeSeconds = mockNextTime;
                if (beat > 0)
                    receive (off);

                MidiEvent on;
                on.kind = MidiKind::noteOn;
                on.data1 = note;
                on.data2 = (std::uint8_t) (60 + (beat * 37) % 67);
                on.timeSeconds = mockNextTime;
                receive (on);
            }

            ++mockStep;
            mockNextTime += mockSecondsPerClock;
        }
    }
}
