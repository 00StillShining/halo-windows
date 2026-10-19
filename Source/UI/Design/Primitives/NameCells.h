#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

namespace halo
{
    // Eight boxed pixel-font cells for a sample name (plan section 2.5):
    // A-Z and 0-9 only, forced upper case (today's rule), 8 max. The pixel
    // size follows the physical scale (n x 11 physical px: n = 2 at 100% and
    // 125%, 3 at 150%) and every glyph lands on the device pixel grid, so the
    // cells stay crisp at any DPI. Click to edit; type, Backspace, Enter.
    class NameCells : public juce::Component
    {
    public:
        NameCells();

        void setText (const juce::String&);
        const juce::String& getText() const noexcept { return text; }
        std::function<void (const juce::String&)> onChange;

        static constexpr int maxLength = 8;
        static juce::String sanitise (const juce::String&);

        // Width of the cells for a given height (8 boxes and their gaps).
        static int widthForHeight (int height);

        void paint (juce::Graphics&) override;
        void mouseDown (const juce::MouseEvent&) override;
        bool keyPressed (const juce::KeyPress&) override;
        void focusGained (FocusChangeType) override { repaint(); }
        void focusLost (FocusChangeType) override { repaint(); }

    private:
        juce::String text;
    };
}
