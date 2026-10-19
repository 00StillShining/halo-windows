#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

#include <array>

namespace halo
{
    // The EP-40's 3x4 pad layout (7 8 9 / 4 5 6 / 1 2 3 / . 0 enter) as step
    // keys with paired row shading (docs/redesign/plan.md section 2.5). Each
    // pad shows its device legend top-left and a payload: a slice numeral
    // (Halo Numerals) or a sound name on two lines.
    //   start pad      white LED + 2 px ink outline
    //   selected       ramp7 fill, black payload, a play mark
    //   trimmed        a 20 x 5 hatched chip with an orange edge, bottom-right
    //   empty          outline only, "empty"
    //   assign-armed   every pad gets a dashed hot outline and a +
    //   hit            a 90 ms flash
    // Row height is computed from the region: (h - 3 gaps) / 4, clamped 48-100.
    class PadGrid : public juce::Component
    {
    public:
        struct Pad
        {
            juce::String numeral;     // "12" (slice) - or -
            juce::String name;        // "BIGKICK" (sound)
            juce::String corner;      // top-right small text (slot number)
            bool empty = false, start = false, selected = false, trimmed = false;
        };

        PadGrid();

        void setPads (const std::array<Pad, 12>&);
        void setArmed (bool);
        void flash (int gridIndex);

        std::function<void (int gridIndex)> onPadClicked;
        std::function<void (int detents)> onWheel;

        static int rowHeightFor (int regionHeight, int gap = 8);
        static constexpr int gap = 8;

        void paint (juce::Graphics&) override;
        void mouseMove (const juce::MouseEvent&) override;
        void mouseExit (const juce::MouseEvent&) override;
        void mouseUp (const juce::MouseEvent&) override;
        void mouseWheelMove (const juce::MouseEvent&, const juce::MouseWheelDetails&) override;

        juce::Rectangle<float> padBounds (int gridIndex) const;

    private:
        std::array<Pad, 12> pads {};
        bool armed = false;
        int hovered = -1, flashing = -1;
        double flashUntil = 0.0;
        float wheelAcc = 0.0f;
    };
}
