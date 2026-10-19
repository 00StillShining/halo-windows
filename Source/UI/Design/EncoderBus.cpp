#include "UI/Design/EncoderBus.h"

namespace halo
{
    namespace
    {
        double nowSeconds() { return juce::Time::getMillisecondCounterHiRes() * 0.001; }
    }

    EncoderBus::EncoderBus()
    {
        startTimer (40); // gesture ends, card expiry
    }

    EncoderBus::~EncoderBus()
    {
        stopTimer();
    }

    void EncoderBus::changed()
    {
        listeners.call ([] (Listener& l) { l.encoderBusChanged(); });
    }

    void EncoderBus::setBindings (std::array<std::optional<EncoderBinding>, 4> b)
    {
        for (int i = 0; i < 4; ++i)
            endGestureIfAny (i);
        bindings = std::move (b);
        for (auto& a : accumulators)
            a.reset();
        touchIndex = -1;
        changed();
    }

    const EncoderBinding* EncoderBus::binding (int index) const
    {
        if (! juce::isPositiveAndBelow (index, 4) || ! bindings[(size_t) index].has_value())
            return nullptr;
        return &*bindings[(size_t) index];
    }

    bool EncoderBus::isLive (int index) const
    {
        const auto* b = binding (index);
        return b != nullptr && b->isAvailable();
    }

    void EncoderBus::touch (int index)
    {
        touchIndex = index;
        touchTime = nowSeconds();
    }

    bool EncoderBus::isTouched (int index) const
    {
        if (pinnedCard == index)
            return true;
        return index >= 0 && index == touchIndex && secondsSinceTouch() < cardHoldSeconds;
    }

    double EncoderBus::secondsSinceTouch() const
    {
        return touchIndex < 0 ? 1.0e9 : nowSeconds() - touchTime;
    }

    bool EncoderBus::showsNoShift (int index) const
    {
        return index == noShiftIndex && nowSeconds() - noShiftTime < cardHoldSeconds;
    }

    void EncoderBus::wheel (int index, const juce::MouseEvent& e, const juce::MouseWheelDetails& w)
    {
        if (binding (index) == nullptr)
            return;
        const double delta = (std::abs (w.deltaY) >= std::abs (w.deltaX) ? w.deltaY : -w.deltaX) * (w.isReversed ? -1.0 : 1.0);
        const int detents = accumulators[(size_t) index].feed (delta);
        if (detents == 0)
            return;
        const auto mods = e.mods;
        const auto speed = mods.isAltDown() ? encoder::Speed::fine
                         : mods.isCtrlDown() ? encoder::Speed::coarse
                                             : encoder::Speed::normal;
        turn (index, detents, speed, mods.isShiftDown() || shiftActive());
    }

    void EncoderBus::turn (int index, int detents, encoder::Speed speed, bool shiftLayer)
    {
        const auto* b = binding (index);
        if (b == nullptr || detents == 0 || ! b->isAvailable())
            return;

        if (shiftLayer)
        {
            // The shift layer turns the shift binding; it's never "fine".
            if (b->shiftTurn != nullptr)
            {
                b->shiftTurn (detents);
                touch (index);
            }
            else
            {
                noShiftIndex = index;
                noShiftTime = nowSeconds();
            }
            changed();
            return;
        }

        auto& gesture = gestures[(size_t) index];
        if (gesture.event (nowSeconds()) && b->beginGesture)
            b->beginGesture();

        const double norm = b->getNorm ? b->getNorm() : 0.0;
        double next = norm;
        if (b->steps > 0)
        {
            const int count = b->steps;
            const int at = (int) std::lround (norm * (count - 1));
            next = count > 1 ? (double) encoder::turnDiscrete (at, detents, count, speed) / (double) (count - 1) : 0.0;
        }
        else
        {
            next = encoder::turn (norm, detents, b->step, speed, b->fine, b->coarse);
        }
        if (b->setNorm)
            b->setNorm (next);
        touch (index);
        changed();
    }

    void EncoderBus::dragBegin (int index)
    {
        const auto* b = binding (index);
        if (b == nullptr || ! b->isAvailable())
            return;
        dragging[(size_t) index] = true;
        dragStart[(size_t) index] = b->getNorm ? b->getNorm() : 0.0;
        if (b->beginGesture)
            b->beginGesture();
    }

    void EncoderBus::dragTo (int index, double pixelsUp, bool fine)
    {
        const auto* b = binding (index);
        if (b == nullptr || ! dragging[(size_t) index])
            return;
        double next;
        if (b->steps > 0)
        {
            const int count = b->steps;
            const int start = (int) std::lround (dragStart[(size_t) index] * (count - 1));
            next = count > 1 ? (double) encoder::dragDiscrete (start, pixelsUp, count, fine) / (double) (count - 1) : 0.0;
        }
        else
        {
            next = encoder::drag (dragStart[(size_t) index], pixelsUp, fine);
        }
        if (b->setNorm && b->getNorm && std::abs (b->getNorm() - next) > 1.0e-9)
        {
            b->setNorm (next);
            touch (index);
            changed();
        }
    }

    void EncoderBus::dragEnd (int index)
    {
        if (! juce::isPositiveAndBelow (index, 4) || ! dragging[(size_t) index])
            return;
        dragging[(size_t) index] = false;
        if (const auto* b = binding (index); b != nullptr && b->endGesture)
            b->endGesture();
    }

    void EncoderBus::endGestureIfAny (int index)
    {
        auto& gesture = gestures[(size_t) index];
        if (gesture.active)
        {
            gesture.active = false;
            if (const auto* b = binding (index); b != nullptr && b->endGesture)
                b->endGesture();
        }
    }

    void EncoderBus::press (int index)
    {
        const auto* b = binding (index);
        if (b == nullptr || ! b->isAvailable())
            return;
        const bool shifted = shiftActive();
        if (shifted && b->shiftPress != nullptr)
            b->shiftPress();
        else if (! shifted && b->canPress())
            b->press();
        shiftLatched = false; // a latch lasts one press
        changed();
    }

    void EncoderBus::resetToDefault (int index)
    {
        const auto* b = binding (index);
        if (b == nullptr || ! b->isAvailable() || b->reset == nullptr)
            return;
        b->reset();
        touch (index);
        changed();
    }

    void EncoderBus::setFocused (int index)
    {
        focusIndex = juce::jlimit (-1, 3, index);
        changed();
    }

    bool EncoderBus::keyPressed (const juce::KeyPress& k)
    {
        const int code = k.getKeyCode();
        for (int i = 0; i < 4; ++i)
            if (code == juce::KeyPress::F1Key + i)
            {
                setFocused (focusIndex == i ? -1 : i);
                return true;
            }
        if (focusIndex < 0)
            return false;
        const auto mods = k.getModifiers();
        const auto speed = mods.isAltDown() ? encoder::Speed::fine
                         : mods.isCtrlDown() ? encoder::Speed::coarse
                                             : encoder::Speed::normal;
        if (code == juce::KeyPress::upKey || code == juce::KeyPress::downKey)
        {
            turn (focusIndex, code == juce::KeyPress::upKey ? 1 : -1, speed, mods.isShiftDown());
            return true;
        }
        if (code == juce::KeyPress::spaceKey || code == juce::KeyPress::returnKey)
        {
            press (focusIndex);
            return true;
        }
        if (code == juce::KeyPress::escapeKey)
        {
            setFocused (-1);
            return true;
        }
        return false;
    }

    void EncoderBus::setShiftHeld (bool held)
    {
        if (held == shiftHeld)
            return;
        shiftHeld = held;
        changed();
    }

    void EncoderBus::toggleShiftLatch()
    {
        shiftLatched = ! shiftLatched;
        changed();
    }

    void EncoderBus::setHovered (int index)
    {
        if (index == hoverIndex)
            return;
        hoverIndex = index;
        changed();
    }

    void EncoderBus::timerCallback()
    {
        const double now = nowSeconds();
        bool any = false;
        for (int i = 0; i < 4; ++i)
            if (gestures[(size_t) i].poll (now))
                if (const auto* b = binding (i); b != nullptr && b->endGesture)
                {
                    b->endGesture();
                    any = true;
                }
        // Let touched cells, the popup card and "no shift" cut back once their hold ends.
        const bool touchShowing = touchIndex >= 0 && secondsSinceTouch() < cardHoldSeconds;
        const bool noShiftShowing = noShiftIndex >= 0 && now - noShiftTime < cardHoldSeconds;
        if (touchShowing != wasTouchShowing || noShiftShowing != wasNoShiftShowing)
            any = true;
        wasTouchShowing = touchShowing;
        wasNoShiftShowing = noShiftShowing;
        if (any)
            changed();
    }
}
