#pragma once

#include "App/AppModel.h"
#include "Core/UiLayout.h"
#include "UI/Design/EncoderBus.h"
#include "UI/Design/Primitives/DeckControls.h"

#include <juce_gui_basics/juce_gui_basics.h>

namespace halo
{
    // The graphite deck under the glass (docs/redesign/plan.md sections 2.3,
    // 3.1, 3.3): seven mode keys whose LEDs report state, SHIFT (held = the
    // shift layer, click = latch for one press), REC (Ctrl+R; dead with its
    // reason when monitoring is off), the four colour encoders centred under
    // their cells, the speaker grille, the VU window (the same `out` meters as
    // PLAY, so they're visible in every mode) and the nameplate.
    class Deck : public juce::Component, private AppModel::Listener, private EncoderBus::Listener, private juce::Timer
    {
    public:
        Deck (AppModel&, EncoderBus&);
        ~Deck() override;

        // Lays the deck's children out from the window frame (window coords).
        void setFrame (const layout::Frame&);

        // The hovered key's legend and shortcut, for the soft-key hint at compact sizes.
        std::function<void (const juce::String&)> onHint;
        juce::String recReason() const;

        void paint (juce::Graphics&) override;

    private:
        void statusChanged() override { updateLeds(); }
        void encoderBusChanged() override { updateShift(); }
        void timerCallback() override;
        void updateLeds();
        void updateShift();
        juce::Rectangle<int> local (const layout::Rect&) const;

        AppModel& model;
        EncoderBus& bus;
        layout::Frame frame;
        juce::OwnedArray<DeckKey> modeKeys;
        DeckKey shiftKey { "shift", Pict::none, {}, DeckKey::Style::shift };
        DeckKey recKey { "rec ctrl r", Pict::rec, {}, DeckKey::Style::rec };
        juce::OwnedArray<DeckEncoder> encoders;
        StereoLevels levels;
        bool blink = true, metersLive = false;
        int tick = 0;
    };
}
