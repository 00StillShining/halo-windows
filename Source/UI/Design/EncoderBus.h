#pragma once

#include "Core/EncoderMath.h"

#include <juce_gui_basics/juce_gui_basics.h>

#include <array>
#include <optional>

namespace halo
{
    // What one of the four colour encoders turns on the current page
    // (docs/redesign/plan.md section 2.5). Values are normalised 0...1;
    // `steps` > 0 makes it discrete (QUALITY 5, CHANNELS 3, SLICES 5).
    struct EncoderBinding
    {
        juce::String label;                         // "LENGTH" (upper case, label type)
        std::function<double()> getNorm;
        std::function<void (double)> setNorm;
        std::function<void()> reset;                // double-click: the binding's default
        int steps = 0;
        double step = 0.01, fine = 0.0, coarse = 0.0; // per detent, in normalised units
        std::function<juce::String()> format;       // "4.0" (Halo Numerals)
        std::function<juce::String()> unit;         // "s"
        std::function<juce::String()> caption;      // gauge caption ("7 slices cut")
        std::function<juce::String()> hint;         // popup card hint ("wheel ±0.5 s · alt fine")

        juce::String pressLabel;                    // pill: "auto-trim"
        std::function<void()> press;
        juce::String shiftLabel;                    // shift layer: label of the shift turn
        std::function<void (int detents)> shiftTurn;
        juce::String shiftPressLabel;
        std::function<void()> shiftPress;

        std::function<void()> beginGesture, endGesture; // expensive commits wait for endGesture
        std::function<bool()> available;            // dead state...
        std::function<juce::String()> reason;       // ...and its printed reason

        // Optional tag beside the label ("AUTO-TRIM"), shown filled when on.
        std::function<juce::String()> tag;
        std::function<bool()> tagOn;
        // Optional gauge painter for the ParamCell (tick dial, dot row, ruler...).
        std::function<void (juce::Graphics&, juce::Rectangle<float>, juce::Colour line, bool live)> gauge;

        bool isAvailable() const { return available == nullptr || available(); }
        juce::String valueText() const { return format ? format() : juce::String(); }
        juce::String unitText() const { return unit ? unit() : juce::String(); }
    };

    // Routes the deck encoders, ParamCells, pills and any owned on-screen
    // region through one place, so the four views can never disagree.
    class EncoderBus : private juce::Timer
    {
    public:
        struct Listener
        {
            virtual ~Listener() = default;
            virtual void encoderBusChanged() = 0; // values, bindings, shift, hover or touch changed
        };

        EncoderBus();
        ~EncoderBus() override;

        void addListener (Listener* l) { listeners.add (l); }
        void removeListener (Listener* l) { listeners.remove (l); }

        // A page installs its four bindings (empty = a dead cell).
        void setBindings (std::array<std::optional<EncoderBinding>, 4>);
        const EncoderBinding* binding (int index) const;
        bool isLive (int index) const;

        // Input (mouse wheel / drag / click from any owned view).
        void wheel (int index, const juce::MouseEvent&, const juce::MouseWheelDetails&);
        void turn (int index, int detents, encoder::Speed, bool shiftLayer = false);
        void dragBegin (int index);
        void dragTo (int index, double pixelsUp, bool fine);
        void dragEnd (int index);
        void press (int index);
        void resetToDefault (int index);

        // Keyboard: F1-F4 focus an encoder, arrows turn it, Space/Enter press.
        bool keyPressed (const juce::KeyPress&);
        int focused() const noexcept { return focusIndex; }
        void setFocused (int index);

        // The shift layer: held (Shift key) or latched for one press (SHIFT on the deck).
        bool shiftActive() const noexcept { return shiftHeld || shiftLatched || shiftPinned; }
        bool shiftLatchedOnly() const noexcept { return shiftLatched && ! shiftHeld; }
        void setShiftHeld (bool);
        void toggleShiftLatch();

        // Hover links a view to its encoder (the cell's top bar turns hot, the
        // deck encoder shows a ring).
        void setHovered (int index);
        int hovered() const noexcept { return hoverIndex; }

        // The encoder last turned and when (for the popup card and touched cells).
        int touched() const noexcept { return touchIndex; }
        bool isTouched (int index) const;
        double secondsSinceTouch() const;
        // "no shift": a shift-turn on a binding without one.
        bool showsNoShift (int index) const;

        // Debug poses for shots (HALO_UI_POSE): pin the popup card or the shift layer.
        void pinCard (int index) { pinnedCard = index; changed(); }
        int pinnedCardIndex() const noexcept { return pinnedCard; }
        void pinShift (bool on) { shiftPinned = on; changed(); }

        static constexpr double cardHoldSeconds = 1.0;

    private:
        void timerCallback() override;
        void touch (int index);
        void changed();
        void endGestureIfAny (int index);

        std::array<std::optional<EncoderBinding>, 4> bindings;
        std::array<encoder::DetentAccumulator, 4> accumulators;
        std::array<encoder::GestureClock, 4> gestures;
        std::array<double, 4> dragStart {};
        std::array<bool, 4> dragging {};
        int focusIndex = -1, hoverIndex = -1, touchIndex = -1, noShiftIndex = -1, pinnedCard = -1;
        double touchTime = 0.0, noShiftTime = 0.0;
        bool shiftHeld = false, shiftLatched = false, shiftPinned = false;
        bool wasTouchShowing = false, wasNoShiftShowing = false;
        juce::ListenerList<Listener> listeners;
    };
}
