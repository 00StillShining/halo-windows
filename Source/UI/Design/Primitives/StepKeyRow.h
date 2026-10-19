#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

namespace halo
{
    // A row of flat step keys (plan section 2.5): radius 3, 30 px, 2 px black
    // lip, paired shading; equal widths computed once from the full row.
    // Selected = ramp7 with black text. Disabled = no fill, a dead outline and
    // ramp5 text, with the reason printed beside the row by its page.
    // Click selects; the wheel moves the selection; left/right when focused.
    class StepKeyRow : public juce::Component
    {
    public:
        StepKeyRow();

        void setItems (const juce::StringArray&);
        const juce::StringArray& getItems() const noexcept { return items; }
        void setSelected (int index); // -1 = none; does not call onSelect
        int getSelected() const noexcept { return selected; }
        void setRowEnabled (bool);
        void setGap (float g) { gap = g; repaint(); }

        std::function<void (int)> onSelect;

        juce::Rectangle<float> keyBounds (int index) const;
        static constexpr int height = 30;

        void paint (juce::Graphics&) override;
        void mouseMove (const juce::MouseEvent&) override;
        void mouseExit (const juce::MouseEvent&) override;
        void mouseUp (const juce::MouseEvent&) override;
        void mouseWheelMove (const juce::MouseEvent&, const juce::MouseWheelDetails&) override;
        bool keyPressed (const juce::KeyPress&) override;
        void focusGained (FocusChangeType) override { repaint(); }
        void focusLost (FocusChangeType) override { repaint(); }

    private:
        int indexAt (juce::Point<float>) const;
        void choose (int index);

        juce::StringArray items;
        int selected = -1, hovered = -1;
        bool rowEnabled = true;
        float gap = 6.0f;
        float wheelAccumulator = 0.0f;
    };
}
