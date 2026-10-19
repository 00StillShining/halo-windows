#pragma once

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
