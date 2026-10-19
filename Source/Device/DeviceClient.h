#pragma once

#include "Core/TeProtocol.h"

#include <juce_events/juce_events.h>

#include <deque>
#include <functional>

namespace halo
{
    // Request/response transport for TE's protocol. One request in flight at a
    // time (the device's FILE subsystem is single-threaded and wedges if
    // sessions overlap), matched by request id, with timeouts. Interim replies
    // (status 64) restart the timeout and keep waiting for the final status.
    // Message thread only.
    class DeviceClient : private juce::Timer
    {
    public:
        using Send = std::function<bool (const te::Bytes&, const juce::String& note)>;
        // Called with the final response, or nullopt on timeout / send failure.
        using Reply = std::function<void (const std::optional<te::Frame>&)>;

        explicit DeviceClient (Send);
        ~DeviceClient() override;

        void setDeviceId (std::uint8_t id) noexcept { deviceId = id; }
        std::uint8_t getDeviceId() const noexcept { return deviceId; }

        void request (std::uint8_t command, te::Bytes payload, Reply done, int timeoutMs = 2000,
                      const juce::String& note = {});

        // Feed every incoming SysEx message here.
        void handle (const te::Bytes&);

        // Drop everything (disconnect). Pending callbacks get nullopt.
        void reset();

        bool isBusy() const noexcept { return inFlight.has_value() || ! queue.empty(); }

        std::function<void (const te::Frame&)> onEvent;
        std::function<void (const std::string&)> onDebug; // firmware error text: stop FILE traffic

    private:
        struct Pending
        {
            std::uint8_t command;
            te::Bytes payload;
            Reply done;
            int timeoutMs;
            juce::String note;
            int requestId = -1;
            double deadline = 0.0;
        };

        void pump();
        void timerCallback() override;
        void finish (const std::optional<te::Frame>&);

        Send send;
        std::uint8_t deviceId = 0x3C;
        int nextRequestId;
        std::deque<Pending> queue;
        std::optional<Pending> inFlight;
    };
}
