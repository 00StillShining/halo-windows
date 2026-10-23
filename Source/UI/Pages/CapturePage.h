#pragma once

#include "App/AppModel.h"
#include "Core/UiLayout.h"
#include "UI/Design/EncoderBus.h"
#include "UI/Design/Motion.h"
#include "UI/Design/Primitives/ActionKey.h"
#include "UI/Design/Primitives/StepKeyRow.h"

#include <juce_gui_basics/juce_gui_basics.h>

namespace halo
{
    // CAPTURE (Ctrl+4; docs/redesign/plan.md section 3.10): the recorder and
    // Halo's local library. The tape machine (counter, reels, head, the `in`
    // input ladders and, with PRINT FX, the `print` ladders, the crimson REC
    // lamp, GRAB lengths, the last take with `show`), then the searchable
    // library (kind and sort chips, 40 px rows with a kind tag and a lazy
    // thumbnail), its message line and edit / chop / forget. Audio files
    // dropped anywhere on the page are imported.
    class CapturePage : public juce::Component, public juce::FileDragAndDropTarget, private juce::ChangeListener,
                        private AppModel::Listener, private juce::Timer
    {
    public:
        CapturePage (AppModel&);
        ~CapturePage() override;

        void setFrame (const layout::Frame&);
        std::array<std::optional<EncoderBinding>, 4> bindings();
        void paintLane (juce::Graphics&, juce::Rectangle<float>, bool compact) const;

        void paint (juce::Graphics&) override;
        void paintOverChildren (juce::Graphics&) override;
        void resized() override;
        bool keyPressed (const juce::KeyPress&) override;

        bool isInterestedInFileDrag (const juce::StringArray&) override;
        void fileDragEnter (const juce::StringArray&, int, int) override { dropHover = true; repaint(); }
        void fileDragExit (const juce::StringArray&) override { dropHover = false; repaint(); }
        void filesDropped (const juce::StringArray&, int, int) override;

        class List;

    private:
        void changeListenerCallback (juce::ChangeBroadcaster*) override { refresh(); }
        void statusChanged() override { refresh(); }
        void timerCallback() override;
        void refresh();
        void rebuildView();
        const Library::Item* selectedItem() const;
        void audition();
        void openSelected (bool chop);
        void import();
        juce::Rectangle<float> localF (const layout::Rect&) const;
        void paintMachine (juce::Graphics&, juce::Rectangle<float>);

        AppModel& model;
        layout::Frame frame;
        layout::CaptureLayout L;

        std::unique_ptr<List> list;
        juce::TextEditor search;
        StepKeyRow kinds, sorts, grabLengths;
        ActionKey emptyImportKey { "import audio files", Pict::file, ActionKey::Variant::primary };
        struct EmptyLayout { juce::Rectangle<float> frame, motif, text; juce::Rectangle<int> key; bool roomy = true; };
        EmptyLayout emptyLayout() const;
        ActionKey importKey { "import", Pict::file }, folderKey { "folder", Pict::folder }, recKey { "rec", Pict::rec },
                  monitorKey { "monitor", Pict::monitor }, grabKey { "grab", Pict::grab }, showKey { "show" },
                  editKey { "edit", Pict::edit }, chopKey { "chop", Pict::chop }, forgetKey { "forget", Pict::cross };
        std::unique_ptr<juce::FileChooser> chooser;
        juce::ThreadPool decodePool { 1 };
        std::shared_ptr<bool> alive = std::make_shared<bool> (true);

        Library::Filter filter = Library::Filter::all;
        Library::Sort sort = Library::Sort::date;
        std::vector<int> view;      // indices into model.library().items()
        juce::String message;
        bool dropHover = false;
        Spinner reels;            // tape: spins up on rec, runs down after
        void updateTimer();
    };
}
