#include "UI/LibraryPanel.h"
#include "Audio/SampleIO.h"
#include "Audio/SessionRecorder.h"
#include "UI/HaloColours.h"

namespace halo
{
    namespace
    {
        const auto& c = HaloColours::graphPaper;

        juce::String duration (double s)
        {
            const int whole = (int) s;
            return s < 60.0 ? juce::String (s, 1) + " s"
                            : juce::String (whole / 60) + ":" + juce::String (whole % 60).paddedLeft ('0', 2);
        }
    }

    LibraryPanel::LibraryPanel (AppModel& m) : model (m)
    {
        list.setRowHeight (40);
        list.setColour (juce::ListBox::backgroundColourId, juce::Colours::transparentBlack);
        list.setColour (juce::ListBox::outlineColourId, c.ink.withAlpha (0.14f));
        list.setOutlineThickness (1);
        addAndMakeVisible (list);

        importButton.onClick = [this]
        {
            chooser = std::make_unique<juce::FileChooser> ("Import audio into Halo", juce::File(), SampleIO::supportedPattern());
            chooser->launchAsync (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles
                                      | juce::FileBrowserComponent::canSelectMultipleItems,
                                  [this] (const juce::FileChooser& fc)
                                  {
                                      juce::StringArray paths;
                                      for (const auto& f : fc.getResults())
                                          paths.add (f.getFullPathName());
                                      filesDropped (paths, 0, 0);
                                  });
        };
        folderButton.onClick = [] { SessionRecorder::recordingsFolder().revealToUser(); };
        editButton.onClick = [this] { if (auto* item = selectedItem()) model.openInEditor (*item, false); };
        chopButton.onClick = [this] { if (auto* item = selectedItem()) model.openInEditor (*item, true); };
        forgetButton.onClick = [this] { if (auto* item = selectedItem()) model.library().forget (item->file); };

        for (auto* b : { &importButton, &folderButton, &editButton, &chopButton, &forgetButton })
            addAndMakeVisible (b);

        model.library().addChangeListener (this);
        updateButtons();
    }

    LibraryPanel::~LibraryPanel()
    {
        model.library().removeChangeListener (this);
    }

    const Library::Item* LibraryPanel::selectedItem() const
    {
        const int row = list.getSelectedRow();
        const auto& items = model.library().items();
        return juce::isPositiveAndBelow (row, (int) items.size()) ? &items[(size_t) row] : nullptr;
    }

    void LibraryPanel::updateButtons()
    {
        const auto* item = selectedItem();
        editButton.setEnabled (item != nullptr);
        chopButton.setEnabled (item != nullptr);
        forgetButton.setVisible (item != nullptr && item->kind == Library::Item::Kind::imported);
    }

    void LibraryPanel::changeListenerCallback (juce::ChangeBroadcaster*)
    {
        list.updateContent();
        list.repaint();
        updateButtons();
        repaint();
    }

    bool LibraryPanel::isInterestedInFileDrag (const juce::StringArray& files)
    {
        for (const auto& f : files)
            if (SampleIO::isSupported (juce::File (f)))
                return true;
        return false;
    }

    void LibraryPanel::filesDropped (const juce::StringArray& files, int, int)
    {
        dropHover = false;
        if (files.isEmpty())
            return;
        const int added = model.library().importFiles (files);
        message = added > 0 ? "Imported " + juce::String (added) + (added == 1 ? " file" : " files")
                            : "Nothing new to import (unsupported, or already here)";
        repaint();
    }

    void LibraryPanel::resized()
    {
        auto area = getLocalBounds().reduced (16, 12);
        headerArea = area.removeFromTop (18);
        area.removeFromTop (8);

        auto top = area.removeFromTop (34);
        importButton.setBounds (top.removeFromLeft (110).reduced (2));
        folderButton.setBounds (top.removeFromLeft (100).reduced (2));
        messageArea = area.removeFromTop (22);

        auto bottom = area.removeFromBottom (40);
        editButton.setBounds (bottom.removeFromLeft (100).reduced (2, 3));
        chopButton.setBounds (bottom.removeFromLeft (100).reduced (2, 3));
        forgetButton.setBounds (bottom.removeFromRight (100).reduced (2, 3));
        area.removeFromBottom (6);
        list.setBounds (area);
    }

    void LibraryPanel::paint (juce::Graphics& g)
    {
        g.setFont (HaloType::label (11.0f));
        g.setColour (c.inkSoft);
        g.drawText ("LIBRARY  " + juce::String ((int) model.library().items().size()), headerArea, juce::Justification::centredLeft);

        g.setFont (HaloType::mono (11.5f));
        g.setColour (message.isNotEmpty() ? c.ink : c.inkSoft);
        g.drawText (message.isNotEmpty() ? message : "takes, grabs and imported files. Drop audio here to import.",
                    messageArea, juce::Justification::centredLeft, true);

        if (model.library().items().empty())
        {
            g.setFont (HaloType::body (14.0f));
            g.setColour (c.inkSoft);
            g.drawFittedText ("Nothing here yet.\nRecord a take or GRAB in PLAY, or IMPORT / drop audio files here.",
                              list.getBounds().reduced (24), juce::Justification::centred, 3);
        }

        if (dropHover)
        {
            g.setColour (c.orange);
            g.drawRect (getLocalBounds().reduced (4), 2);
        }
    }

    int LibraryPanel::getNumRows()
    {
        return (int) model.library().items().size();
    }

    void LibraryPanel::paintListBoxItem (int row, juce::Graphics& g, int width, int height, bool selected)
    {
        const auto& items = model.library().items();
        if (! juce::isPositiveAndBelow (row, (int) items.size()))
            return;
        const auto& item = items[(size_t) row];

        if (selected)
        {
            g.setColour (c.orange.withAlpha (0.12f));
            g.fillRect (0, 0, width, height);
            g.setColour (c.orange);
            g.fillRect (0, 0, 3, height);
        }
        g.setColour (c.ink.withAlpha (0.08f));
        g.fillRect (0, height - 1, width, 1);

        auto r = juce::Rectangle<int> (0, 0, width, height).reduced (10, 4);
        auto tag = r.removeFromLeft (48);
        g.setFont (HaloType::label (10.0f));
        g.setColour (item.kind == Library::Item::Kind::grab ? c.orange
                     : item.kind == Library::Item::Kind::take ? c.riddimGreen : c.inkSoft);
        g.drawText (Library::kindLabel (item.kind), tag, juce::Justification::centredLeft);

        auto right = r.removeFromRight (120);
        g.setFont (HaloType::mono (11.0f));
        g.setColour (c.inkSoft);
        g.drawText (duration (item.seconds) + "  " + juce::String (item.sampleRate / 1000.0, 1) + "k"
                        + (item.channels == 1 ? " M" : " ST"),
                    right, juce::Justification::centredRight);

        g.setFont (HaloType::body (13.0f));
        g.setColour (c.ink);
        g.drawText (item.title, r, juce::Justification::centredLeft, true);
    }

    void LibraryPanel::listBoxItemDoubleClicked (int row, const juce::MouseEvent&)
    {
        const auto& items = model.library().items();
        if (juce::isPositiveAndBelow (row, (int) items.size()))
            model.openInEditor (items[(size_t) row], false);
    }
}
