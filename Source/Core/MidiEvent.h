#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>

namespace halo
{
    // The MIDI messages halo reacts to (port of EP40MIDIMessage). A plain value
    // type so it can travel through a lock-free FIFO from the MIDI thread.
    enum class MidiKind : std::uint8_t
    {
        noteOn,
        noteOff,
        controlChange,
        timingClock,
        start,
        continuePlaying,
        stop,
        songPosition
    };

    struct MidiEvent
    {
        MidiKind kind = MidiKind::noteOff;
        std::uint8_t channel = 0;       // 0...15
        std::uint8_t data1 = 0;         // note or controller
        std::uint8_t data2 = 0;         // velocity or value
        std::uint16_t songPosition = 0; // in sixteenth notes
        double timeSeconds = 0.0;       // arrival time, seconds on a monotonic clock

        bool isNoteOff() const noexcept { return kind == MidiKind::noteOff; }
    };

    // Parses one complete short MIDI message. Note On with velocity 0 becomes a
    // Note Off here, the one place that rule lives. Anything halo doesn't react
    // to (SysEx, aftertouch, pitch bend...) returns nullopt.
    std::optional<MidiEvent> parseMidi (const std::uint8_t* bytes, std::size_t size, double timeSeconds) noexcept;
}
