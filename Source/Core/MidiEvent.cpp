#include "Core/MidiEvent.h"

namespace halo
{
    std::optional<MidiEvent> parseMidi (const std::uint8_t* bytes, std::size_t size, double timeSeconds) noexcept
    {
        if (bytes == nullptr || size == 0)
            return std::nullopt;

        MidiEvent event;
        event.timeSeconds = timeSeconds;
        const auto status = bytes[0];

        switch (status)
        {
            case 0xF8: event.kind = MidiKind::timingClock;     return event;
            case 0xFA: event.kind = MidiKind::start;           return event;
            case 0xFB: event.kind = MidiKind::continuePlaying; return event;
            case 0xFC: event.kind = MidiKind::stop;            return event;
            case 0xF2:
                if (size < 3)
                    return std::nullopt;
                event.kind = MidiKind::songPosition;
                event.songPosition = (std::uint16_t) ((bytes[1] & 0x7F) | ((bytes[2] & 0x7F) << 7));
                return event;
            default:
                break;
        }

        if (status < 0x80 || status >= 0xF0 || size < 3)
            return std::nullopt;

        event.channel = (std::uint8_t) (status & 0x0F);
        event.data1 = (std::uint8_t) (bytes[1] & 0x7F);
        event.data2 = (std::uint8_t) (bytes[2] & 0x7F);

        switch (status & 0xF0)
        {
            case 0x90: event.kind = event.data2 == 0 ? MidiKind::noteOff : MidiKind::noteOn; break;
            case 0x80: event.kind = MidiKind::noteOff; event.data2 = 0; break;
            case 0xB0: event.kind = MidiKind::controlChange; break;
            default:   return std::nullopt;
        }
        return event;
    }
}
