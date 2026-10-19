#pragma once

#include "UI/Design/WriteGate.h"

#include <juce_gui_basics/juce_gui_basics.h>

namespace halo
{
    // The confirm for every device write (docs/redesign/plan.md 3.13), in the
    // OP-1 field "POWER OFF?" manner: a 480 px paper card with the one hard
    // 3,3 px shadow, over a 70% scrim across the whole window; a crimson
    // header ("SEND TO EP–40?"), a pictogram row (file -> ep-40 -> slot or
    // pads), today's copy, the backup line it relies on (always present) and a
    // dot timeline for the time estimate.
    //   cancel   grey outline, default focus; Esc
    //   confirm  filled crimson; Enter confirms only after Tab moved focus to
    //            it (Enter on a fresh card cancels)
    // While open the gate is re-checked at 2 Hz: if it closes, the confirm
    // key goes dead and the reason shows as a blocker inside the card.
    class ConfirmCard : public juce::Component, private juce::Timer
    {
    public:
        ConfirmCard (WriteGate&, WriteSpec);
        ~ConfirmCard() override;

        void paint (juce::Graphics&) override;
        void resized() override;
        bool keyPressed (const juce::KeyPress&) override;
        void mouseUp (const juce::MouseEvent&) override;
        void parentSizeChanged() override { resized(); }

        // For tests and keyboard handling.
        void refreshGate();
        bool confirmIsLive() const noexcept { return gateOpen; }
        bool confirmHasFocus() const;
        void focusConfirm();
        void pressConfirm();
        void pressCancel();
        const WriteSpec& spec() const noexcept { return writeSpec; }
        juce::String shownReason() const { return gateOpen ? juce::String() : reasonText; }

    private:
        class Key;
        void timerCallback() override { refreshGate(); }
        juce::Rectangle<float> cardBounds() const;
        float contentHeight (float width) const;

        WriteGate& gate;
        WriteSpec writeSpec;
        std::unique_ptr<Key> cancelKey, confirmKey;
        bool gateOpen = true;
        bool focusOnConfirm = false; // Tab moves it; Enter acts on it
        juce::String reasonText;
    };
}
