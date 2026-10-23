#pragma once

#include "App/AppModel.h"
#include "Core/UiLayout.h"
#include "UI/Design/EncoderBus.h"
#include "UI/Design/Motion.h"
#include "UI/Design/Primitives/ActionKey.h"

#include <juce_gui_basics/juce_gui_basics.h>

namespace halo
{
    // BACKUPS (Ctrl+6; docs/redesign/plan.md section 3.12): the hero lock
    // plate (no device / locked / unlocked, with back up now, cancel and
    // open folder), then either the running backup (big reels, one dot per
    // project and sound, the count in Numerals XL, today's status line) or
    // the shelf of cassettes (COMPLETE ink tags; INCOMPLETE hatched outline
    // tags; crimson only for a run that failed with an error), and today's
    // explainer. Everything shown comes from the cached Ep40Backup view.
    class BackupsPage : public juce::Component, private juce::ChangeListener, private juce::Timer
    {
    public:
        explicit BackupsPage (AppModel&);
        ~BackupsPage() override;

        void setFrame (const layout::Frame&);
        std::array<std::optional<EncoderBinding>, 4> bindings();
        void paintLane (juce::Graphics&, juce::Rectangle<float>, bool compact) const;

        void paint (juce::Graphics&) override;
        void resized() override;
        void mouseDown (const juce::MouseEvent&) override;
        void mouseDoubleClick (const juce::MouseEvent&) override;
        void mouseWheelMove (const juce::MouseEvent&, const juce::MouseWheelDetails&) override;
        bool keyPressed (const juce::KeyPress&) override;

        // What the page shows; real state, or a debug shot pose.
        enum class Plate { noDevice, locked, unlocked };
        struct View
        {
            Plate plate = Plate::noDevice;
            juce::String serial;
            bool running = false;
            int done = 0, total = 0;
            double secondsLeft = -1.0;
            juce::String status;
            bool failed = false;          // the last run ended with an error (not a cancel)
            std::vector<Ep40Backup::Entry> shelf;
        };

    private:
        void changeListenerCallback (juce::ChangeBroadcaster*) override { refresh(); }
        void timerCallback() override;
        void refresh();
        View currentView() const;
        juce::Rectangle<float> localF (const layout::Rect&) const;
        juce::Rectangle<float> cassetteBounds (int index) const;
        int cassetteAt (juce::Point<float>) const;
        int columns() const;
        void paintPlate (juce::Graphics&, juce::Rectangle<float>, const View&);
        void paintProgress (juce::Graphics&, juce::Rectangle<float>, const View&);
        void paintShelf (juce::Graphics&, juce::Rectangle<float>, const View&);
        void paintCassette (juce::Graphics&, juce::Rectangle<float>, const Ep40Backup::Entry*, bool selected, bool failedRun);
        static juce::String explainer();

        AppModel& model;
        layout::Frame frame;
        layout::BackupsLayout L;
        ActionKey backupKey { "back up now", Pict::backup, ActionKey::Variant::primary }, cancelKey { "cancel", Pict::cross },
                  folderKey { "open folder", Pict::folder }, showKey { "show" };
        int selected = -1, firstRow = 0;
        float wheelAcc = 0.0f;
        int pose = -1; // HALO_UI_POSE=backup:1..5 (debug shots)
        Spinner reels;
    };
}
