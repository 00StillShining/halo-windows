#pragma once

#include "App/AppModel.h"

#include <juce_gui_basics/juce_gui_basics.h>

namespace halo
{
    // BACKUPS mode (BackupsRail.swift): back up the whole EP-40 (every sound
    // and project) to the PC, see past backups, open their folders. A complete
    // backup of the connected device unlocks Halo's write features.
    class BackupsPanel : public juce::Component, private juce::ListBoxModel, private juce::ChangeListener, private juce::Timer
    {
    public:
        explicit BackupsPanel (AppModel&);
        ~BackupsPanel() override;

        void paint (juce::Graphics&) override;
        void resized() override;
        void visibilityChanged() override { if (isVisible()) reload(); }

    private:
        int getNumRows() override { return (int) entries.size(); }
        void paintListBoxItem (int row, juce::Graphics&, int width, int height, bool selected) override;
        void listBoxItemDoubleClicked (int row, const juce::MouseEvent&) override;
        void changeListenerCallback (juce::ChangeBroadcaster*) override;
        void timerCallback() override { repaint(); }
        void reload();

        AppModel& model;
        juce::ListBox list { "backups", this };
        juce::TextButton backupButton { "BACK UP NOW" }, cancelButton { "CANCEL" }, folderButton { "OPEN FOLDER" };
        std::vector<Ep40Backup::Entry> entries;
        juce::Rectangle<int> headerArea, progressArea, explainArea;
    };
}
