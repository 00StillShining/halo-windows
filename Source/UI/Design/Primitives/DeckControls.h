#pragma once

#include "UI/Design/EncoderBus.h"
#include "UI/Design/Pictograms.h"

#include <juce_gui_basics/juce_gui_basics.h>

namespace halo
{
    // The graphite deck's controls (docs/redesign/plan.md sections 2.5, 3.3).

    // A low-profile pictogram key: cap with a 2 px lip and 1 px of press
    // travel, an LED top-left, the ^n shortcut printed bottom-right on the cap
    // (chassis micro-print, also printed in the soft-key hint) and a lowercase
    // legend below. Compact sizes drop the legend (pictogram-only caps).
    class DeckKey : public juce::Button
    {
    public:
        enum class Led { off, on, dim, crimson, hollow, blink };
        enum class Style { mode, shift, rec };

        DeckKey (const juce::String& legend, Pict, const juce::String& shortcut, Style = Style::mode);

        void setActive (bool);        // the active mode: cap pressed, LED on
        void setLed (Led);
        void setCapSize (int px) { capSize = px; repaint(); }
        void setShowLegend (bool b) { showLegend = b; repaint(); }
        void setBlinkPhase (bool on) { blinkOn = on; if (led == Led::blink) repaint(); }
        const juce::String& legend() const noexcept { return legendText; }
        const juce::String& shortcut() const noexcept { return shortcutText; }

        static int heightFor (int capSize, bool legend) { return capSize + (legend ? 20 : 0); }
        void paintButton (juce::Graphics&, bool highlighted, bool down) override;

    private:
        juce::String legendText, shortcutText;
        Pict icon;
        Style style;
        Led led = Led::off;
        bool active = false, showLegend = true, blinkOn = true;
        int capSize = 44;
    };

    // A knurled colour encoder on the deck: graphite skirt with 48 ticks, a
    // cap in the encoder's colour, an off-white pointer that follows the value
    // with no easing, and its parameter name printed below with a colour dot.
    // Wheel = turn (alt fine, ctrl coarse, shift = shift layer), vertical drag
    // (180 px = range; alt or right-drag = fine), click = press, double-click =
    // reset. Dims to 40% with a "—" name when its binding is unavailable.
    class DeckEncoder : public juce::Component, private EncoderBus::Listener
    {
    public:
        DeckEncoder (EncoderBus&, int index);
        ~DeckEncoder() override;

        void setDiameter (int d) { diameter = d; repaint(); }
        void setShowName (bool b) { showName = b; repaint(); }
        static int hitSize (int diameter) { return juce::jmax (72, diameter + 16); }

        void paint (juce::Graphics&) override;
        void mouseEnter (const juce::MouseEvent&) override;
        void mouseExit (const juce::MouseEvent&) override;
        void mouseDown (const juce::MouseEvent&) override;
        void mouseDrag (const juce::MouseEvent&) override;
        void mouseUp (const juce::MouseEvent&) override;
        void mouseDoubleClick (const juce::MouseEvent&) override;
        void mouseWheelMove (const juce::MouseEvent&, const juce::MouseWheelDetails&) override;

        // The cap drawing, shared with the PopupCard's cylinder.
        static void paintKnob (juce::Graphics&, juce::Point<float> centre, float diameter, int encoderIndex,
                               double norm, bool live, bool hovered, bool focused);

    private:
        void encoderBusChanged() override { repaint(); }

        EncoderBus& bus;
        const int index;
        int diameter = 56;
        bool showName = true, dragged = false;
    };
}
