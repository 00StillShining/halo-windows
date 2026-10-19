#pragma once

#include "UI/Design/EncoderBus.h"

#include <juce_gui_basics/juce_gui_basics.h>

namespace halo
{
    // The parameter strip's pieces (docs/redesign/plan.md section 2.5).
    enum class CellSize { regular, compact, ultra }; // 86 / 72 / 64 px tall

    // One encoder's cell on the glass: a 3 px top bar, lamp and label, the
    // value in Halo Numerals with its unit, a gauge, and a 2 px value bar
    // along the bottom, all in the encoder's colour. The wheel works on the
    // cell itself. Touched (or hovered): the top bar turns hot and the label
    // inverts to a filled tag. Unavailable: everything dead, the value reads
    // "–" and a one-line reason is printed.
    class ParamCell : public juce::Component, private EncoderBus::Listener
    {
    public:
        ParamCell (EncoderBus&, int index);
        ~ParamCell() override;

        void setSize (CellSize s) { size = s; repaint(); }
        static int heightFor (CellSize s) { return s == CellSize::regular ? 86 : s == CellSize::compact ? 72 : 64; }

        void paint (juce::Graphics&) override;
        void mouseEnter (const juce::MouseEvent&) override;
        void mouseExit (const juce::MouseEvent&) override;
        void mouseDown (const juce::MouseEvent&) override;
        void mouseDrag (const juce::MouseEvent&) override;
        void mouseUp (const juce::MouseEvent&) override;
        void mouseDoubleClick (const juce::MouseEvent&) override;
        void mouseWheelMove (const juce::MouseEvent&, const juce::MouseWheelDetails&) override;

    private:
        void encoderBusChanged() override { repaint(); }

        EncoderBus& bus;
        const int index;
        CellSize size = CellSize::regular;
        bool dragged = false;
    };

    // A read-only cell: ramp-filled (ramp1/3/5/7), small lowercase label
    // top-left, value bottom-right; it ignores the wheel and has no top bar
    // (the form that says "not turnable").
    namespace readout
    {
        void paint (juce::Graphics&, juce::Rectangle<float>, int rampIndex, const juce::String& label,
                    const juce::String& value, bool numerals = true, float capHeight = 26.0f);
    }

    // The soft-key row between the strip and the deck: a hint on the left and
    // one pill per encoder, centred under it. A pill is filled when its
    // encoder has a press, a dim empty capsule when it has none. The shift
    // layer swaps the labels.
    class SoftKeyRow : public juce::Component, private EncoderBus::Listener
    {
    public:
        explicit SoftKeyRow (EncoderBus&);
        ~SoftKeyRow() override;

        void setCentres (std::array<float, 4> xCentres) { centres = xCentres; repaint(); }
        void setHint (const juce::String& h) { if (h != hint) { hint = h; repaint(); } }
        void setHintWidth (float w) { hintWidth = w; repaint(); }
        void setPillSize (float w, float h) { pillW = w; pillH = h; repaint(); }

        void paint (juce::Graphics&) override;
        void mouseMove (const juce::MouseEvent&) override;
        void mouseExit (const juce::MouseEvent&) override;
        void mouseDown (const juce::MouseEvent&) override;
        void mouseUp (const juce::MouseEvent&) override;

    private:
        void encoderBusChanged() override { repaint(); }
        juce::Rectangle<float> pillBounds (int i) const;
        int pillAt (juce::Point<float>) const;

        EncoderBus& bus;
        std::array<float, 4> centres {};
        juce::String hint;
        float hintWidth = 480.0f, pillW = 120.0f, pillH = 24.0f;
        int hoverPill = -1, downPill = -1;
    };

    // The pop-up value card, docked in the lane while an encoder turns: an
    // off-white paper card with a header block in the encoder's fill colour,
    // a black value box (Numerals L), a drawing of the encoder's cap at its
    // current angle, and a hint. Shows within 80 ms, holds 1 s, then cuts.
    namespace popupCard
    {
        void paint (juce::Graphics&, juce::Rectangle<float>, const EncoderBinding&, int encoderIndex, bool compact);
    }
}
