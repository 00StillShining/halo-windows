#pragma once

#include "App/AppModel.h"

#include <juce_gui_basics/juce_gui_basics.h>

namespace halo
{
    // CAPTURE mode (CaptureRail.swift): every local take, grab and imported
    // file, newest first. Drop audio files here (or IMPORT) to add them.
    class LibraryPanel : public juce::Component,
                         public juce::FileDragAndDropTarget,
                         private juce::ListBoxModel,
                         private juce::ChangeListener
    {
    public:
        explicit LibraryPanel (AppModel&);
        ~LibraryPanel() override;

        void paint (juce::Graphics&) override;
        void resized() override;

        bool isInterestedInFileDrag (const juce::StringArray&) override;
        void fileDragEnter (const juce::StringArray&, int, int) override { dropHover = true; repaint(); }
        void fileDragExit (const juce::StringArray&) override { dropHover = false; repaint(); }
        void filesDropped (const juce::StringArray&, int, int) override;

    private:
        int getNumRows() override;
        void paintListBoxItem (int row, juce::Graphics&, int width, int height, bool selected) override;
        void listBoxItemDoubleClicked (int row, const juce::MouseEvent&) override;
        void selectedRowsChanged (int) override { updateButtons(); }
        void changeListenerCallback (juce::ChangeBroadcaster*) override;
        void updateButtons();
        const Library::Item* selectedItem() const;

        AppModel& model;
        juce::ListBox list { "library", this };
        juce::TextButton importButton { "IMPORT" }, folderButton { "FOLDER" },
                         editButton { "EDIT" }, chopButton { "CHOP" }, forgetButton { "FORGET" };
        std::unique_ptr<juce::FileChooser> chooser;
        juce::String message;
        juce::Rectangle<int> headerArea, messageArea;
        bool dropHover = false;
    };
}
