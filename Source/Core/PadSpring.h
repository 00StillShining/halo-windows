#pragma once

#include <algorithm>
#include <cmath>

// A pad's travel (plan M10, section 6): a damped spring toward the target
// depth, integrated in fixed 0.5 ms substeps so 30 fps and 144 fps frames
// trace the same curve. Pressing is critically damped (down in ~40 ms, no
// bounce); releasing is slightly under-damped, so the cap overshoots its rest
// by a barely perceptible few percent of its travel before settling. The
// depth never depends on velocity: a key has one travel.
//
// Pure logic, no JUCE: Tests/PadSpringTests.cpp covers it.
namespace halo
{
    struct PadSpring
    {
        float depth = 0.0f;     // metres below rest (positive = pressed in)
        float speed = 0.0f;     // metres per second

        static constexpr float substep = 0.0005f;
        static constexpr float pressOmega = 160.0f, pressZeta = 1.0f;     // rad/s
        static constexpr float releaseOmega = 110.0f, releaseZeta = 0.72f;

        void step (float target, float dt)
        {
            dt = std::clamp (dt, 0.0f, 0.1f);
            while (dt > 0.0f)
            {
                const float h = std::min (dt, substep);
                // Toward a deeper target: pressing; otherwise the cap springs back.
                const bool pressing = target > depth;
                const float w = pressing ? pressOmega : releaseOmega;
                const float z = pressing ? pressZeta : releaseZeta;
                const float accel = w * w * (target - depth) - 2.0f * z * w * speed;
                speed += accel * h;   // semi-implicit Euler: stable at this substep
                depth += speed * h;
                dt -= h;
            }
        }

        bool settled (float target) const noexcept
        {
            return std::abs (depth - target) < 1.0e-5f && std::abs (speed) < 1.0e-3f;
        }
    };
}
