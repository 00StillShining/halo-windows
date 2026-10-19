#pragma once

#include <algorithm>
#include <cmath>

// The encoder input contract (docs/redesign/plan.md sections 2.5 and U19), as
// pure maths so it can be tested without JUCE:
//   wheel        1 detent per notch (touchpads accumulate)
//   alt + wheel  fine, 1/10
//   ctrl + wheel coarse, x10
//   drag         180 px = the full range; alt or right-button drag = fine
//   gesture end  250 ms after the last wheel detent (expensive commits wait for it)
namespace halo::encoder
{
    // One Windows wheel notch, in JUCE's MouseWheelDetails units (120 / 256).
    inline constexpr double kNotch = 120.0 / 256.0;
    // A turn registers once the wheel has moved this far (in notches), so
    // touchpads with small deltas still step, and one notch is one detent.
    inline constexpr double kThreshold = 0.85;
    inline constexpr double kDragFullRangePx = 180.0;
    inline constexpr double kGestureEndSeconds = 0.25;

    enum class Speed { normal, fine, coarse };

    // Accumulates wheel deltas (JUCE units, + = up = clockwise) into whole detents.
    struct DetentAccumulator
    {
        double residual = 0.0; // in notches

        int feed (double wheelDelta)
        {
            residual += wheelDelta / kNotch;
            int detents = 0;
            while (residual >= kThreshold)  { ++detents; residual -= 1.0; }
            while (residual <= -kThreshold) { --detents; residual += 1.0; }
            // Never let a remainder build up across many slow turns.
            residual = std::clamp (residual, -kThreshold, kThreshold);
            return detents;
        }

        void reset() { residual = 0.0; }
    };

    // Step for one detent at a speed (fine = step / 10, coarse = step x 10,
    // unless the binding names its own fine and coarse steps).
    constexpr double stepFor (Speed s, double step, double fine = 0.0, double coarse = 0.0)
    {
        switch (s)
        {
            case Speed::fine:   return fine > 0.0 ? fine : step / 10.0;
            case Speed::coarse: return coarse > 0.0 ? coarse : step * 10.0;
            case Speed::normal:
            default:            return step;
        }
    }

    // A continuous parameter in normalised units (0...1).
    inline double turn (double norm, int detents, double step, Speed speed = Speed::normal, double fine = 0.0, double coarse = 0.0)
    {
        return std::clamp (norm + (double) detents * stepFor (speed, step, fine, coarse), 0.0, 1.0);
    }

    // A discrete parameter (count positions). Fine moves one position like
    // normal (there's nothing finer than one step); coarse jumps to the ends.
    inline int turnDiscrete (int index, int detents, int count, Speed speed = Speed::normal)
    {
        if (count <= 0)
            return 0;
        if (speed == Speed::coarse && detents != 0)
            return detents > 0 ? count - 1 : 0;
        return std::clamp (index + detents, 0, count - 1);
    }

    // Vertical drag: 180 px = the full range; fine = 1/10. pixelsUp > 0 raises.
    inline double drag (double startNorm, double pixelsUp, bool fine)
    {
        return std::clamp (startNorm + pixelsUp / kDragFullRangePx * (fine ? 0.1 : 1.0), 0.0, 1.0);
    }

    // Discrete drag: the same travel spread over the positions.
    inline int dragDiscrete (int startIndex, double pixelsUp, int count, bool fine)
    {
        if (count <= 1)
            return 0;
        const double perStep = kDragFullRangePx / (double) (count - 1) * (fine ? 10.0 : 1.0);
        return std::clamp (startIndex + (int) std::lround (pixelsUp / perStep), 0, count - 1);
    }

    // Wheel gestures: begin on the first detent, end 250 ms after the last.
    struct GestureClock
    {
        bool active = false;
        double lastEvent = 0.0;

        // Returns true when this event begins a new gesture.
        bool event (double nowSeconds)
        {
            const bool begins = ! active;
            active = true;
            lastEvent = nowSeconds;
            return begins;
        }

        // Returns true once, when the gesture has ended by now.
        bool poll (double nowSeconds)
        {
            if (active && nowSeconds - lastEvent >= kGestureEndSeconds)
            {
                active = false;
                return true;
            }
            return false;
        }
    };

    // Pointer angle for a normalised value: -135 deg (min) to +135 deg (max),
    // 0 = straight up, clockwise positive, in radians.
    inline double pointerAngle (double norm)
    {
        constexpr double pi = 3.14159265358979323846;
        return (std::clamp (norm, 0.0, 1.0) * 270.0 - 135.0) * pi / 180.0;
    }
}
