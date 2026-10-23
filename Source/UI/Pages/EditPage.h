#pragma once

#include "App/AppModel.h"
#include "Core/UiLayout.h"
#include "UI/Design/EncoderBus.h"
#include "UI/Design/Primitives/ActionKey.h"
#include "UI/Design/Primitives/Notice.h"
#include "UI/Design/Primitives/StepKeyRow.h"
#include "UI/Design/Primitives/Waveform/SampleBand.h"
#include "UI/Design/WriteGate.h"

#include <juce_gui_basics/juce_gui_basics.h>

namespace halo
{
    // EDIT (Ctrl+3; docs/redesign/plan.md section 3.8): the paper sampler
    // band with its four markers, the trim readout, and below it the
    // channels / quality / normalize choices, the exact size and memory, the
    // notice, the backup line, audition / export / chop / SEND TO EP-40 and
    // the page's own status line (CHOP's never shows here). Sending shows a
    // progress block with turning reels.
    // Encoders: the trim page (IN, FADE IN, FADE OUT, OUT); the shape page is
    // the shift layer (GAIN, CHANNELS, QUALITY, NORMALIZE): hold Shift to see
    // it, or click "shape" to latch it.
    class EditPage : public juce::Component, private juce::ChangeListener, private AppModel::Listener, private juce::Timer
    {
    public:
        EditPage (AppModel&, WriteGate&);
        ~EditPage() override;

        void setFrame (const layout::Frame&);
        std::array<std::optional<EncoderBinding>, 4> bindings (EncoderBus&);
        std::function<void()> onPageChanged; // trim <-> shape latched: rebind
        void paintLane (juce::Graphics&, juce::Rectangle<float>, bool compact) const;

        void paint (juce::Graphics&) override;
        void resized() override;
        bool keyPressed (const juce::KeyPress&) override;
        void mouseUp (const juce::MouseEvent&) override;

    private:
        void changeListenerCallback (juce::ChangeBroadcaster*) override { refresh(); }
        void statusChanged() override { refresh(); }
        void timerCallback() override;
        void refresh();
        void send();
        juce::String sendBlocker() const;
        NoticeData notice() const;
        juce::Rectangle<float> localF (const layout::Rect&) const;
        juce::Rectangle<float> pageWord (bool shape) const;
        juce::Rectangle<float> normalizeTag() const;

        AppModel& model;
        WriteGate& gate;
        EditSession& session;
        layout::Frame frame;
        layout::EditLayout L;
        bool shapeLatched = false;

        SampleBand band { session };
        StepKeyRow channels, quality;
        Notice noticeView;
        ActionKey auditionKey { "audition", Pict::play }, exportKey { "export", Pict::exportOut }, chopKey { "chop", Pict::chop },
                  sendKey { juce::String::fromUTF8 ("send to ep\xe2\x80\x93" "40"), Pict::upload, ActionKey::Variant::deviceWrite }, showKey { "show" },
                  libraryKey { "open library", Pict::capture };

        // The send in flight (for the progress block).
        bool sending = false;
        double sendFraction = -1.0, reelAngle = 0.0;
        juce::String sendEta;
    };
}
