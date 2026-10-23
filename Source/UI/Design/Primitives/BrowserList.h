#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

namespace halo
{
    // A three-column browser (docs/redesign/plan.md section 3.9, OP-XY lists):
    // categories on the left, the rows in the middle, and a facts column for
    // the selected row on the right, painted by the owner. Selection is a
    // filled ramp7 row with black text; keyboard focus is an outline. Wheel
    // scrolls; up / down / page / home / end move the selection.
    class BrowserList : public juce::Component
    {
    public:
        struct Row
        {
            juce::String number;  // "002"
            juce::String name;    // "BIGKICK"
            juce::String facts;   // "0.50 s  m"
        };

        BrowserList();

        void setCategories (const juce::StringArray&);
        void setCategory (int index);      // 0 = the first entry ("ALL")
        int getCategory() const noexcept { return category; }

        void setRows (std::vector<Row>);
        int rowCount() const noexcept { return (int) rows.size(); }
        void setSelected (int row, bool scrollTo = true); // does not call onSelect
        int getSelected() const noexcept { return selected; }
        void setColumnWidths (float categoryWidth, float factsWidth);
        void setEmptyText (const juce::String& t) { emptyText = t; repaint(); }

        std::function<void (int)> onCategory;     // a category was clicked
        std::function<void (int)> onSelect;       // the selection moved (click or keys)
        std::function<void (int)> onActivate;     // double-click / enter
        std::function<void (juce::Graphics&, juce::Rectangle<float>)> paintFacts;

        static constexpr float rowHeight = 30.0f, categoryRowHeight = 26.0f;

        void paint (juce::Graphics&) override;
        void mouseDown (const juce::MouseEvent&) override;
        void mouseDoubleClick (const juce::MouseEvent&) override;
        void mouseWheelMove (const juce::MouseEvent&, const juce::MouseWheelDetails&) override;
        bool keyPressed (const juce::KeyPress&) override;
        void focusGained (FocusChangeType) override { repaint(); }
        void focusLost (FocusChangeType) override { repaint(); }

        juce::Rectangle<float> categoryColumn() const;
        juce::Rectangle<float> rowColumn() const;
        juce::Rectangle<float> factsColumn() const;
        void moveSelection (int delta);

    private:
        int visibleRows() const;
        void ensureVisible (int row);
        void clampScroll();

        juce::StringArray categories;
        std::vector<Row> rows;
        int category = 0, selected = -1, firstRow = 0;
        float categoryW = 160.0f, factsW = 200.0f, wheelAcc = 0.0f;
        juce::String emptyText { "no sounds" };
    };
}
