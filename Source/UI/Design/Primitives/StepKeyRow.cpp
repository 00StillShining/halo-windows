#include "UI/Design/Primitives/StepKeyRow.h"
#include "UI/Design/Paint.h"

namespace halo
{
    StepKeyRow::StepKeyRow()
    {
        setWantsKeyboardFocus (true);
        setMouseClickGrabsKeyboardFocus (false);
    }

    void StepKeyRow::setItems (const juce::StringArray& newItems)
    {
        if (newItems == items)
            return;
        items = newItems;
        selected = juce::jlimit (-1, items.size() - 1, selected);
        repaint();
    }

    void StepKeyRow::setSelected (int index)
    {
        index = juce::jlimit (-1, items.size() - 1, index);
        if (index == selected)
            return;
        selected = index;
        repaint();
    }

    void StepKeyRow::setRowEnabled (bool shouldBeEnabled)
    {
        if (rowEnabled == shouldBeEnabled)
            return;
        rowEnabled = shouldBeEnabled;
        setMouseCursor (rowEnabled ? juce::MouseCursor::PointingHandCursor : juce::MouseCursor::NormalCursor);
        repaint();
    }

    juce::Rectangle<float> StepKeyRow::keyBounds (int index) const
    {
        const int n = juce::jmax (1, items.size());
        // Equal widths from the full row, snapped to whole pixels so every key
        // is exactly the same width.
        const float w = std::floor (((float) getWidth() - gap * (float) (n - 1)) / (float) n);
        const float used = w * (float) n + gap * (float) (n - 1);
        const float x0 = std::floor (((float) getWidth() - used) * 0.5f);
        return { x0 + (float) index * (w + gap), 0.0f, w, (float) getHeight() };
    }

    int StepKeyRow::indexAt (juce::Point<float> p) const
    {
        for (int i = 0; i < items.size(); ++i)
            if (keyBounds (i).contains (p))
                return i;
        return -1;
    }

    void StepKeyRow::paint (juce::Graphics& g)
    {
        for (int i = 0; i < items.size(); ++i)
        {
            const auto state = ! rowEnabled ? paint::KeyState::disabled
                             : i == selected ? paint::KeyState::selected
                                             : paint::KeyState::normal;
            paint::stepKey (g, keyBounds (i), items[i], state, (i % 2) == 1, i == hovered && rowEnabled,
                            hasKeyboardFocus (false) && i == selected);
        }
    }

    void StepKeyRow::mouseMove (const juce::MouseEvent& e)
    {
        const int h = indexAt (e.position);
        if (h != hovered)
        {
            hovered = h;
            repaint();
        }
    }

    void StepKeyRow::mouseExit (const juce::MouseEvent&)
    {
        hovered = -1;
        repaint();
    }

    void StepKeyRow::choose (int index)
    {
        if (! rowEnabled || ! juce::isPositiveAndBelow (index, items.size()))
            return;
        selected = index;
        repaint();
        if (onSelect)
            onSelect (index);
    }

    void StepKeyRow::mouseUp (const juce::MouseEvent& e)
    {
        if (e.mouseWasClicked())
            choose (indexAt (e.position));
    }

    void StepKeyRow::mouseWheelMove (const juce::MouseEvent&, const juce::MouseWheelDetails& w)
    {
        if (! rowEnabled || items.isEmpty())
            return;
        wheelAccumulator += (std::abs (w.deltaY) > std::abs (w.deltaX) ? w.deltaY : -w.deltaX) * (w.isReversed ? -1.0f : 1.0f);
        // One Windows wheel notch is 120/256 in JUCE units; touchpads send less.
        const float notch = 120.0f / 256.0f, threshold = 0.4f;
        while (std::abs (wheelAccumulator) >= threshold)
        {
            const int dir = wheelAccumulator > 0 ? 1 : -1; // like an encoder: up = clockwise = next
            wheelAccumulator -= (float) dir * notch;
            choose (juce::jlimit (0, items.size() - 1, (selected < 0 ? 0 : selected) + dir));
        }
    }

    bool StepKeyRow::keyPressed (const juce::KeyPress& k)
    {
        if (k.isKeyCode (juce::KeyPress::leftKey))  { choose (juce::jmax (0, selected - 1)); return true; }
        if (k.isKeyCode (juce::KeyPress::rightKey)) { choose (juce::jmin (items.size() - 1, selected + 1)); return true; }
        return false;
    }
}
