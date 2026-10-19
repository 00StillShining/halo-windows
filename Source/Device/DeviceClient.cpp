#include "Device/DeviceClient.h"

namespace halo
{
    namespace
    {
        double nowMs() { return juce::Time::getMillisecondCounterHiRes(); }
    }

    DeviceClient::DeviceClient (Send s)
        : send (std::move (s)), nextRequestId (juce::Random::getSystemRandom().nextInt (4095))
    {
        startTimer (50);
    }

    DeviceClient::~DeviceClient()
    {
        stopTimer();
    }

    void DeviceClient::request (std::uint8_t command, te::Bytes payload, Reply done, int timeoutMs, const juce::String& note)
    {
        queue.push_back ({ command, std::move (payload), std::move (done), timeoutMs, note });
        pump();
    }

    void DeviceClient::pump()
    {
        if (inFlight || queue.empty())
            return;

        inFlight = std::move (queue.front());
        queue.pop_front();
        inFlight->requestId = nextRequestId;
        nextRequestId = (nextRequestId + 1) % 4096;
        inFlight->deadline = nowMs() + inFlight->timeoutMs;

        const auto frame = te::buildRequest (deviceId, inFlight->requestId, inFlight->command, inFlight->payload);
        if (frame.empty() || ! send (frame, inFlight->note))
            finish (std::nullopt);
    }

    void DeviceClient::finish (const std::optional<te::Frame>& result)
    {
        auto done = std::move (inFlight->done);
        inFlight.reset();
        if (done)
            done (result);
        pump();
    }

    void DeviceClient::handle (const te::Bytes& message)
    {
        const auto frame = te::parseFrame (message);
        if (! frame)
            return;

        if (frame->kind == te::Frame::Kind::debug)
        {
            if (onDebug)
                onDebug (frame->text);
            return;
        }
        if (frame->kind == te::Frame::Kind::event)
        {
            if (onEvent)
                onEvent (*frame);
            return;
        }
        if (! inFlight || frame->requestId != inFlight->requestId || frame->command != inFlight->command)
            return; // late or stray reply

        if (frame->status == te::interim)
        {
            inFlight->deadline = nowMs() + inFlight->timeoutMs;
            return;
        }
        finish (frame);
    }

    void DeviceClient::reset()
    {
        auto pending = std::move (queue);
        queue.clear();
        if (inFlight)
        {
            auto done = std::move (inFlight->done);
            inFlight.reset();
            if (done)
                done (std::nullopt);
        }
        for (auto& p : pending)
            if (p.done)
                p.done (std::nullopt);
    }

    void DeviceClient::timerCallback()
    {
        if (inFlight && nowMs() > inFlight->deadline)
            finish (std::nullopt);
    }
}
