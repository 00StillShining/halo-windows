#pragma once

#include "App/AppModel.h"
#include "Core/UiLayout.h"
#include "UI/Design/EncoderBus.h"
#include "UI/Design/Primitives/ActionKey.h"
#include "UI/Design/Primitives/BrowserList.h"
#include "UI/Design/Primitives/PadGrid.h"
#include "UI/Design/Primitives/StepKeyRow.h"
#include "UI/Design/WriteGate.h"

#include <juce_gui_basics/juce_gui_basics.h>

#include <map>

namespace halo
{
    // LOAD (Ctrl+2; docs/redesign/plan.md section 3.9): the EP-40's own
    // library, read over TE's protocol. Header with the sync steps, the
    // storage dots, PROJECT + GROUP on one row over the PadGrid, the
    // three-column browser (category / sound / facts), the status line and
    // audition / save to library / assign to pad / delete. ASSIGN ARMED is a
    // persistent header tag; a pad click then goes through the WriteGate's
    // confirm card, as does delete. Without a readable EP-40 the body is one
    // centred card with today's copy; refresh stays.
    class LoadPage : public juce::Component, private juce::ChangeListener, private AppModel::Listener
    {
    public:
        LoadPage (AppModel&, WriteGate&);
        ~LoadPage() override;

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
        void rebuildRows();
        bool available() const;
        juce::String unavailableText() const;
        const Ep40Library::Sound* selectedSound() const;
        void selectSlot (int slot);
        void audition();
        void saveToLibrary();
        void armAssign (bool);
        void confirmAssign (int gridIndex);
        void confirmDelete();
        void withAudio (int slot, std::function<void (std::shared_ptr<const AudioData>)>);
        void setStatus (const juce::String&);
        void paintFacts (juce::Graphics&, juce::Rectangle<float>) const;
        juce::Rectangle<float> localF (const layout::Rect&) const;
        juce::Rectangle<float> cancelWord() const;
        juce::String armedInstruction() const;

        AppModel& model;
        WriteGate& gate;
        Ep40Library& lib;
        layout::Frame frame;
        layout::LoadLayout L;

        StepKeyRow projects, groups;
        PadGrid padGrid;
        BrowserList browser;
        ActionKey refreshKey { "refresh" }, auditionKey { "audition", Pict::play }, saveKey { "save to library", Pict::folder },
                  assignKey { "assign to pad", Pict::pads, ActionKey::Variant::deviceWrite },
                  deleteKey { "delete", Pict::cross, ActionKey::Variant::destructive };

        int group = 0;
        int category = -1;          // -1 = all
        int pendingProject = 1;     // PROJECT encoder; press reads its pads
        int shownProject = -1;
        int targetPad = 0;          // PAD encoder
        bool armed = false;
        bool armedPose = false;     // HALO_UI_POSE=armed:1 (debug shots only)
        std::vector<int> visible;   // indices into lib.sounds()
        std::map<int, std::shared_ptr<const AudioData>> cache;
        juce::String status;
    };
}
