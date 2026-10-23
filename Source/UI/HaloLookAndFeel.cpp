#include "UI/HaloLookAndFeel.h"
#include "UI/Design/Fonts.h"
#include "UI/Design/Theme.h"

namespace halo
{
    HaloLookAndFeel::HaloLookAndFeel()
    {
        const auto& t = theme();
        const juce::Colour black { 0xff000005 };
        setColour (juce::ResizableWindow::backgroundColourId, t.scr.bg);
        setColour (juce::Label::textColourId, t.scr.ink);
        setColour (juce::ScrollBar::thumbColourId, t.scr.ramp[3]);
        setColour (juce::TextEditor::backgroundColourId, t.scr.panel);
        setColour (juce::TextEditor::textColourId, t.scr.ink);
        setColour (juce::TextEditor::outlineColourId, t.scr.frame);
        setColour (juce::TextEditor::focusedOutlineColourId, t.scr.hot);
        setColour (juce::TextEditor::highlightColourId, t.scr.ramp[3]);
        setColour (juce::CaretComponent::caretColourId, t.scr.hot);
        setColour (juce::PopupMenu::backgroundColourId, t.scr.panel);
        setColour (juce::PopupMenu::textColourId, t.scr.ink);
        setColour (juce::PopupMenu::headerTextColourId, t.scr.dim);
        setColour (juce::PopupMenu::highlightedBackgroundColourId, t.scr.ramp[7]);
        setColour (juce::PopupMenu::highlightedTextColourId, black);
        setColour (juce::AlertWindow::backgroundColourId, t.scr.paper);
        setColour (juce::AlertWindow::textColourId, black);
        setColour (juce::AlertWindow::outlineColourId, black);
        setColour (juce::TextButton::buttonColourId, t.scr.neutralKey);
        setColour (juce::TextButton::buttonOnColourId, t.scr.ramp[7]);
        setColour (juce::TextButton::textColourOffId, t.scr.ink);
        setColour (juce::TextButton::textColourOnId, black);
        setDefaultSansSerifTypefaceName ("Inter Tight");
    }

    namespace
    {
        juce::Font tooltipFont()
        {
            return HaloFonts::instance() != nullptr ? HaloFonts::font (Role::ui) : juce::Font (juce::FontOptions (13.0f));
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
        if (HaloFonts::instance() != nullptr)
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
        return HaloFonts::instance() != nullptr ? HaloFonts::font (Role::tag, 12.0f) : LookAndFeel_V4::getAlertWindowTitleFont();
    }

    juce::Font HaloLookAndFeel::getAlertWindowMessageFont()
    {
        return HaloFonts::instance() != nullptr ? HaloFonts::font (Role::body) : LookAndFeel_V4::getAlertWindowMessageFont();
    }

    juce::Font HaloLookAndFeel::getAlertWindowFont()
    {
        return HaloFonts::instance() != nullptr ? HaloFonts::font (Role::ui) : LookAndFeel_V4::getAlertWindowFont();
    }

    juce::Font HaloLookAndFeel::getPopupMenuFont()
    {
        return HaloFonts::instance() != nullptr ? HaloFonts::font (Role::ui, 13.0f) : LookAndFeel_V4::getPopupMenuFont();
    }
}
