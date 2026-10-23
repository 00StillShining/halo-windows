#pragma once

#include "App/AppModel.h"
#include "Core/UiLayout.h"
#include "UI/Design/EncoderBus.h"
#include "UI/Design/Primitives/ActionKey.h"
#include "UI/Design/Primitives/NameCells.h"
#include "UI/Design/Primitives/Notice.h"
#include "UI/Design/Primitives/PadGrid.h"
#include "UI/Design/Primitives/StepKeyRow.h"
#include "UI/Design/Primitives/Waveform/Slicer.h"
#include "UI/Design/WriteGate.h"

#include <juce_gui_basics/juce_gui_basics.h>

namespace halo
{
    // CHOP (Ctrl+7, or EDIT's CHOP; docs/redesign/plan.md section 3.7):
    //   header   scissors, the title and its facts, the mode words
    //            (transient / even), the visible CLEAR CUTS key, the big
    //            slice count, an esc chip
    //   canvas   the stepped slicer under an overview strip and a ruler, the
    //            selected slice's bracket and the status line
    //   pads     GROUP a-d and PROJECT on one row, the pad grid, the plan column
    //   output   QUALITY, CHANNELS (shared with EDIT), NAME, the totals
    //   send     memory, the notice (blocker > info > plain, today's copy),
    //            the backup line, export / show / SEND TO PADS (write gate)
    // Encoders: SENSITIVITY (dead in even), SLICES, LENGTH (+ AUTO-TRIM), SLICE.
    class ChopPage : public juce::Component, private juce::ChangeListener, private AppModel::Listener
    {
    public:
        ChopPage (AppModel&, WriteGate&);
        ~ChopPage() override;

        void setFrame (const layout::Frame&);
        std::array<std::optional<EncoderBinding>, 4> bindings();
        void paintLane (juce::Graphics&, juce::Rectangle<float>, bool compact) const;

        void paint (juce::Graphics&) override;
        void resized() override;
        bool keyPressed (const juce::KeyPress&) override;
        void mouseUp (const juce::MouseEvent&) override;

    private:
        void changeListenerCallback (juce::ChangeBroadcaster*) override { refresh(); }
        void statusChanged() override { refresh(); }
        void refresh();
        void send();
        juce::String sendBlocker() const;
        juce::String trimNote() const;
        juce::String plainNote() const;
        NoticeData notice() const;
        juce::Rectangle<float> localF (const layout::Rect&) const;
        juce::Rectangle<float> modeWordBounds (bool even) const;
        juce::Rectangle<float> projectArrow (int dir) const;

        AppModel& model;
        WriteGate& gate;
        EditSession& session;
        layout::Frame frame;
        layout::ChopLayout L;
        juce::Rectangle<float> plainArea;

        Slicer slicer { session };
        PadGrid pads;
        StepKeyRow groups, quality, channels;
        NameCells name;
        Notice noticeView;
        ActionKey clearKey { "clear cuts", Pict::cross }, exportKey { "export slices", Pict::folder },
                  showKey { "show" }, sendKey { "send to pads", Pict::upload, ActionKey::Variant::deviceWrite };
    };
}
