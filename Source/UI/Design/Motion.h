#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

#include <cmath>
#include <limits>

namespace halo
{
    // One clock for everything that moves (docs/redesign/plan.md section 4.1):
    // a single VBlankAttachment that attaches while anything animates and
    // detaches once everything has settled. Animations implement tick(dt) and
    // return whether they're still moving. Timers remain only for slow polls
    // of cached state. Debug HALO_UI_FREEZE=1 pins time (dt = 0), so shots are
    // deterministic.
    class MotionClock
    {
    public:
        struct Animated
        {
            virtual ~Animated();
            // dt in seconds; return false once settled.
            virtual bool tick (double dt) = 0;
        };

        // The window's clock (owned by the window's content; null in tests).
        static MotionClock* instance() noexcept { return current; }

        explicit MotionClock (juce::Component& host);
        ~MotionClock();

        void wake (Animated*);   // starts ticking it (and attaches if needed)
        void forget (Animated*); // stops ticking it
        bool isFrozen() const noexcept { return frozen; }
        bool isAttached() const noexcept { return attachment != nullptr; }
        // Frames ticked since start (HALO_UI_STATS: 0 over 10 s when idle).
        juce::int64 framesTicked() const noexcept { return frames; }
        double now() const;      // seconds; pinned when frozen

    private:
        void frame (double timestamp);

        static MotionClock* current;
        juce::Component& host;
        std::unique_ptr<juce::VBlankAttachment> attachment;
        juce::Array<Animated*> active;
        double lastTimestamp = 0.0;
        bool frozen = false;
        juce::int64 frames = 0;
    };

    namespace motion
    {
        // Reduced motion (the Windows animation setting, or HALO_REDUCED_MOTION=1):
        // the machines stand still; meters, counters and state still move.
        bool reduced() noexcept;
        void setReduced (bool) noexcept;
    }

    // A reel or rotor with inertia (plan section 4.2): its speed eases toward
    // the target (spin-up 0.4 s, run-down 0.9 s), so tape starts and stops
    // like a machine. Under reduced motion it never turns.
    struct Spinner
    {
        double angle = 0.0, speed = 0.0; // radians, radians per second
        // Returns true while it still moves.
        bool step (double targetSpeed, double dt)
        {
            if (motion::reduced())
            {
                speed = 0.0;
                return false;
            }
            const double tau = targetSpeed > speed ? 0.4 : 0.9;
            speed += (targetSpeed - speed) * (dt <= 0.0 ? 0.0 : 1.0 - std::exp (-dt / tau));
            if (std::abs (speed) < 0.01 && targetSpeed == 0.0)
                speed = 0.0;
            angle = std::fmod (angle + speed * dt, juce::MathConstants<double>::twoPi * 1000.0);
            return speed != 0.0 || targetSpeed != 0.0;
        }
    };

    // Needle and LED ballistics (plan section 2.4): rise 10 ms, fall 300 ms,
    // peak hold 1.5 s then 20 dB/s. Works in dB.
    struct Ballistics
    {
        float value = -100.0f;
        float peak = -100.0f;
        double peakAge = 0.0;
        float riseSeconds = 0.010f, fallSeconds = 0.300f;

        // Returns true while still moving towards the target.
        bool step (float targetDb, double dt)
        {
            if (! std::isfinite (targetDb))
                targetDb = -100.0f;
            const float tau = targetDb > value ? riseSeconds : fallSeconds;
            const float k = dt <= 0.0 ? 0.0f : 1.0f - std::exp (-(float) dt / tau);
            value += (targetDb - value) * k;
            if (value > peak)
            {
                peak = value;
                peakAge = 0.0;
            }
            else
            {
                peakAge += dt;
                if (peakAge > 1.5)
                    peak = juce::jmax (value, peak - (float) (20.0 * dt));
            }
            return std::abs (targetDb - value) > 0.05f || peak > value + 0.05f;
        }
    };
}
