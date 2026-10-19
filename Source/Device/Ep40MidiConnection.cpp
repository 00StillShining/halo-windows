#include "Device/Ep40MidiConnection.h"

namespace halo
{
    Ep40MidiConnection::Ep40MidiConnection() = default;

    Ep40MidiConnection::~Ep40MidiConnection()
    {
        cancelPendingUpdate();
        if (input != nullptr)
            input->stop();
    }

    bool Ep40MidiConnection::isEp40Name (const juce::String& name)
    {
        const auto lower = name.toLowerCase();
        return lower.contains ("ep-40") || lower.contains ("ep40") || lower.contains ("riddim");
    }

    void Ep40MidiConnection::start()
    {
        if (started)
            return;
        started = true;
        deviceListConnection = juce::MidiDeviceListConnection::make ([this] { rescan(); });
        rescan();
    }

    bool Ep40MidiConnection::sendSysex (const std::uint8_t* bytes, std::size_t size)
    {
        if (output == nullptr || size < 2 || bytes[0] != 0xF0 || bytes[size - 1] != 0xF7)
            return false;
        output->sendMessageNow (juce::MidiMessage (bytes, (int) size));
        return true;
    }

    void Ep40MidiConnection::rescan()
    {
        const auto devices = juce::MidiInput::getAvailableDevices();

        if (input != nullptr)
        {
            const bool stillPresent = std::any_of (devices.begin(), devices.end(),
                                                   [this] (const auto& d) { return d.identifier == openIdentifier; });
            if (stillPresent)
                return;

            input->stop();
            input.reset();
            output.reset();
            openIdentifier.clear();
            openOutputIdentifier.clear();
        }

        for (const auto& device : devices)
        {
            if (! isEp40Name (device.name))
                continue;

            if (auto opened = juce::MidiInput::openDevice (device.identifier, this))
            {
                input = std::move (opened);
                openIdentifier = device.identifier;
                fifo.reset();

                // The matching output port, for SysEx requests. If it can't be
                // opened, live MIDI still works; device library features won't.
                for (const auto& out : juce::MidiOutput::getAvailableDevices())
                    if (isEp40Name (out.name))
                        if ((output = juce::MidiOutput::openDevice (out.identifier)) != nullptr)
                        {
                            openOutputIdentifier = out.identifier;
                            break;
                        }

                input->start();
                setStatus (Status::connected, device.name);
                return;
            }

            // Windows MIDI ports are single-client: a DAW or TE's own tool can
            // hold the EP-40. Say so instead of pretending it isn't there.
            setStatus (Status::busy, device.name);
            return;
        }

        setStatus (Status::searching, {});
    }

    void Ep40MidiConnection::setStatus (Status status, const juce::String& name)
    {
        if (status == currentStatus && name == currentName)
            return;
        currentStatus = status;
        currentName = name;
        if (onStatusChanged)
            onStatusChanged (status, name);
    }

    void Ep40MidiConnection::handleIncomingMidiMessage (juce::MidiInput*, const juce::MidiMessage& message)
    {
        if (message.isSysEx())
        {
            {
                const juce::ScopedLock sl (sysexLock);
                sysexQueue.emplace_back (message.getRawData(), (size_t) message.getRawDataSize());
            }
            triggerAsyncUpdate();
            return;
        }

        const auto parsed = parseMidi (message.getRawData(), (std::size_t) message.getRawDataSize(),
                                       message.getTimeStamp());
        if (! parsed)
            return;

        if (fifo.getFreeSpace() < 1)
        {
            overflowed.store (true, std::memory_order_release);
            triggerAsyncUpdate();
            return;
        }

        const auto scope = fifo.write (1);
        if (scope.blockSize1 > 0)
            buffer[(std::size_t) scope.startIndex1] = *parsed;
        else
            buffer[(std::size_t) scope.startIndex2] = *parsed;

        triggerAsyncUpdate();
    }

    void Ep40MidiConnection::handleAsyncUpdate()
    {
        if (overflowed.exchange (false, std::memory_order_acquire) && onOverflow)
            onOverflow();

        const auto scope = fifo.read (fifo.getNumReady());
        auto deliver = [this] (int start, int count)
        {
            for (int i = 0; i < count; ++i)
                if (onEvent)
                    onEvent (buffer[(std::size_t) (start + i)]);
        };
        deliver (scope.startIndex1, scope.blockSize1);
        deliver (scope.startIndex2, scope.blockSize2);

        std::vector<juce::MemoryBlock> sysex;
        {
            const juce::ScopedLock sl (sysexLock);
            sysex.swap (sysexQueue);
        }
        for (const auto& m : sysex)
            if (onSysex)
                onSysex (m);
    }
}
