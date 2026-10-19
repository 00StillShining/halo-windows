#include "UI/LoadPanel.h"
#include "Audio/SampleIO.h"
#include "Core/Ep40MidiMapping.h"
#include "UI/HaloColours.h"
#include "UI/HaloLookAndFeel.h"

namespace halo
{
    namespace
    {
        const auto& c = HaloColours::graphPaper;

        juce::String mb (juce::int64 bytes) { return juce::String ((double) bytes / (1024.0 * 1024.0), 1) + " MB"; }
    }

    LoadPanel::LoadPanel (AppModel& m) : model (m), lib (m.device())
    {
        list.setRowHeight (30);
        list.setColour (juce::ListBox::backgroundColourId, juce::Colours::transparentBlack);
        list.setColour (juce::ListBox::outlineColourId, c.ink.withAlpha (0.14f));
        list.setOutlineThickness (1);
        addAndMakeVisible (list);

        refreshButton.onClick = [this] { cache.clear(); lib.sync(); };
        auditionButton.onClick = [this] { audition (list.getSelectedRow()); };
        saveButton.onClick = [this] { saveToLibrary (list.getSelectedRow()); };
        assignButton.onClick = [this]
        {
            assignMode = ! assignMode && selectedSound() != nullptr && model.writesUnlocked();
            status = assignMode ? "Click a pad to put \"" + selectedSound()->name + "\" on it (project " + juce::String (lib.activeProject()) + ")" : juce::String();
            refresh();
        };
        deleteButton.onClick = [this]
        {
            const auto* s = selectedSound();
            if (s == nullptr || ! model.writesUnlocked())
                return;
            const int slot = s->slot;
            const auto name = s->name;
            juce::AlertWindow::showOkCancelBox (juce::MessageBoxIconType::WarningIcon, "Delete from EP-40",
                "Delete sound " + juce::String (slot) + " \"" + name + "\" from the EP-40?" + juce::newLine + juce::newLine
                    + "Your Halo backup still has a copy. Pads that use it will be silent.",
                "Delete", "Cancel", this,
                juce::ModalCallbackFunction::create ([this, slot, name] (int result)
                {
                    if (result == 0)
                        return;
                    status = "Deleting...";
                    cache.erase (slot);
                    model.deviceWriter().deleteSound (slot, [this, name] (Ep40Writer::Result r)
                    {
                        status = r.ok ? "Deleted \"" + name + "\"" : "Delete failed: " + r.message;
                        refresh();
                    });
                    refresh();
                }));
        };
        for (auto* b : { &refreshButton, &auditionButton, &saveButton, &assignButton, &deleteButton })
            addAndMakeVisible (b);

        for (size_t i = 0; i < projectButtons.size(); ++i)
        {
            projectButtons[i].setButtonText (juce::String ((int) i + 1));
            projectButtons[i].onClick = [this, i] { lib.readPads ((int) i + 1); };
            addAndMakeVisible (projectButtons[i]);
        }
        for (size_t i = 0; i < groupButtons.size(); ++i)
        {
            groupButtons[i].setButtonText (juce::String (ep40::groupLabels[i].data()));
            groupButtons[i].onClick = [this, i] { group = (int) i; refresh(); };
            addAndMakeVisible (groupButtons[i]);
        }

        lib.addChangeListener (this);
        model.backup().addChangeListener (this);
        refresh();
    }

    LoadPanel::~LoadPanel()
    {
        lib.removeChangeListener (this);
        model.backup().removeChangeListener (this);
    }

    bool LoadPanel::deviceAvailable() const
    {
        return model.canTalkToDevice() && lib.state() != Ep40Library::State::idle;
    }

    void LoadPanel::rebuildVisible()
    {
        visible.clear();
        const auto& sounds = lib.sounds();
        const auto& cats = lib.info().categories;
        for (int i = 0; i < (int) sounds.size(); ++i)
        {
            const int slot = sounds[(size_t) i].slot;
            if (category < 0 || category >= (int) cats.size()
                || (slot >= cats[(size_t) category].firstSlot && slot <= cats[(size_t) category].lastSlot))
                visible.push_back (i);
        }
    }

    void LoadPanel::refresh()
    {
        // Category tabs come from the device itself.
        const auto& cats = lib.info().categories;
        if (categoryButtons.size() != (int) cats.size() + 1)
        {
            categoryButtons.clear();
            for (int i = -1; i < (int) cats.size(); ++i)
            {
                auto* b = categoryButtons.add (new juce::TextButton (i < 0 ? juce::String ("ALL") : cats[(size_t) i].name));
                b->onClick = [this, i] { category = i; refresh(); };
                addAndMakeVisible (b);
            }
            resized();
        }
        for (int i = 0; i < categoryButtons.size(); ++i)
            categoryButtons[i]->setToggleState (i - 1 == category, juce::dontSendNotification);
        for (size_t i = 0; i < projectButtons.size(); ++i)
            projectButtons[i].setToggleState (lib.activeProject() == (int) i + 1, juce::dontSendNotification);
        for (size_t i = 0; i < groupButtons.size(); ++i)
            groupButtons[i].setToggleState (group == (int) i, juce::dontSendNotification);

        const bool available = deviceAvailable();
        for (auto* comp : std::initializer_list<juce::Component*> { &list, &refreshButton, &auditionButton, &saveButton, &assignButton, &deleteButton })
            comp->setVisible (available || comp == &refreshButton);
        assignButton.setEnabled (model.writesUnlocked());
        deleteButton.setEnabled (model.writesUnlocked());
        assignButton.setToggleState (assignMode, juce::dontSendNotification);
        assignButton.setTooltip (model.writesUnlocked() ? juce::String() : juce::String ("Back up the EP-40 in BACKUPS to unlock"));
        refreshButton.setEnabled (model.canTalkToDevice() && lib.state() != Ep40Library::State::syncing);
        for (auto& b : projectButtons) b.setVisible (available);
        for (auto& b : groupButtons) b.setVisible (available);
        for (auto* b : categoryButtons) b->setVisible (available);

        rebuildVisible();
        list.updateContent();
        list.repaint();
        repaint();
    }

    const Ep40Library::Sound* LoadPanel::selectedSound() const
    {
        const int row = list.getSelectedRow();
        if (! juce::isPositiveAndBelow (row, (int) visible.size()))
            return nullptr;
        return &lib.sounds()[(size_t) visible[(size_t) row]];
    }

    void LoadPanel::withAudio (int slot, std::function<void (std::shared_ptr<const AudioData>)> use)
    {
        if (const auto it = cache.find (slot); it != cache.end())
            return use (it->second);
        status = "Downloading sound " + juce::String (slot) + "...";
        repaint();
        lib.download (slot, [this, slot, use] (std::optional<AudioData> audio, juce::String error)
        {
            if (! audio)
            {
                status = error;
                repaint();
                return;
            }
            auto shared = std::make_shared<const AudioData> (std::move (*audio));
            cache[slot] = shared;
            status.clear();
            repaint();
            use (shared);
        });
    }

    void LoadPanel::audition (int row)
    {
        if (! juce::isPositiveAndBelow (row, (int) visible.size()))
            return;
        const auto& s = lib.sounds()[(size_t) visible[(size_t) row]];
        withAudio (s.slot, [this, slot = s.slot] (std::shared_ptr<const AudioData> audio)
        {
            model.editor().playOther (std::move (audio), 10000 + slot);
        });
    }

    void LoadPanel::saveToLibrary (int row)
    {
        if (! juce::isPositiveAndBelow (row, (int) visible.size()))
            return;
        const auto& s = lib.sounds()[(size_t) visible[(size_t) row]];
        withAudio (s.slot, [this, slot = s.slot, name = s.name] (std::shared_ptr<const AudioData> audio)
        {
            const auto folder = juce::File::getSpecialLocation (juce::File::userMusicDirectory)
                                    .getChildFile ("Halo").getChildFile ("From EP-40");
            folder.createDirectory();
            const auto file = folder.getNonexistentChildFile (juce::String (slot).paddedLeft ('0', 3) + " "
                                                                  + (name.isNotEmpty() ? name : juce::String ("sound")), ".wav");
            if (SampleIO::writeWav16 (*audio, file))
            {
                model.library().importFiles ({ file.getFullPathName() });
                status = "Saved " + file.getFileName() + " to the library (CAPTURE)";
            }
            else
            {
                status = "Couldn't write " + file.getFullPathName();
            }
            repaint();
        });
    }

    juce::Rectangle<float> LoadPanel::padCell (int gridIndex) const
    {
        const float w = (float) padsArea.getWidth() / 3.0f, h = (float) padsArea.getHeight() / 4.0f;
        return { (float) padsArea.getX() + (float) (gridIndex % 3) * w, (float) padsArea.getY() + (float) (gridIndex / 3) * h, w, h };
    }

    void LoadPanel::mouseUp (const juce::MouseEvent& e)
    {
        // Clicking a pad selects (and plays) the sound on it.
        for (int i = 0; i < 12; ++i)
            if (padCell (i).contains (e.position))
            {
                if (assignMode)
                {
                    assignMode = false;
                    if (const auto* s = selectedSound(); s != nullptr && model.writesUnlocked())
                    {
                        status = "Assigning...";
                        model.deviceWriter().assignPad (lib.activeProject(), group, i, s->slot, [this, i, name = s->name] (Ep40Writer::Result r)
                        {
                            status = r.ok ? "\"" + name + "\" is now on pad " + juce::String (ep40::groupLabels[(size_t) group].data())
                                                + " " + juce::String (ep40::gridLabels[(size_t) i].data())
                                          : "Assign failed: " + r.message;
                            refresh();
                        });
                    }
                    refresh();
                    return;
                }
                const int slot = lib.padSound (group, i);
                if (slot <= 0)
                    return;
                category = -1;
                rebuildVisible();
                list.updateContent();
                for (int row = 0; row < (int) visible.size(); ++row)
                    if (lib.sounds()[(size_t) visible[(size_t) row]].slot == slot)
                    {
                        list.selectRow (row);
                        list.scrollToEnsureRowIsOnscreen (row);
                        audition (row);
                    }
                refresh();
                return;
            }
    }

    void LoadPanel::resized()
    {
        auto area = getLocalBounds().reduced (16, 12);
        fallbackArea = area;
        auto header = area.removeFromTop (34);
        refreshButton.setBounds (header.removeFromRight (110).reduced (2));
        headerArea = header;
        storageArea = area.removeFromTop (34);
        area.removeFromTop (4);

        auto projects = area.removeFromTop (30);
        projects.removeFromLeft (70);
        for (auto& b : projectButtons)
            b.setBounds (projects.removeFromLeft (juce::jmin (40, projects.getWidth() / 9 + 1)).reduced (1));
        auto groups = area.removeFromTop (32);
        groups.removeFromLeft (70);
        for (auto& b : groupButtons)
            b.setBounds (groups.removeFromLeft (60).reduced (2));
        area.removeFromTop (4);
        padsArea = area.removeFromTop (juce::jmin (200, area.getHeight() / 3));
        area.removeFromTop (8);

        auto cats = area.removeFromTop (categoryButtons.size() > 6 ? 60 : 30);
        const int perRow = juce::jmax (1, (categoryButtons.size() + 1) / 2);
        for (int i = 0; i < categoryButtons.size(); ++i)
        {
            const int r = i / perRow, col = i % perRow;
            const int w = cats.getWidth() / perRow;
            categoryButtons[i]->setBounds (cats.getX() + col * w, cats.getY() + r * 30, w, 30);
            categoryButtons[i]->setBounds (categoryButtons[i]->getBounds().reduced (1, 2));
        }
        area.removeFromTop (4);

        statusArea = area.removeFromBottom (22);
        auto actions = area.removeFromBottom (38);
        auditionButton.setBounds (actions.removeFromLeft (120).reduced (2, 3));
        saveButton.setBounds (actions.removeFromLeft (170).reduced (2, 3));
        assignButton.setBounds (actions.removeFromLeft (150).reduced (2, 3));
        deleteButton.setBounds (actions.removeFromRight (90).reduced (2, 3));
        area.removeFromBottom (4);
        list.setBounds (area);
    }

    void LoadPanel::paintListBoxItem (int row, juce::Graphics& g, int width, int height, bool selected)
    {
        if (! juce::isPositiveAndBelow (row, (int) visible.size()))
            return;
        const auto& s = lib.sounds()[(size_t) visible[(size_t) row]];
        if (selected)
        {
            g.setColour (c.orange.withAlpha (0.12f));
            g.fillRect (0, 0, width, height);
            g.setColour (c.orange);
            g.fillRect (0, 0, 3, height);
        }
        auto r = juce::Rectangle<int> (0, 0, width, height).reduced (10, 2);
        g.setFont (HaloType::mono (12.0f));
        g.setColour (c.inkSoft);
        g.drawText (juce::String (s.slot).paddedLeft ('0', 3), r.removeFromLeft (40), juce::Justification::centredLeft);
        g.drawText (juce::String (s.seconds(), 2) + " s  " + (s.channels == 2 ? "ST" : "M"),
                    r.removeFromRight (100), juce::Justification::centredRight);
        g.setFont (HaloType::body (13.0f));
        g.setColour (c.ink);
        g.drawText (s.name, r, juce::Justification::centredLeft, true);
    }

    void LoadPanel::paint (juce::Graphics& g)
    {
        const auto& info = lib.info();

        if (! deviceAvailable())
        {
            auto area = fallbackArea.withTrimmedTop (44);
            HaloLookAndFeel::drawPanel (g, area.withHeight (juce::jmin (area.getHeight(), 230)).toFloat(), c.paper);
            area.reduce (20, 18);
            g.setFont (HaloType::label (11.0f));
            g.setColour (c.orange);
            g.drawText ("EP-40 LIBRARY", area.removeFromTop (18), juce::Justification::centredLeft);
            area.removeFromTop (8);
            g.setFont (HaloType::body (14.0f));
            g.setColour (c.ink);
            const auto why = model.isMock() ? juce::String ("Halo is running on the MOCK source, so there's no real EP-40 to read.")
                           : ! model.isConnected() ? juce::String ("Connect the EP-40 over USB. Halo reads its sounds and pads automatically.")
                           : ! model.midi().canSend() ? juce::String ("The EP-40's MIDI output is in use by another app (a DAW or TE's tool). Close it and replug.")
                           : juce::String ("Waiting for the EP-40 to answer...");
            g.drawFittedText (why, area.removeFromTop (60), juce::Justification::topLeft, 3);
            g.setFont (HaloType::label (11.0f, false));
            g.setColour (c.inkSoft);
            g.drawText ("EP-40", headerArea, juce::Justification::centredLeft);
            return;
        }

        // Header: device and state.
        g.setFont (HaloType::label (11.0f));
        g.setColour (c.inkSoft);
        auto header = headerArea;
        g.drawText ("EP-40 LIBRARY", header.removeFromTop (16), juce::Justification::centredLeft);
        g.setFont (HaloType::mono (12.0f));
        g.setColour (lib.state() == Ep40Library::State::failed ? c.warning : c.ink);
        g.drawText (lib.state() == Ep40Library::State::syncing ? lib.progress()
                    : lib.state() == Ep40Library::State::failed ? lib.error()
                    : info.product + "  OS " + info.os + "  " + juce::String ((int) lib.sounds().size()) + " sounds",
                    header, juce::Justification::centredLeft, true);

        // Storage bar.
        if (info.capacityBytes > 0 && info.freeBytes >= 0)
        {
            auto storage = storageArea; // never trim the stored layout rect while painting
            auto bar = storage.removeFromTop (12).toFloat();
            const float used = 1.0f - (float) info.freeBytes / (float) info.capacityBytes;
            g.setColour (c.paperHigh);
            g.fillRect (bar);
            g.setColour (used > 0.9f ? c.warning : c.riddimGreen);
            g.fillRect (bar.withWidth (bar.getWidth() * used));
            g.setColour (c.ink.withAlpha (0.2f));
            g.drawRect (bar, 1.0f);
            g.setFont (HaloType::mono (11.0f));
            g.setColour (c.inkSoft);
            g.drawText (mb (info.capacityBytes - info.freeBytes) + " used, " + mb (info.freeBytes) + " free of " + mb (info.capacityBytes),
                        storage, juce::Justification::centredLeft, true);
        }

        g.setFont (HaloType::label (11.0f, false));
        g.setColour (c.inkSoft);
        g.drawText ("PROJECT", projectButtons[0].getBounds().withX (16).withWidth (68), juce::Justification::centredLeft);
        g.drawText ("GROUP", groupButtons[0].getBounds().withX (16).withWidth (68), juce::Justification::centredLeft);

        // Pads of the chosen group: grid label, slot, sound name.
        for (int i = 0; i < 12; ++i)
        {
            const auto r = padCell (i).reduced (3.0f);
            const int slot = lib.padSound (group, i);
            const auto* s = slot > 0 ? lib.sound (slot) : nullptr;
            g.setColour (c.ink.withAlpha (0.2f));
            g.fillRoundedRectangle (r.translated (2.0f, 2.0f), 3.0f);
            g.setColour (slot > 0 ? c.paperHigh : c.paper.darker (0.04f));
            g.fillRoundedRectangle (r, 3.0f);
            g.setFont (HaloType::label (10.0f));
            g.setColour (c.inkSoft);
            g.drawText (juce::String (ep40::gridLabels[(size_t) i].data()), r.reduced (5.0f, 3.0f), juce::Justification::topLeft);
            if (slot > 0)
            {
                g.drawText (juce::String (slot), r.reduced (5.0f, 3.0f), juce::Justification::topRight);
                g.setFont (HaloType::body (12.0f));
                g.setColour (c.ink);
                g.drawFittedText (s != nullptr ? s->name : juce::String ("sound ") + juce::String (slot),
                                  r.reduced (5.0f, 3.0f).withTrimmedTop (12.0f).toNearestInt(), juce::Justification::centredLeft, 2, 0.9f);
            }
            else if (slot == 0)
            {
                g.setFont (HaloType::mono (10.0f));
                g.drawText ("empty", r, juce::Justification::centred);
            }
        }

        g.setFont (HaloType::mono (11.5f));
        g.setColour (c.ink);
        g.drawText (status, statusArea, juce::Justification::centredLeft, true);
    }
}
