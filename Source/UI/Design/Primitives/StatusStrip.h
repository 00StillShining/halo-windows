#pragma once

#include "App/AppModel.h"
#include "Core/UiLayout.h"

#include <juce_gui_basics/juce_gui_basics.h>

namespace halo
{
    // The status strip across the top of the glass (docs/redesign/plan.md
    // section 3.2): wordmark with the ring glyph, the reserved honest slot
    // (MOCK / PREVIEW), device, feed, ring, out, mon, rec and writes. Slots
    // collapse in a fixed order (layout::layoutStrip) and the honest slot's
    // width never moves the others. Every value reads cached state: nothing
    // here touches the filesystem. Polls at 2 Hz and on model changes.
    class StatusStrip : public juce::Component, private AppModel::Listener, private juce::Timer
    {
    public:
        explicit StatusStrip (AppModel&);
        ~StatusStrip() override;

        void setMonitorOwnedByEncoder (bool owned) { monOwned = owned; repaint(); }

        // What the strip would show (also used by tests and the gallery).
        layout::StripContent content() const;

        void paint (juce::Graphics&) override;

    private:
        void statusChanged() override { repaint(); }
        void displayChanged (const DisplayState&) override;
        void timerCallback() override;

        AppModel& model;
        FeedMode lastFeed = FeedMode::preview;
        bool blink = true, monOwned = false;
    };
}
