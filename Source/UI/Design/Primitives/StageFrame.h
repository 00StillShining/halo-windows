#pragma once

#include "App/AppModel.h"

#include <juce_gui_basics/juce_gui_basics.h>

namespace halo
{
    // The frame around the 3D stage (docs/redesign/plan.md section 3.4): a
    // 22 px chrome row above it and an 8 px band around its active child,
    // which is the GL view or, while the GL child is hidden or re-creating,
    // a JUCE-painted poster of its last frame. The band carries the
    // authoritative honest marks, drawn beside the GL window (nothing JUCE
    // paints can sit over it, H6):
    //   live      the glass with a hairline
    //   MOCK      yellow / black caution stripes, 14 px pitch
    //   PREVIEW   a 1.5 px violet dashed line
    //   both      outer 5 px hazard, inner 3 px violet
    class StageFrame : public juce::Component, private AppModel::Listener, private juce::Timer
    {
    public:
        StageFrame (AppModel&, juce::Component& stage);
        ~StageFrame() override;

        static constexpr int chromeHeight = 22, band = 8;

        // The return poster (3D plan section 4.4 item 2): painted in the stage
        // cell instead of the GL child while that is hidden. Empty until the
        // stage can hand over its last frame.
        void setPoster (const juce::Image& image) { poster = image; repaint(); }
        juce::Rectangle<int> stageBounds() const;

        void resized() override;
        void paint (juce::Graphics&) override;

    private:
        void statusChanged() override { repaint(); }
        void displayChanged (const DisplayState&) override;
        void timerCallback() override { repaint(); }

        AppModel& model;
        juce::Component& stage;
        juce::Image poster;
        FeedMode lastFeed = FeedMode::preview;
    };
}
