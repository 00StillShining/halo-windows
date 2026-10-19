#pragma once

#include "App/EditSession.h"
#include "App/Library.h"
#include "App/RackModel.h"
#include "App/Settings.h"
#include "Audio/GrabController.h"
#include "Audio/MonitorEngine.h"
#include "Core/DisplayReducer.h"
#include "Core/Keymap.h"
#include "Core/PadHoldRegistry.h"
#include "Core/RingState.h"
#include "Core/SysEx.h"
#include "Device/DeviceClient.h"
#include "Device/Ep40Backup.h"
#include "Device/Ep40Writer.h"
#include "Device/Ep40Library.h"
#include "Device/Ep40MidiConnection.h"
#include "Device/ProtocolTrace.h"

#include <juce_events/juce_events.h>

#include <deque>
#include <optional>

namespace halo
{
    // The app's single source of truth on the message thread. A slimmed port of
    // HaloAppModel.swift: MIDI connection, display reducer, pad holds, and the
    // honest ring / lifecycle state. Audio, GRAB, rack and storage come later.
    class AppModel : private juce::Timer, private juce::ChangeListener
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

        // What the EP-40 reported about itself (MIDI identity reply), if anything.
        const std::optional<sysex::Identity>& deviceIdentity() const noexcept { return identity; }
        juce::String firmwareStatus() const;
        bool canTalkToDevice() const noexcept { return endpointConnected && ! mock && connection.canSend(); }
        Ep40MidiConnection& midi() noexcept { return connection; }
        ProtocolTrace& trace() noexcept { return protocolTrace; }
        // Every SysEx from the device, for the protocol layer (message thread).
        std::function<void (const sysex::Bytes&)> onDeviceSysex;
        // Sends and traces one SysEx message. False if the EP-40 can't be written to.
        bool sendToDevice (const sysex::Bytes&, const juce::String& note = {});

        // The EP-40's own sounds, pads and storage (TE protocol).
        Ep40Library& device() noexcept { return deviceLibrary; }
        DeviceClient& client() noexcept { return deviceClient; }
        Ep40Backup& backup() noexcept { return backupJob; }
        Ep40Writer& deviceWriter() noexcept { return writer; }
        // Writes to the EP-40 need a connected, synced device with a complete
        // backup. The one formula (H3); it reads the backup cache, never the disk.
        bool writesUnlocked() const { return deviceLibrary.state() == Ep40Library::State::ready && backupJob.hasCompleteBackup (deviceLibrary.info().serial); }
        // The complete backup writes rely on (named in every write confirm).
        std::optional<Ep40Backup::Entry> latestBackup() const { return backupJob.latestComplete (deviceLibrary.info().serial); }

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

        // The one monitor-level getter (H7): the status strip, encoder 1, the
        // PLAY rail's numeral and the meters all read this.
        double monitorGainDb() const noexcept { return engine.gainDb(); }

        // Persisted conveniences.
        void setMonitorGainDb (double db);
        void setMonitorOutput (const juce::String& name);

        // Recent human-readable events for the rail's log (newest last).
        const std::deque<juce::String>& eventLog() const noexcept { return log; }

        static double now() noexcept { return juce::Time::getMillisecondCounterHiRes() * 0.001; }

    private:
        void timerCallback() override;
        void changeListenerCallback (juce::ChangeBroadcaster*) override;
        void connectionChanged (Ep40MidiConnection::Status, const juce::String& name);
        void receive (const MidiEvent&);
        void releaseAllPads();
        void refreshStatus();
        void appendLog (const juce::String&);
        void runMockStep();

        const bool mock;
        static juce::String noValueText() { return juce::String::charToString ((juce::juce_wchar) 0x2014); }
        std::shared_ptr<bool> aliveToken = std::make_shared<bool> (true);
        Ep40MidiConnection connection;
        ProtocolTrace protocolTrace;
        DeviceClient deviceClient { [this] (const te::Bytes& b, const juce::String& note) { return sendToDevice (b, note); } };
        Ep40Library deviceLibrary { deviceClient };
        Ep40Library::State lastLibraryState = Ep40Library::State::idle;
        Ep40Backup backupJob { deviceLibrary };
        Ep40Writer writer { deviceLibrary };
        std::optional<sysex::Identity> identity;
        juce::String deviceOs; // from TE's own GREET reply, once that's implemented
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
        bool lastChopOpen = false;

        bool observerRunning = false;
        bool endpointConnected = false;
        bool portBusy = false;
        juce::String endpointName;

        int previewTick = 0;
        double lastPreviewTime = 0.0;
        int mockStep = 0;
        double mockNextTime = 0.0;
        // Deterministic mock (plan section 6.1): HALO_MOCK_TRANSPORT=stopped sends
        // no clock and no pads; HALO_MOCK_SEED=<n> picks a seeded pad sequence
        // (0 = no pad strikes). Unset: the classic 120 BPM walk.
        bool mockTransportRunning = true;
        int mockSeed = -1;
        std::uint32_t mockRng = 0;
        int mockLastNote = -1;

        std::deque<juce::String> log;
        juce::ListenerList<Listener> listeners;
    };
}
