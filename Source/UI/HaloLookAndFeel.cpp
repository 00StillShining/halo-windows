#include "UI/HaloLookAndFeel.h"
#include "UI/Design/Fonts.h"
#include "UI/Design/Theme.h"

namespace halo
{
    namespace
    {
        const auto& c = legacyColours();
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

        setColour (juce::ComboBox::backgroundColourId, c.paperHigh);
        setColour (juce::ComboBox::textColourId, c.ink);
        setColour (juce::ComboBox::outlineColourId, c.ink.withAlpha (0.25f));
        setColour (juce::ComboBox::arrowColourId, c.inkSoft);
        setColour (juce::ComboBox::focusedOutlineColourId, c.orange);
        setColour (juce::PopupMenu::backgroundColourId, c.paperHigh);
        setColour (juce::PopupMenu::textColourId, c.ink);
        setColour (juce::PopupMenu::highlightedBackgroundColourId, c.ink);
        setColour (juce::PopupMenu::highlightedTextColourId, c.paperHigh);

        setColour (juce::Slider::backgroundColourId, c.metal.withAlpha (0.6f));
        setColour (juce::Slider::trackColourId, c.riddimGreen);
        setColour (juce::Slider::thumbColourId, c.orange);
        setColour (juce::Slider::textBoxTextColourId, c.ink);
        setColour (juce::Slider::textBoxBackgroundColourId, c.paperHigh);
        setColour (juce::Slider::textBoxOutlineColourId, c.ink.withAlpha (0.2f));
        setDefaultSansSerifTypefaceName ("Segoe UI");
    }

    void HaloLookAndFeel::drawPanel (juce::Graphics& g, juce::Rectangle<float> bounds, juce::Colour fill)
    {
        // On the black glass a light "shadow" would read as a ghost: no shadow there.
        if (! c.dark)
        {
            g.setColour (c.ink.withAlpha (0.22f));
            g.fillRoundedRectangle (bounds.translated ((float) HaloMetrics::panelShadowX, (float) HaloMetrics::panelShadowY),
                                    HaloMetrics::radiusPanel);
        }
        g.setColour (fill);
        g.fillRoundedRectangle (bounds, HaloMetrics::radiusPanel);
        g.setColour (c.ink.withAlpha (0.18f));
        g.drawRoundedRectangle (bounds.reduced (0.5f), HaloMetrics::radiusPanel, 1.0f);
    }

    namespace
    {
        juce::Font tooltipFont()
        {
            return HaloFonts::instance() != nullptr ? HaloFonts::font (Role::ui) : HaloType::body (13.0f);
        }

        juce::TextLayout tooltipLayout (const juce::String& text)
        {
            juce::AttributedString s;
            s.setJustification (juce::Justification::centredLeft);
            s.append (text, tooltipFont(), juce::Colour (0xff000005));
            juce::TextLayout layout;
            layout.createLayoutWithBalancedLineLengths (s, 340.0f);
            return layout;
        }
    }

    juce::Rectangle<int> HaloLookAndFeel::getTooltipBounds (const juce::String& tipText, juce::Point<int> screenPos,
                                                            juce::Rectangle<int> parentArea)
    {
        const auto layout = tooltipLayout (tipText);
        const int w = (int) std::ceil (layout.getWidth()) + 20;
        const int h = (int) std::ceil (layout.getHeight()) + 14;
        return juce::Rectangle<int> (screenPos.x > parentArea.getCentreX() ? screenPos.x - (w + 12) : screenPos.x + 24,
                                     screenPos.y > parentArea.getCentreY() ? screenPos.y - (h + 6) : screenPos.y + 6,
                                     w, h).constrainedWithin (parentArea);
    }

    void HaloLookAndFeel::drawTooltip (juce::Graphics& g, const juce::String& text, int width, int height)
    {
        const auto bounds = juce::Rectangle<float> (0.0f, 0.0f, (float) width, (float) height);
        g.setColour (theme().scr.paper);
        g.fillRect (bounds);
        g.setColour (juce::Colour (0xff000005));
        g.drawRect (bounds, 1.0f);
        tooltipLayout (text).draw (g, bounds.reduced (10.0f, 7.0f));
    }

    juce::Typeface::Ptr HaloLookAndFeel::getTypefaceForFont (const juce::Font& font)
    {
        if (! theme().legacyLook && HaloFonts::instance() != nullptr)
        {
            const auto name = font.getTypefaceName();
            const bool bold = font.isBold();
            if (name == juce::Font::getDefaultSansSerifFontName() || name == "Segoe UI")
                return HaloFonts::instance()->typefaceForFamily ("Inter Tight", bold);
            if (auto tf = HaloFonts::instance()->typefaceForFamily (name, bold))
                return tf;
        }
        return LookAndFeel_V4::getTypefaceForFont (font);
    }

    void HaloLookAndFeel::drawAlertBox (juce::Graphics& g, juce::AlertWindow& alert, const juce::Rectangle<int>& textArea,
                                        juce::TextLayout& layout)
    {
        if (theme().legacyLook)
            return LookAndFeel_V4::drawAlertBox (g, alert, textArea, layout);
        const auto r = alert.getLocalBounds().toFloat();
        g.setColour (theme().scr.paper);
        g.fillRect (r);
        g.setColour (juce::Colour (0xff000005));
        g.drawRect (r, 1.0f);
        layout.draw (g, juce::Rectangle<int> (textArea.getX(), textArea.getY(), alert.getWidth() - textArea.getX() - 16,
                                              alert.getHeight() - textArea.getY()).toFloat());
    }

    juce::Font HaloLookAndFeel::getAlertWindowTitleFont()
    {
        return ! theme().legacyLook && HaloFonts::instance() != nullptr ? HaloFonts::font (Role::tag, 12.0f) : LookAndFeel_V4::getAlertWindowTitleFont();
    }

    juce::Font HaloLookAndFeel::getAlertWindowMessageFont()
    {
        return ! theme().legacyLook && HaloFonts::instance() != nullptr ? HaloFonts::font (Role::body) : LookAndFeel_V4::getAlertWindowMessageFont();
    }

    juce::Font HaloLookAndFeel::getAlertWindowFont()
    {
        return ! theme().legacyLook && HaloFonts::instance() != nullptr ? HaloFonts::font (Role::ui) : LookAndFeel_V4::getAlertWindowFont();
    }

    void HaloLookAndFeel::drawButtonBackground (juce::Graphics& g, juce::Button& button, const juce::Colour& fill,
                                                bool highlighted, bool down)
    {
        auto bounds = button.getLocalBounds().toFloat().reduced (1.0f, 1.0f).withTrimmedRight (3.0f).withTrimmedBottom (3.0f);
        const float travel = down ? 2.0f : (highlighted ? -0.5f : 0.0f);
        const auto cap = bounds.translated (down ? 2.0f : 0.0f, travel);

        if (! down)
        {
            // A hard offset shadow on paper; on the glass a black lip instead.
            g.setColour (c.dark ? juce::Colours::black : c.ink.withAlpha (button.isEnabled() ? 0.30f : 0.12f));
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

    void HaloLookAndFeel::drawRotarySlider (juce::Graphics& g, int x, int y, int width, int height, float pos,
                                            float startAngle, float endAngle, juce::Slider& slider)
    {
        const auto bounds = juce::Rectangle<int> (x, y, width, height).toFloat().reduced (4.0f);
        const float size = juce::jmin (bounds.getWidth(), bounds.getHeight());
        const auto centre = bounds.getCentre();
        const float radius = size * 0.5f;
        const float angle = startAngle + pos * (endAngle - startAngle);
        const bool enabled = slider.isEnabled();

        // Value arc track + fill.
        juce::Path track, fill;
        track.addCentredArc (centre.x, centre.y, radius, radius, 0.0f, startAngle, endAngle, true);
        fill.addCentredArc (centre.x, centre.y, radius, radius, 0.0f, startAngle, angle, true);
        g.setColour (c.ink.withAlpha (0.12f));
        g.strokePath (track, juce::PathStrokeType (2.5f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
        g.setColour (enabled ? c.orange : c.inkSoft.withAlpha (0.4f));
        g.strokePath (fill, juce::PathStrokeType (2.5f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));

        // Cap with a hard offset shadow (key light upper-left, no blur).
        const float capR = radius * 0.72f;
        g.setColour (c.ink.withAlpha (0.25f));
        g.fillEllipse (centre.x - capR + 2.0f, centre.y - capR + 3.0f, capR * 2.0f, capR * 2.0f);
        g.setColour (c.paperHigh);
        g.fillEllipse (centre.x - capR, centre.y - capR, capR * 2.0f, capR * 2.0f);
        g.setColour (c.ink.withAlpha (0.25f));
        g.drawEllipse (centre.x - capR, centre.y - capR, capR * 2.0f, capR * 2.0f, 1.0f);

        // Indicator line.
        const auto tip = centre.getPointOnCircumference (capR * 0.82f, angle);
        const auto base = centre.getPointOnCircumference (capR * 0.30f, angle);
        g.setColour (enabled ? c.ink : c.inkSoft);
        g.drawLine ({ base, tip }, 2.5f);
    }
}
