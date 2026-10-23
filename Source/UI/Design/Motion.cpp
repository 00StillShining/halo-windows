#include "UI/Design/Motion.h"

#include <atomic>

namespace halo
{
    MotionClock* MotionClock::current = nullptr;

    MotionClock::Animated::~Animated()
    {
        if (auto* clock = MotionClock::instance())
            clock->forget (this);
    }

    MotionClock::MotionClock (juce::Component& h) : host (h)
    {
       #if JUCE_DEBUG
        frozen = juce::SystemStats::getEnvironmentVariable ("HALO_UI_FREEZE", {}) == "1";
       #endif
        if (current == nullptr)
            current = this;
    }

    MotionClock::~MotionClock()
    {
        attachment.reset();
        if (current == this)
            current = nullptr;
    }

    double MotionClock::now() const
    {
        return frozen ? 0.0 : juce::Time::getMillisecondCounterHiRes() * 0.001;
    }

    void MotionClock::wake (Animated* a)
    {
        if (a == nullptr)
            return;
        active.addIfNotAlreadyThere (a);
        if (attachment == nullptr)
        {
            lastTimestamp = 0.0;
            attachment = std::make_unique<juce::VBlankAttachment> (&host, [this] (double t) { frame (t); });
        }
    }

    void MotionClock::forget (Animated* a)
    {
        active.removeAllInstancesOf (a);
    }

    void MotionClock::frame (double timestamp)
    {
        const double dt = frozen || lastTimestamp <= 0.0 ? (frozen ? 0.0 : 1.0 / 60.0)
                                                         : juce::jlimit (0.0, 0.1, timestamp - lastTimestamp);
        lastTimestamp = timestamp;
        ++frames;
        for (int i = active.size(); --i >= 0;)
            if (i < active.size() && ! active.getUnchecked (i)->tick (dt))
                active.remove (i);
        // Settled: detach, so an idle window gets no vblank callbacks at all.
        if (active.isEmpty())
            juce::MessageManager::callAsync ([this, alive = juce::Component::SafePointer<juce::Component> (&host)]
            {
                if (alive != nullptr && active.isEmpty())
                    attachment.reset();
            });
    }

    namespace motion
    {
        namespace { std::atomic<bool> reducedFlag { false }; }
        bool reduced() noexcept { return reducedFlag.load (std::memory_order_relaxed); }
        void setReduced (bool on) noexcept { reducedFlag.store (on, std::memory_order_relaxed); }
    }
}
