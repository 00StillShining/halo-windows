#include "UI/Design/Primitives/BrowserList.h"
#include "Core/EncoderMath.h"
#include "UI/Design/Paint.h"

namespace halo
{
    BrowserList::BrowserList()
    {
        setWantsKeyboardFocus (true);
    }

    void BrowserList::setCategories (const juce::StringArray& c)
    {
        if (c == categories)
            return;
        categories = c;
        category = juce::jlimit (0, juce::jmax (0, categories.size() - 1), category);
        repaint();
    }

    void BrowserList::setCategory (int index)
    {
        index = juce::jlimit (0, juce::jmax (0, categories.size() - 1), index);
        if (index == category)
            return;
        category = index;
        repaint();
    }

    void BrowserList::setRows (std::vector<Row> r)
    {
        rows = std::move (r);
        selected = juce::jlimit (-1, (int) rows.size() - 1, selected);
        clampScroll();
        repaint();
    }

    void BrowserList::setSelected (int row, bool scrollTo)
    {
        row = juce::jlimit (-1, (int) rows.size() - 1, row);
        selected = row;
        if (scrollTo && row >= 0)
            ensureVisible (row);
        repaint();
    }

    void BrowserList::setColumnWidths (float c, float f)
    {
        categoryW = c;
        factsW = f;
        repaint();
    }

    juce::Rectangle<float> BrowserList::categoryColumn() const
    {
        return getLocalBounds().toFloat().withWidth (categoryW);
    }

    juce::Rectangle<float> BrowserList::factsColumn() const
    {
        const auto b = getLocalBounds().toFloat();
        return b.withTrimmedLeft (b.getWidth() - factsW);
    }

    juce::Rectangle<float> BrowserList::rowColumn() const
    {
        auto b = getLocalBounds().toFloat();
        b.removeFromLeft (categoryW + 12.0f);
        b.removeFromRight (factsW + 12.0f);
        return b;
    }

    int BrowserList::visibleRows() const
    {
        return juce::jmax (1, (int) std::floor (rowColumn().getHeight() / rowHeight));
    }

    void BrowserList::clampScroll()
    {
        firstRow = juce::jlimit (0, juce::jmax (0, (int) rows.size() - visibleRows()), firstRow);
    }

    void BrowserList::ensureVisible (int row)
    {
        if (row < firstRow)
            firstRow = row;
        else if (row >= firstRow + visibleRows())
            firstRow = row - visibleRows() + 1;
        clampScroll();
    }

    void BrowserList::moveSelection (int delta)
    {
        if (rows.empty())
            return;
        const int next = juce::jlimit (0, (int) rows.size() - 1, (selected < 0 ? (delta > 0 ? -1 : (int) rows.size()) : selected) + delta);
        if (next == selected)
            return;
        setSelected (next);
        if (onSelect)
            onSelect (selected);
    }

    void BrowserList::paint (juce::Graphics& g)
    {
        const auto& t = theme();
        const bool focused = hasKeyboardFocus (false);

        // Categories: plain words; the chosen one filled, OP-XY style.
        {
            auto col = categoryColumn();
            paint::hairlineV (g, col.getRight() + 5.5f, col.getY(), col.getBottom(), t.scr.rule);
            const auto font = HaloFonts::font (Role::ui, 12.0f);
            for (int i = 0; i < categories.size(); ++i)
            {
                auto r = col.removeFromTop (categoryRowHeight);
                if (r.getBottom() > getHeight())
                    break;
                const bool on = i == category;
                if (on)
                {
                    g.setColour (t.scr.ramp[7]);
                    g.fillRoundedRectangle (r.reduced (0.0f, 2.0f), 2.5f);
                }
                paint::text (g, categories[i], r.reduced (10.0f, 0.0f), font, on ? juce::Colour (0xff000005) : t.scr.ink.withAlpha (0.82f));
            }
        }

        // Rows: number, name, facts.
        {
            const auto col = rowColumn();
            paint::hairlineV (g, col.getRight() + 5.5f, col.getY(), col.getBottom(), t.scr.rule);
            if (rows.empty())
            {
                paint::text (g, emptyText, col.withHeight (rowHeight).reduced (10.0f, 0.0f), HaloFonts::font (Role::ui, 12.0f), t.scr.dim);
            }
            const auto mono = HaloFonts::font (Role::mono, 11.5f);
            const auto name = HaloFonts::font (Role::ui, 13.0f);
            const int last = juce::jmin ((int) rows.size(), firstRow + visibleRows());
            for (int i = firstRow; i < last; ++i)
            {
                const auto r = juce::Rectangle<float> (col.getX(), col.getY() + (float) (i - firstRow) * rowHeight, col.getWidth(), rowHeight);
                const bool on = i == selected;
                if (on)
                {
                    g.setColour (t.scr.ramp[7]);
                    g.fillRoundedRectangle (r.reduced (0.0f, 1.5f), 2.5f);
                    if (focused)
                    {
                        g.setColour (t.scr.hot);
                        g.drawRoundedRectangle (r.reduced (0.5f, 2.0f), 2.5f, 1.0f);
                    }
                }
                else if ((i % 2) == 1)
                {
                    g.setColour (t.scr.panel.withAlpha (0.55f));
                    g.fillRect (r.reduced (0.0f, 1.5f));
                }
                const auto ink = on ? juce::Colour (0xff000005) : t.scr.ink;
                const auto dim = on ? juce::Colour (0xff000005).withAlpha (0.6f) : t.scr.dim;
                auto inner = r.reduced (10.0f, 0.0f);
                paint::text (g, rows[(size_t) i].number, inner.removeFromLeft (38.0f), mono, dim);
                const float fw = paint::textWidth (mono, rows[(size_t) i].facts) + 4.0f;
                paint::text (g, rows[(size_t) i].facts, inner.removeFromRight (fw), mono, dim, juce::Justification::centredRight);
                inner.removeFromRight (10.0f);
                paint::text (g, paint::middleEllipsis (rows[(size_t) i].name, name, inner.getWidth()), inner, name, ink);
            }
            // A scroll position mark when the list is longer than the view.
            if ((int) rows.size() > visibleRows())
            {
                const float frac = (float) visibleRows() / (float) rows.size();
                const float h = juce::jmax (16.0f, col.getHeight() * frac);
                const float y = col.getY() + (col.getHeight() - h) * (float) firstRow / (float) juce::jmax (1, (int) rows.size() - visibleRows());
                g.setColour (t.scr.ramp[3]);
                g.fillRect (juce::Rectangle<float> (col.getRight() + 4.0f, y, 3.0f, h));
            }
        }

        if (paintFacts)
            paintFacts (g, factsColumn());
    }

    void BrowserList::mouseDown (const juce::MouseEvent& e)
    {
        auto cats = categoryColumn();
        if (cats.contains (e.position))
        {
            const int i = (int) ((e.position.y - cats.getY()) / categoryRowHeight);
            if (juce::isPositiveAndBelow (i, categories.size()) && onCategory)
                onCategory (i);
            return;
        }
        const auto col = rowColumn();
        if (col.contains (e.position))
        {
            const int i = firstRow + (int) ((e.position.y - col.getY()) / rowHeight);
            if (juce::isPositiveAndBelow (i, (int) rows.size()) && i != selected)
            {
                setSelected (i, false);
                if (onSelect)
                    onSelect (i);
            }
        }
    }

    void BrowserList::mouseDoubleClick (const juce::MouseEvent& e)
    {
        const auto col = rowColumn();
        if (! col.contains (e.position))
            return;
        const int i = firstRow + (int) ((e.position.y - col.getY()) / rowHeight);
        if (juce::isPositiveAndBelow (i, (int) rows.size()) && onActivate)
            onActivate (i);
    }

    void BrowserList::mouseWheelMove (const juce::MouseEvent& e, const juce::MouseWheelDetails& w)
    {
        wheelAcc += (std::abs (w.deltaY) >= std::abs (w.deltaX) ? w.deltaY : -w.deltaX) * (w.isReversed ? -1.0f : 1.0f);
        const float notch = (float) encoder::kNotch;
        int detents = 0;
        while (std::abs (wheelAcc) >= notch * 0.85f)
        {
            const int dir = wheelAcc > 0 ? 1 : -1;
            wheelAcc -= (float) dir * notch;
            detents += dir;
        }
        if (detents == 0)
            return;
        if (categoryColumn().contains (e.position) && onCategory)
        {
            onCategory (juce::jlimit (0, juce::jmax (0, categories.size() - 1), category - detents));
            return;
        }
        firstRow -= detents * 3;
        clampScroll();
        repaint();
    }

    bool BrowserList::keyPressed (const juce::KeyPress& k)
    {
        const int page = juce::jmax (1, visibleRows() - 1);
        if (k == juce::KeyPress::upKey)       { moveSelection (-1); return true; }
        if (k == juce::KeyPress::downKey)     { moveSelection (1); return true; }
        if (k == juce::KeyPress::pageUpKey)   { moveSelection (-page); return true; }
        if (k == juce::KeyPress::pageDownKey) { moveSelection (page); return true; }
        if (k == juce::KeyPress::homeKey)     { moveSelection (-(int) rows.size()); return true; }
        if (k == juce::KeyPress::endKey)      { moveSelection ((int) rows.size()); return true; }
        if (k == juce::KeyPress::returnKey && selected >= 0 && onActivate) { onActivate (selected); return true; }
        return false;
    }
}
