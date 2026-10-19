#pragma once

#include "App/EditSession.h"
#include "App/Library.h"
#include "App/RackModel.h"
#include "App/Settings.h"
#include "Audio/GrabController.h"
#include "Audio/MonitorEngine.h"
#include "Core/DisplayReducer.h"
#include "Core/PadHoldRegistry.h"
#include "Core/RingState.h"
#include "Device/Ep40MidiConnection.h"

#include <juce_events/juce_events.h>

#include <deque>
#include <optional>

namespace halo
{
    enum class AppMode { play, load, edit, capture, rack, backups };

    // The app's single source of truth on the message thread. A slimmed port of
    // HaloAppModel.swift: MIDI connection, display reducer, pad holds, and the
    // honest ring / lifecycle state. Audio, GRAB, rack and storage come later.
    class AppModel : private juce::Timer
    {
    public:
        struct Listener
        {
            virtual ~Listener() = default;
            virtual void statusChanged() {}
            virtual void displayChanged (const DisplayState&) {}
            virtual void padTransition (const PadHoldRegistry::Transition&) {}
            virtual void allPadsReleased() {}
            virtual void eventLogged() {}
        };

        // mockMidi feeds a built-in synthetic EP-40 instead of the real port,
        // for development without hardware. The UI always labels it MOCK.
        explicit AppModel (bool mockMidi);
        ~AppModel() override;

        void addListener (Listener* l) { listeners.add (l); }
        void removeListener (Listener* l) { listeners.remove (l); }

        void start();

        AppMode mode() const noexcept { return currentMode; }
        void setMode (AppMode);

        const DisplayState& display() const noexcept { return reducer.state(); }
        const RingState& ring() const noexcept { return ringState; }
        const LifecyclePhase& lifecycle() const noexcept { return lifecyclePhase; }
        std::optional<double> clockBpm() const;

        bool isMock() const noexcept { return mock; }
        bool isConnected() const noexcept { return endpointConnected; }
        juce::String deviceStatus() const;
        juce::String usbStatus() const;
        juce::String displayStatus() const;
        juce::String deviceName() const;

        // Audio monitor + recorder. Starts only on explicit action.
        MonitorEngine& monitor() noexcept { return engine; }
        void toggleMonitor();
        void toggleRecording();
        const std::optional<SessionRecorder::Take>& lastTake() const noexcept { return latestTake; }

        // GRAB: last N bars (clock + transport playing) or last N seconds.
        GrabController& grabber() noexcept { return grab; }
        void grabNow();
        bool grabUsesBars() const { return grab.canGrabBars (clockBpm()); }
        void setGrabLength (int barsOrSeconds); // applies to the current mode
        // A short outcome line for the UI, cleared after a few seconds.
        const juce::String& grabMessage() const noexcept { return grabStatus; }

        // The dub rack (runs on the monitor route).
        RackModel& rack() noexcept { return rackModel; }

        // Local samples (takes, grabs, imports) and the EDIT / CHOP session.
        Library& library() noexcept { return sampleLibrary; }
        EditSession& editor() noexcept { return editSession; }
        void openInEditor (const Library::Item&, bool chop);

        // Persisted conveniences.
        void setMonitorGainDb (double db);
        void setMonitorOutput (const juce::String& name);

        // Recent human-readable events for the rail's log (newest last).
        const std::deque<juce::String>& eventLog() const noexcept { return log; }

        static double now() noexcept { return juce::Time::getMillisecondCounterHiRes() * 0.001; }

    private:
        void timerCallback() override;
        void connectionChanged (Ep40MidiConnection::Status, const juce::String& name);
        void receive (const MidiEvent&);
        void releaseAllPads();
        void refreshStatus();
        void appendLog (const juce::String&);
        void runMockStep();

        const bool mock;
        Ep40MidiConnection connection;
        Settings settings;
        MonitorEngine engine;
        RackModel rackModel { engine.rackParameters() };
        Library sampleLibrary { settings };
        EditSession editSession { engine };
        GrabController grab;
        juce::String grabStatus;
        double grabStatusUntil = 0.0;
        std::optional<SessionRecorder::Take> latestTake;
        std::optional<double> recordingStartedAt;
        int diskCheckCountdown = 0;
        DisplayReducer reducer;
        PadHoldRegistry pads;
        RingState ringState;
        LifecyclePhase lifecyclePhase;
        AppMode currentMode = AppMode::play;

        bool observerRunning = false;
        bool endpointConnected = false;
        bool portBusy = false;
        juce::String endpointName;

        int previewTick = 0;
        double lastPreviewTime = 0.0;
        int mockStep = 0;
        double mockNextTime = 0.0;

        std::deque<juce::String> log;
        juce::ListenerList<Listener> listeners;
    };
}
