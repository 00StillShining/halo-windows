#pragma once

#include "Core/HonestState.h"

#include <juce_gui_basics/juce_gui_basics.h>

namespace halo
{
    // A note beside the thing it explains (plan sections 2.1 and 2.5):
    //  - blocker: filled crimson, 3 px radius, white triangle, white sentence
    //    (600 weight). Stays until its condition clears; never blinks.
    //  - info: outlined Halo orange, circled i, the orange title word
    //    ("LENGTH:", "AUTO-TRIM:") then ink body text.
    //  - plain: ink body text, no box.
    // It wraps and grows; it never ellipsises.
    struct NoticeData
    {
        honest::NoticeKind kind = honest::NoticeKind::none;
        juce::String text;
        bool operator== (const NoticeData&) const = default;

        static NoticeData from (const honest::Notice& n) { return { n.kind, juce::String (n.text) }; }
        static NoticeData blocker (const juce::String& t) { return { honest::NoticeKind::blocker, t }; }
        static NoticeData info (const juce::String& t)    { return { honest::NoticeKind::info, t }; }
        static NoticeData plain (const juce::String& t)   { return { honest::NoticeKind::plain, t }; }
    };

    namespace notice
    {
        float height (const NoticeData&, float width);
        // onPaper: dark text for notices inside a paper card (the confirm).
        void paint (juce::Graphics&, const NoticeData&, juce::Rectangle<float>, bool onPaper = false);
    }

    class Notice : public juce::Component
    {
    public:
        Notice() { setInterceptsMouseClicks (false, false); }
        void set (const NoticeData& n)
        {
            if (n == data)
                return;
            data = n;
            repaint();
        }
        const NoticeData& get() const noexcept { return data; }
        int heightForWidth (int width) const { return (int) std::ceil (notice::height (data, (float) width)); }
        void paint (juce::Graphics& g) override { notice::paint (g, data, getLocalBounds().toFloat()); }

    private:
        NoticeData data;
    };
}
