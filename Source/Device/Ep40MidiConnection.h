#pragma once

#include "Core/MidiEvent.h"

#include <juce_audio_devices/juce_audio_devices.h>

#include <array>
#include <atomic>
#include <functional>

namespace halo
{
    // Finds the EP-40's MIDI input by name, opens it, and follows hot-plug.
    // Port of EP40MIDIObserver.swift for Windows (WinMM via JUCE).
    //
    // Threading: JUCE delivers MIDI on its own thread. Events are parsed there
    // and pushed into a fixed lock-free FIFO; the message thread drains it. If
    // the FIFO ever overflows, the message thread is told so it can release
    // every held pad, rather than silently losing a Note Off and leaving a pad
    // stuck down (Mac audit finding #9).
    class Ep40MidiConnection : private juce::MidiInputCallback,
                               private juce::AsyncUpdater
    {
    public:
        enum class Status { searching, connected, busy };

        // All callbacks run on the message thread.
        std::function<void (Status, const juce::String& deviceName)> onStatusChanged;
        std::function<void (const MidiEvent&)> onEvent;
        std::function<void()> onOverflow;
        // Complete SysEx messages from the EP-40 (F0 ... F7), in arrival order.
        std::function<void (const juce::MemoryBlock&)> onSysex;

        Ep40MidiConnection();
        ~Ep40MidiConnection() override;

        void start();

        Status status() const noexcept { return currentStatus; }
        const juce::String& deviceName() const noexcept { return currentName; }

        // The EP-40's MIDI output (Halo -> device), needed for SysEx requests.
        bool canSend() const noexcept { return output != nullptr; }
        // Sends one complete SysEx message (F0 ... F7). Message thread.
        bool sendSysex (const std::uint8_t* bytes, std::size_t size);

        // True for a port name that identifies an EP-40 / Riddim, so unrelated
        // controllers can't animate the replica.
        static bool isEp40Name (const juce::String& name);

    private:
        void rescan();
        void setStatus (Status, const juce::String& name);
        void handleIncomingMidiMessage (juce::MidiInput*, const juce::MidiMessage&) override;
        void handleAsyncUpdate() override;

        static constexpr int fifoSize = 2048;
        juce::AbstractFifo fifo { fifoSize };
        std::array<MidiEvent, fifoSize> buffer {};
        std::atomic<bool> overflowed { false };

        std::unique_ptr<juce::MidiInput> input;
        std::unique_ptr<juce::MidiOutput> output;
        juce::String openIdentifier, openOutputIdentifier;

        // SysEx is rare and variable-length: a locked queue is fine here.
        juce::CriticalSection sysexLock;
        std::vector<juce::MemoryBlock> sysexQueue;
        juce::MidiDeviceListConnection deviceListConnection;
        Status currentStatus = Status::searching;
        juce::String currentName;
        bool started = false;
    };
}
