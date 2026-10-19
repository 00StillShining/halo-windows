#include "UI/HaloLookAndFeel.h"

namespace halo
{
    namespace
    {
        const auto& c = HaloColours::graphPaper;
    }

    HaloLookAndFeel::HaloLookAndFeel()
    {
        setColour (juce::ResizableWindow::backgroundColourId, c.canvas);
        setColour (juce::TextButton::buttonColourId, c.paperHigh);
        setColour (juce::TextButton::buttonOnColourId, c.paperHigh);
        setColour (juce::TextButton::textColourOffId, c.ink);
        setColour (juce::TextButton::textColourOnId, c.ink);
        setColour (juce::Label::textColourId, c.ink);
        setColour (juce::ScrollBar::thumbColourId, c.inkSoft.withAlpha (0.35f));
        setDefaultSansSerifTypefaceName ("Segoe UI");
    }

    void HaloLookAndFeel::drawPanel (juce::Graphics& g, juce::Rectangle<float> bounds, juce::Colour fill)
    {
        g.setColour (c.ink.withAlpha (0.22f));
        g.fillRoundedRectangle (bounds.translated ((float) HaloMetrics::panelShadowX, (float) HaloMetrics::panelShadowY),
                                HaloMetrics::radiusPanel);
        g.setColour (fill);
        g.fillRoundedRectangle (bounds, HaloMetrics::radiusPanel);
        g.setColour (c.ink.withAlpha (0.18f));
        g.drawRoundedRectangle (bounds.reduced (0.5f), HaloMetrics::radiusPanel, 1.0f);
    }

    void HaloLookAndFeel::drawButtonBackground (juce::Graphics& g, juce::Button& button, const juce::Colour& fill,
                                                bool highlighted, bool down)
    {
        auto bounds = button.getLocalBounds().toFloat().reduced (1.0f, 1.0f).withTrimmedRight (3.0f).withTrimmedBottom (3.0f);
        const float travel = down ? 2.0f : (highlighted ? -0.5f : 0.0f);
        const auto cap = bounds.translated (down ? 2.0f : 0.0f, travel);

        if (! down)
        {
            g.setColour (c.ink.withAlpha (button.isEnabled() ? 0.30f : 0.12f));
            g.fillRoundedRectangle (bounds.translated (2.0f, 3.0f), HaloMetrics::radiusSmall);
        }

        g.setColour (button.isEnabled() ? fill : fill.withMultipliedAlpha (0.55f));
        g.fillRoundedRectangle (cap, HaloMetrics::radiusSmall);
        g.setColour (c.ink.withAlpha (0.25f));
        g.drawRoundedRectangle (cap.reduced (0.5f), HaloMetrics::radiusSmall, 1.0f);

        if (button.getToggleState())
        {
            g.setColour (c.orange);
            g.drawRoundedRectangle (cap.reduced (1.0f), HaloMetrics::radiusSmall, 2.0f);
            g.fillRect (cap.reduced (6.0f, 0.0f).removeFromBottom (2.0f).translated (0.0f, -3.0f));
        }
    }

    void HaloLookAndFeel::drawButtonText (juce::Graphics& g, juce::TextButton& button, bool, bool down)
    {
        auto bounds = button.getLocalBounds().withTrimmedRight (3).withTrimmedBottom (3);
        if (down)
            bounds.translate (2, 2);

        g.setFont (getTextButtonFont (button, button.getHeight()));
        g.setColour (button.findColour (juce::TextButton::textColourOffId).withMultipliedAlpha (button.isEnabled() ? 1.0f : 0.5f));
        g.drawFittedText (button.getButtonText(), bounds, juce::Justification::centred, 1);
    }

    juce::Font HaloLookAndFeel::getTextButtonFont (juce::TextButton&, int)
    {
        return HaloType::label (13.0f);
    }
}
