#pragma once

#include "App/AppModel.h"

#include <juce_gui_basics/juce_gui_basics.h>

#include <map>

namespace halo
{
    // LOAD mode: the EP-40's own library, read over TE's protocol. Storage,
    // the active project's pads by group, every sound by the device's own
    // categories, audition and save-to-library. (The Mac app showed mock
    // data here.) Without a connected EP-40 it explains what's needed.
    class LoadPanel : public juce::Component, private juce::ListBoxModel, private juce::ChangeListener
    {
    public:
        explicit LoadPanel (AppModel&);
        ~LoadPanel() override;

        void paint (juce::Graphics&) override;
        void resized() override;
        void mouseUp (const juce::MouseEvent&) override;

    private:
        int getNumRows() override { return (int) visible.size(); }
        void paintListBoxItem (int row, juce::Graphics&, int width, int height, bool selected) override;
        void listBoxItemDoubleClicked (int row, const juce::MouseEvent&) override { audition (row); }
        void selectedRowsChanged (int) override { repaint(); }
        void changeListenerCallback (juce::ChangeBroadcaster*) override { refresh(); }

        void refresh();
        void rebuildVisible();
        void audition (int row);
        void saveToLibrary (int row);
        void withAudio (int slot, std::function<void (std::shared_ptr<const AudioData>)>);
        juce::Rectangle<float> padCell (int gridIndex) const;
        const Ep40Library::Sound* selectedSound() const;
        bool deviceAvailable() const;

        AppModel& model;
        Ep40Library& lib;
        juce::ListBox list { "ep40 sounds", this };
        juce::TextButton refreshButton { "REFRESH" }, auditionButton { "AUDITION" }, saveButton { "SAVE TO LIBRARY" }, assignButton { "ASSIGN TO PAD" }, deleteButton { "DELETE" };
        bool assignMode = false;
        std::array<juce::TextButton, 9> projectButtons;
        std::array<juce::TextButton, 4> groupButtons;
        juce::OwnedArray<juce::TextButton> categoryButtons;
        int group = 0;
        int category = -1; // -1 = all sounds
        std::vector<int> visible; // indices into lib.sounds()
        std::map<int, std::shared_ptr<const AudioData>> cache;
        juce::String status;
        juce::Rectangle<int> headerArea, storageArea, padsArea, statusArea, fallbackArea;
    };
}
