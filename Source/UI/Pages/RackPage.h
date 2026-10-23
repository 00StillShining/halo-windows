#pragma once

#include "App/AppModel.h"
#include "Core/UiLayout.h"
#include "UI/Design/EncoderBus.h"
#include "UI/Design/Motion.h"
#include "UI/Design/Primitives/ActionKey.h"
#include "UI/Design/Primitives/Notice.h"
#include "UI/Design/Primitives/StepKeyRow.h"

#include <juce_gui_basics/juce_gui_basics.h>

namespace halo
{
    // RACK (Ctrl+5; docs/redesign/plan.md section 3.11): the dub rack as a
    // sound path. in -> `pre-rack` ladders -> four paper tiles (echo, spring,
    // sweep, low end) -> limiter -1 dbfs -> `out` ladders -> out. Each tile
    // carries its values in its corners as black Numerals with a 3 px bar in
    // the encoder's shade; a bypassed tile is dark and hatched; the focused
    // one has a hot outline. The four encoders are bound to the focused
    // module (keys 1-4 or the soft words choose it).
    class RackPage : public juce::Component, private AppModel::Listener, private juce::Timer
    {
    public:
        explicit RackPage (AppModel&);
        ~RackPage() override;

        void setFrame (const layout::Frame&);
        std::array<std::optional<EncoderBinding>, 4> bindings();
        std::function<void()> onFocusChanged; // rebind the encoders
        void paintLane (juce::Graphics&, juce::Rectangle<float>, bool compact) const;

        void paint (juce::Graphics&) override;
        void resized() override;
        bool keyPressed (const juce::KeyPress&) override;
        void mouseUp (const juce::MouseEvent&) override;

        enum Module { echo, spring, sweep, low };

    private:
        void statusChanged() override { refresh(); }
        void timerCallback() override;
        void refresh();
        void setFocus (int module);
        void toggleModule (int module);
        std::atomic<bool>& onFlag (int module) const;
        juce::Rectangle<float> localF (const layout::Rect&) const;
        juce::Rectangle<float> headerTag (int index) const;   // 0 engaged, 1 print fx, 2 monitor
        juce::Rectangle<float> moduleWord (int module) const;
        juce::Rectangle<float> syncTag() const;
        void paintTile (juce::Graphics&, int module, juce::Rectangle<float>);
        void paintIllustration (juce::Graphics&, int module, juce::Rectangle<float>, juce::Colour line, bool live);
        void paintLadders (juce::Graphics&, juce::Rectangle<float>, const StereoLevels&, const juce::String& tap);

        AppModel& model;
        RackParameters& params;
        layout::Frame frame;
        layout::RackLayout L;
        int focused = echo;
        // Signal-driven motion: reels turn with the echo, rings travel at a
        // speed set by the pre-rack level, the spring jiggles and the cone
        // pumps with the output. All still under reduced motion.
        Spinner echoReels;
        double ringPhase = 0.0, springPhase = 0.0, conePhase = 0.0;
        float drive = 0.0f;                 // 0...1 from the meters, smoothed
        bool live() const;

        StepKeyRow divisions;
        ActionKey tapKey { "tap" };
        Notice noticeView;
    };
}
