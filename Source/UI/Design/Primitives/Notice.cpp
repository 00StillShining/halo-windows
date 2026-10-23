#include "UI/Design/Primitives/Notice.h"
#include "UI/Design/Fonts.h"
#include "UI/Design/Pictograms.h"
#include "UI/Design/Theme.h"

namespace halo::notice
{
    namespace
    {
        constexpr float padX = 12.0f, padY = 9.0f, iconSize = 16.0f, iconGap = 10.0f, lineGap = 3.0f;

        // "LENGTH: 7 slices..." -> { "LENGTH:", " 7 slices..." }
        std::pair<juce::String, juce::String> splitTitle (const juce::String& s)
        {
            const int colon = s.indexOfChar (':');
            if (colon > 0 && colon < 16)
            {
                const auto head = s.substring (0, colon);
                if (head == head.toUpperCase() && head.containsAnyOf ("ABCDEFGHIJKLMNOPQRSTUVWXYZ") && ! head.containsChar (' '))
                    return { s.substring (0, colon + 1), s.substring (colon + 1) };
            }
            return { {}, s };
        }

        juce::TextLayout layout (const NoticeData& n, float width, bool onPaper = false)
        {
            const auto& t = theme();
            const auto ink = onPaper ? juce::Colour (0xff000005) : t.scr.ink;
            juce::AttributedString a;
            a.setLineSpacing (lineGap);
            a.setWordWrap (juce::AttributedString::byWord);
            switch (n.kind)
            {
                case honest::NoticeKind::blocker:
                    // Quiet but unmistakable: crimson outline and triangle, ink sentence.
                    a.append (n.text, HaloFonts::font (Role::bodyStrong), ink);
                    break;
                case honest::NoticeKind::info:
                {
                    const auto [title, body] = splitTitle (n.text);
                    if (title.isNotEmpty())
                        a.append (title, HaloFonts::font (Role::bodyStrong), t.honest.info);
                    a.append (body, HaloFonts::font (Role::body), ink);
                    break;
                }
                case honest::NoticeKind::plain:
                case honest::NoticeKind::none:
                default:
                    a.append (n.text, HaloFonts::font (Role::body), ink);
                    break;
            }
            juce::TextLayout l;
            l.createLayout (a, juce::jmax (1.0f, width));
            return l;
        }

        bool boxed (const NoticeData& n)
        {
            return n.kind == honest::NoticeKind::blocker || n.kind == honest::NoticeKind::info;
        }

        float textWidthFor (const NoticeData& n, float width)
        {
            return boxed (n) ? width - 2.0f * padX - iconSize - iconGap : width;
        }
    }

    float height (const NoticeData& n, float width)
    {
        if (n.kind == honest::NoticeKind::none || n.text.isEmpty())
            return 0.0f;
        const float textH = std::ceil (layout (n, textWidthFor (n, width)).getHeight());
        return boxed (n) ? juce::jmax (iconSize, textH) + 2.0f * padY : textH;
    }

    void paint (juce::Graphics& g, const NoticeData& n, juce::Rectangle<float> r, bool onPaper)
    {
        if (n.kind == honest::NoticeKind::none || n.text.isEmpty())
            return;
        const auto& t = theme();
        if (! boxed (n))
        {
            layout (n, r.getWidth(), onPaper).draw (g, r);
            return;
        }

        const float h = height (n, r.getWidth());
        auto box = r.withHeight (juce::jmin (h, r.getHeight()));
        g.setColour (n.kind == honest::NoticeKind::blocker ? t.honest.danger : t.honest.info);
        g.drawRoundedRectangle (box.reduced (0.75f), 4.0f, 1.5f);

        auto inner = box.reduced (padX, padY);
        auto icon = inner.removeFromLeft (iconSize).removeFromTop (iconSize);
        // Align the icon with the first line's cap height.
        icon = icon.translated (0.0f, 1.0f);
        pictograms::draw (g, n.kind == honest::NoticeKind::blocker ? Pict::triangle : Pict::info, icon,
                          n.kind == honest::NoticeKind::blocker ? t.honest.danger : t.honest.info, 1.5f);
        inner.removeFromLeft (iconGap);
        layout (n, inner.getWidth(), onPaper).draw (g, inner);
    }
}
