#pragma once

#include "UI/Design/EncoderBus.h"
#include "UI/Design/Primitives/ParamStrip.h"

#include <juce_gui_basics/juce_gui_basics.h>

namespace halo
{
    // The lane, left of the parameter strip (docs/redesign/plan.md section
    // 2.5, U4). It only ever holds redundant or hint content (a clock
    // readout, keycap hints, a copy of a status line), so the popup value
    // card can dock here while an encoder turns without hiding anything a
    // user must see: blockers, armed instructions and reasons live in the
    // page header, beside their control or in a notice.
    class Lane : public juce::Component, private EncoderBus::Listener
    {
    public:
        explicit Lane (EncoderBus&);
        ~Lane() override;

        void setCompact (bool c) { compact = c; repaint(); }
        // The page's own (redundant) lane content, painted when no card shows.
        void setContent (std::function<void (juce::Graphics&, juce::Rectangle<float>)> painter)
        {
            content = std::move (painter);
            repaint();
        }
        bool showingCard() const;

        void paint (juce::Graphics&) override;

    private:
        void encoderBusChanged() override { repaint(); }
        int cardIndex() const;

        EncoderBus& bus;
        std::function<void (juce::Graphics&, juce::Rectangle<float>)> content;
        bool compact = false;
    };
}
