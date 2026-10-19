#include "UI/BackupsPanel.h"
#include "UI/HaloColours.h"

namespace halo
{
    namespace
    {
        const auto& c = HaloColours::graphPaper;
    }

    BackupsPanel::BackupsPanel (AppModel& m) : model (m)
    {
        list.setRowHeight (44);
        list.setColour (juce::ListBox::backgroundColourId, juce::Colours::transparentBlack);
        list.setColour (juce::ListBox::outlineColourId, c.ink.withAlpha (0.14f));
        list.setOutlineThickness (1);
        addAndMakeVisible (list);

        backupButton.onClick = [this] { model.backup().start(); };
        cancelButton.onClick = [this] { model.backup().cancel(); };
        folderButton.onClick = []
        {
            Ep40Backup::backupsFolder().createDirectory();
            Ep40Backup::backupsFolder().revealToUser();
        };
        for (auto* b : { &backupButton, &cancelButton, &folderButton })
            addAndMakeVisible (b);

        model.backup().addChangeListener (this);
        model.device().addChangeListener (this);
        reload();
    }

    BackupsPanel::~BackupsPanel()
    {
        model.backup().removeChangeListener (this);
        model.device().removeChangeListener (this);
    }

    void BackupsPanel::reload()
    {
        model.backup().rescanAsync();
        entries = model.backup().entries();
        list.updateContent();
        changeListenerCallback (nullptr);
    }

    void BackupsPanel::changeListenerCallback (juce::ChangeBroadcaster*)
    {
        auto& b = model.backup();
        const bool ready = model.device().state() == Ep40Library::State::ready;
        backupButton.setEnabled (ready && ! b.isRunning());
        cancelButton.setVisible (b.isRunning());
        if (b.isRunning()) startTimerHz (4); else stopTimer();
        if (entries != b.entries())
        {
            entries = b.entries();
            list.updateContent();
        }
        list.repaint();
        repaint();
    }

    void BackupsPanel::resized()
    {
        auto area = getLocalBounds().reduced (16, 12);
        headerArea = area.removeFromTop (40);
        auto buttons = area.removeFromTop (40);
        backupButton.setBounds (buttons.removeFromLeft (150).reduced (2));
        cancelButton.setBounds (buttons.removeFromLeft (110).reduced (2));
        folderButton.setBounds (buttons.removeFromRight (140).reduced (2));
        progressArea = area.removeFromTop (44);
        explainArea = area.removeFromTop (70);
        area.removeFromTop (6);
        list.setBounds (area);
    }

    void BackupsPanel::paintListBoxItem (int row, juce::Graphics& g, int width, int height, bool selected)
    {
        if (! juce::isPositiveAndBelow (row, (int) entries.size()))
            return;
        const auto& e = entries[(size_t) row];
        if (selected)
        {
            g.setColour (c.orange.withAlpha (0.12f));
            g.fillRect (0, 0, width, height);
        }
        g.setColour (c.ink.withAlpha (0.08f));
        g.fillRect (0, height - 1, width, 1);
        auto r = juce::Rectangle<int> (0, 0, width, height).reduced (10, 4);

        g.setFont (HaloType::label (10.0f));
        g.setColour (e.complete ? c.riddimGreen : c.warning);
        g.drawText (e.complete ? "COMPLETE" : "INCOMPLETE", r.removeFromRight (100), juce::Justification::centredRight);
        g.setFont (HaloType::body (13.0f));
        g.setColour (c.ink);
        g.drawText (e.folder.getFileName(), r.removeFromTop (r.getHeight() / 2), juce::Justification::centredLeft, true);
        g.setFont (HaloType::mono (11.0f));
        g.setColour (c.inkSoft);
        g.drawText (juce::String (e.sounds) + " sounds, " + juce::String (e.projects) + " projects, OS " + e.os,
                    r, juce::Justification::centredLeft, true);
    }

    void BackupsPanel::listBoxItemDoubleClicked (int row, const juce::MouseEvent&)
    {
        if (juce::isPositiveAndBelow (row, (int) entries.size()))
            entries[(size_t) row].folder.revealToUser();
    }

    void BackupsPanel::paint (juce::Graphics& g)
    {
        auto header = headerArea;
        g.setFont (HaloType::label (11.0f));
        g.setColour (c.inkSoft);
        g.drawText ("BACKUPS", header.removeFromTop (16), juce::Justification::centredLeft);

        const auto& info = model.device().info();
        const bool backedUp = model.backup().hasCompleteBackup (info.serial);
        g.setFont (HaloType::mono (12.0f));
        g.setColour (backedUp ? c.riddimGreen : c.ink);
        g.drawText (model.device().state() != Ep40Library::State::ready ? juce::String ("Connect the EP-40 (see LOAD) to back it up")
                    : backedUp ? "This EP-40 (" + info.serial + ") has a complete backup: writes are unlocked"
                               : "This EP-40 (" + info.serial + ") has no complete backup yet: writes are locked",
                    header, juce::Justification::centredLeft, true);

        auto& b = model.backup();
        auto progress = progressArea;
        if (b.isRunning() || b.status().isNotEmpty())
        {
            auto bar = progress.removeFromTop (12).toFloat().reduced (0, 1);
            g.setColour (c.paperHigh);
            g.fillRect (bar);
            g.setColour (c.riddimGreen);
            g.fillRect (bar.withWidth (bar.getWidth() * (float) b.progress()));
            g.setColour (c.ink.withAlpha (0.2f));
            g.drawRect (bar, 1.0f);
            g.setFont (HaloType::mono (11.5f));
            g.setColour (b.status().startsWith ("Backup complete") || b.isRunning() ? c.ink : c.warning);
            g.drawFittedText (b.status(), progress, juce::Justification::centredLeft, 2, 1.0f);
        }

        g.setFont (HaloType::body (13.0f));
        g.setColour (c.inkSoft);
        g.drawFittedText ("Copies every sound (bit-exact WAV + its settings) and all 9 projects to Documents\\Halo\\Backups. "
                          "Read-only: nothing on the EP-40 changes. A full backup takes a while (about 1 MB every 12 s over USB MIDI); "
                          "keep the EP-40 connected and awake.",
                          explainArea, juce::Justification::topLeft, 4);
    }
}
