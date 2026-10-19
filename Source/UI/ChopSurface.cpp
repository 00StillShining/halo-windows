#include "UI/ChopSurface.h"
#include "Audio/SampleIO.h"
#include "Core/Ep40MidiMapping.h"
#include "UI/HaloColours.h"
#include "UI/HaloLookAndFeel.h"

namespace halo
{
    namespace
    {
        const auto& c = HaloColours::graphPaper;
        constexpr Treatment treatments[] { Treatment::original, Treatment::high, Treatment::balanced,
                                           Treatment::loFi22k, Treatment::loFi11k };
        constexpr int sliceChoices[] { EditSession::sliceFit, 4, 8, 16, EditSession::sliceAll };
    }

    //==============================================================================
    juce::Rectangle<float> ChopSurface::PadGrid::cell (int gridIndex) const
    {
        const float w = (float) getWidth() / 3.0f, h = (float) getHeight() / 4.0f;
        return { (float) (gridIndex % 3) * w, (float) (gridIndex / 3) * h, w, h };
    }

    void ChopSurface::PadGrid::paint (juce::Graphics& g)
    {
        const auto plan = session.plan();
        for (int i = 0; i < ep40::padsPerGroup; ++i)
        {
            const auto r = cell (i).reduced (4.0f);
            const chop::PadWrite* write = nullptr;
            for (const auto& w : plan.writes)
                if (w.gridIndex == i)
                    write = &w;

            g.setColour (c.ink.withAlpha (0.25f));
            g.fillRoundedRectangle (r.translated (2.0f, 3.0f), 3.0f);
            g.setColour (write != nullptr ? c.paperHigh : c.paper.darker (0.04f));
            g.fillRoundedRectangle (r, 3.0f);
            if (i == plan.startGridIndex)
            {
                g.setColour (c.orange);
                g.drawRoundedRectangle (r, 3.0f, 2.0f);
            }

            g.setFont (HaloType::label (10.0f));
            g.setColour (c.inkSoft);
            g.drawText (juce::String (ep40::gridLabels[(size_t) i].data()), r.reduced (5.0f, 3.0f), juce::Justification::topLeft);
            if (write != nullptr)
            {
                g.setFont (HaloType::label (15.0f));
                g.setColour (session.selectedSlice == write->sliceIndex ? c.orange : c.riddimGreen);
                g.drawText (juce::String (write->sliceIndex + 1), r, juce::Justification::centred);
            }
        }
    }

    void ChopSurface::PadGrid::mouseUp (const juce::MouseEvent& e)
    {
        for (int i = 0; i < ep40::padsPerGroup; ++i)
            if (cell (i).contains (e.position))
            {
                session.setStartPad (i);
                session.changed();
            }
    }

    //==============================================================================
    ChopSurface::ChopSurface (AppModel& m)
        : model (m), session (m.editor()), waveform (m.editor(), WaveformView::Style::chop), pads (m.editor())
    {
        setWantsKeyboardFocus (true);
        addAndMakeVisible (waveform);
        addAndMakeVisible (pads);

        sensitivity.setSliderStyle (juce::Slider::LinearHorizontal);
        sensitivity.setTextBoxStyle (juce::Slider::NoTextBox, false, 0, 0);
        sensitivity.setRange (0.0, 1.0);
        sensitivity.setValue (session.onsetParameters.sensitivity, juce::dontSendNotification);
        sensitivity.onDragEnd = [this]
        {
            session.onsetParameters.sensitivity = sensitivity.getValue();
            session.detect();
        };
        addAndMakeVisible (sensitivity);

        detectButton.onClick = [this] { session.detect(); };
        clearButton.onClick = [this] { session.slices.clear(); session.selectedSlice.reset(); session.changed(); };
        closeButton.onClick = [this] { session.closeChop(); };
        exportButton.onClick = [this] { session.exportSlices(); };
        showButton.onClick = [this]
        {
            const auto f = session.lastExport();
            (f.exists() ? f : SampleIO::exportFolder()).revealToUser();
        };
        sendButton.onClick = [this]
        {
            if (! model.writesUnlocked() || session.slices.sliceCount() == 0)
                return;
            const auto plan = session.plan();
            const int project = model.device().activeProject();
            juce::AlertWindow::showOkCancelBox (juce::MessageBoxIconType::QuestionIcon, "Send to pads",
                "Upload " + juce::String ((int) plan.writes.size()) + " slices (" + juce::String (formatBytes (plan.totalBytes))
                    + ") into empty USER slots, and assign them to group " + ep40::groupLabels[(size_t) plan.group].data()
                    + " pads in project " + juce::String (project) + "?" + juce::newLine + juce::newLine
                    + (plan.trimmedCount > 0 ? juce::String (plan.trimmedCount) + " slice(s) are auto-trimmed to "
                                                   + juce::String (session.sliceCapSeconds(), 1) + " s." + juce::newLine
                                             : juce::String())
                    + "Takes " + Ep40Writer::expectedTime (plan.totalBytes) + " (the EP-40 takes about 40 KB/s; MONO or a LO quality is quicker)." + juce::newLine
                    + "No sound is overwritten. The pads' old sounds stay in the library.",
                "Send", "Cancel", this,
                juce::ModalCallbackFunction::create ([this, plan, project] (int result)
                {
                    if (result == 0)
                        return;
                    session.report ("Preparing slices...");
                    session.renderPlacedSlices ([this, plan, project] (std::shared_ptr<std::vector<AudioData>> audio)
                    {
                        std::vector<Ep40Writer::PadJob> jobs;
                        for (size_t i = 0; i < plan.writes.size() && i < audio->size(); ++i)
                            jobs.push_back ({ std::move ((*audio)[i]), juce::String (plan.writes[i].name), plan.writes[i].group, plan.writes[i].gridIndex });
                        model.deviceWriter().uploadToPads (std::move (jobs), project,
                            [this] (const juce::String& progress) { session.report (progress); },
                            [this] (Ep40Writer::Result r) { session.report (r.ok ? r.message + ". Play them on the EP-40!" : "Send failed: " + r.message); });
                    });
                }));
        };

        const char* sliceNames[] { "FIT", "4", "8", "16", "ALL" };
        for (size_t i = 0; i < sliceButtons.size(); ++i)
        {
            sliceButtons[i].setButtonText (sliceNames[i]);
            sliceButtons[i].onClick = [this, i] { session.setSliceChoice (sliceChoices[i]); };
            addAndMakeVisible (sliceButtons[i]);
        }
        evenButton.onClick = [this] { session.setEvenSlices (! session.evenSlices); };
        addAndMakeVisible (evenButton);
        autoTrimButton.setTooltip ("Cut slices that are too long for the EP-40 (over 20 s, or more than its free memory) short, with a fade-out.");
        autoTrimButton.onClick = [this] { session.setAutoTrim (! session.autoTrim); };
        addAndMakeVisible (autoTrimButton);

        for (size_t i = 0; i < groupButtons.size(); ++i)
        {
            groupButtons[i].setButtonText (juce::String (ep40::groupLabels[i].data()));
            groupButtons[i].onClick = [this, i] { session.group = (int) i; session.changed(); };
            addAndMakeVisible (groupButtons[i]);
        }
        for (size_t i = 0; i < qualityButtons.size(); ++i)
        {
            qualityButtons[i].setButtonText (juce::String (label (treatments[i])).replace ("LO-FI ", "LO "));
            qualityButtons[i].onClick = [this, i] { session.treatment = treatments[i]; session.changed(); };
            addAndMakeVisible (qualityButtons[i]);
        }

        nameEditor.setFont (HaloType::mono (14.0f));
        nameEditor.setInputRestrictions (8, "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789");
        nameEditor.setColour (juce::TextEditor::backgroundColourId, c.paperHigh);
        nameEditor.setColour (juce::TextEditor::textColourId, c.ink);
        nameEditor.setColour (juce::TextEditor::outlineColourId, c.ink.withAlpha (0.25f));
        nameEditor.setColour (juce::TextEditor::focusedOutlineColourId, c.orange);
        nameEditor.onTextChange = [this] { session.baseName = nameEditor.getText().toUpperCase(); session.changed(); };
        addAndMakeVisible (nameEditor);

        for (auto* b : { &detectButton, &clearButton, &closeButton, &exportButton, &sendButton, &showButton })
            addAndMakeVisible (b);

        session.addChangeListener (this);
        model.device().addChangeListener (this);
        model.backup().addChangeListener (this);
        refresh();
    }

    ChopSurface::~ChopSurface()
    {
        session.removeChangeListener (this);
        model.device().removeChangeListener (this);
        model.backup().removeChangeListener (this);
    }

    juce::String ChopSurface::sendBlocker() const
    {
        // What would stop the EP-40 taking these slices, said before sending.
        // With AUTO-TRIM on, the plan is already capped, so these only fire when
        // it's off (or the EP-40 is too full for even short slices).
        const auto plan = session.plan();
        if (plan.tooLongCount > 0)
            return juce::String (plan.tooLongCount) + (plan.tooLongCount == 1 ? " slice is" : " slices are") + " over "
                   + juce::String ((int) maxSoundSeconds) + " s (longest " + juce::String (plan.longestSeconds, 1)
                   + " s), the EP-40's limit per sound. Turn on AUTO-TRIM, use more slices, or CLOSE and trim in EDIT.";
        const auto& info = model.device().info();
        if (model.device().state() == Ep40Library::State::ready && info.freeBytes >= 0 && plan.totalBytes > info.freeBytes)
            return "These slices need " + juce::String (formatBytes (plan.totalBytes)) + " but the EP-40 has "
                   + juce::String (formatBytes (info.freeBytes)) + " free. Turn on AUTO-TRIM, use fewer slices, or pick a LO quality.";
        if (session.autoTrim && plan.trimmedCount > 0 && session.sliceCapSeconds() < 0.25)
            return "The EP-40 is nearly full: AUTO-TRIM would cut the slices to " + juce::String (session.sliceCapSeconds() * 1000.0, 0)
                   + " ms. Delete sounds in LOAD, use fewer slices, or pick a LO quality.";
        return {};
    }

    juce::String ChopSurface::trimNote() const
    {
        const auto plan = session.plan();
        if (! session.autoTrim || plan.trimmedCount == 0)
            return {};
        return "AUTO-TRIM: " + juce::String (plan.trimmedCount) + (plan.trimmedCount == 1 ? " slice" : " slices")
               + " cut to " + juce::String (session.sliceCapSeconds(), 1) + " s"
               + (session.capLimitedBySpace() ? " to fit the EP-40's free memory" : " (the EP-40's limit)")
               + ", from each slice's start, with a fade-out. The shaded tails are dropped.";
    }

    void ChopSurface::refresh()
    {
        // AUTO-TRIM sizes the slices to what the EP-40 has free, when it's known.
        const auto& info = model.device().info();
        session.spaceBudgetBytes = model.device().state() == Ep40Library::State::ready && info.freeBytes >= 0 ? info.freeBytes : -1;
        autoTrimButton.setToggleState (session.autoTrim, juce::dontSendNotification);

        for (size_t i = 0; i < sliceButtons.size(); ++i)
            sliceButtons[i].setToggleState (session.sliceChoice == sliceChoices[i], juce::dontSendNotification);
        sliceButtons[0].setButtonText ("FIT " + juce::String (juce::jmax (1, ep40::padsPerGroup - session.startPad)));
        evenButton.setToggleState (session.evenSlices, juce::dontSendNotification);
        sensitivity.setEnabled (! session.evenSlices);
        detectButton.setEnabled (! session.evenSlices);
        for (size_t i = 0; i < groupButtons.size(); ++i)
            groupButtons[i].setToggleState (session.group == (int) i, juce::dontSendNotification);
        for (size_t i = 0; i < qualityButtons.size(); ++i)
            qualityButtons[i].setToggleState (session.treatment == treatments[i], juce::dontSendNotification);
        if (! nameEditor.hasKeyboardFocus (false) && nameEditor.getText() != session.baseName)
            nameEditor.setText (session.baseName, false);
        showButton.setVisible (session.lastExport().exists());
        exportButton.setEnabled (session.slices.sliceCount() > 0);
        sendButton.setEnabled (session.slices.sliceCount() > 0 && model.writesUnlocked() && ! model.deviceWriter().isBusy()
                               && sendBlocker().isEmpty());
        pads.repaint();
        repaint();
    }

    bool ChopSurface::keyPressed (const juce::KeyPress& key)
    {
        // 1-9 then 0 audition slices 1-10; Delete removes the selected slice's leading cut.
        const auto code = key.getKeyCode();
        if (! key.getModifiers().isAnyModifierKeyDown() && code >= '0' && code <= '9')
        {
            session.auditionSlice (code == '0' ? 9 : code - '1');
            return true;
        }
        if ((code == juce::KeyPress::deleteKey || code == juce::KeyPress::backspaceKey) && session.selectedSlice && *session.selectedSlice > 0)
        {
            session.slices.removeCut (*session.selectedSlice - 1);
            session.selectedSlice = *session.selectedSlice - 1;
            session.changed();
            return true;
        }
        if (code == juce::KeyPress::escapeKey)
        {
            session.closeChop();
            return true;
        }
        return false;
    }

    void ChopSurface::resized()
    {
        auto area = getLocalBounds().reduced (24, 18);
        headerArea = area.removeFromTop (40);
        closeButton.setBounds (headerArea.withLeft (headerArea.getRight() - 110).withSizeKeepingCentre (110, 34));

        auto controls = area.removeFromTop (36);
        sensitivityLabel = controls.removeFromLeft (100);
        sensitivity.setBounds (controls.removeFromLeft (220));
        controls.removeFromLeft (12);
        detectButton.setBounds (controls.removeFromLeft (100).reduced (2));
        clearButton.setBounds (controls.removeFromLeft (130).reduced (2));

        area.removeFromTop (4);
        auto sliceRow = area.removeFromTop (34);
        slicesLabel = sliceRow.removeFromLeft (100);
        for (auto& b : sliceButtons)
            b.setBounds (sliceRow.removeFromLeft (&b == &sliceButtons[0] ? 84 : 60).reduced (2));
        sliceRow.removeFromLeft (16);
        evenButton.setBounds (sliceRow.removeFromLeft (80).reduced (2));
        sliceRow.removeFromLeft (8);
        autoTrimButton.setBounds (sliceRow.removeFromLeft (120).reduced (2));
        hintArea = area.removeFromTop (22);
        area.removeFromTop (6);

        waveform.setBounds (area.removeFromTop (juce::jmax (140, area.getHeight() - 300)));
        area.removeFromTop (14);

        auto bottom = area;
        auto left = bottom.removeFromLeft (juce::jmin (440, bottom.getWidth() / 2));
        auto groups = left.removeFromTop (34);
        for (auto& b : groupButtons)
            b.setBounds (groups.removeFromLeft (groups.getWidth() / 4 + 1).reduced (2));
        left.removeFromTop (8);
        pads.setBounds (left.removeFromLeft (juce::jmin (240, left.getWidth() / 2)).withHeight (juce::jmin (left.getHeight(), 230)));
        planArea = left.reduced (12, 0);

        bottom.removeFromLeft (24);
        qualityLabel = bottom.removeFromTop (18);
        auto quality = bottom.removeFromTop (34);
        const int each = quality.getWidth() / 5;
        for (auto& b : qualityButtons)
            b.setBounds (quality.removeFromLeft (each).reduced (2));
        bottom.removeFromTop (10);
        auto nameRow = bottom.removeFromTop (30);
        nameLabel = nameRow.removeFromLeft (70);
        nameEditor.setBounds (nameRow.removeFromLeft (140));
        bottom.removeFromTop (8);
        totalsArea = bottom.removeFromTop (40);
        auto actions = bottom.removeFromTop (38);
        exportButton.setBounds (actions.removeFromLeft (160).reduced (2));
        sendButton.setBounds (actions.removeFromLeft (150).reduced (2));
        showButton.setBounds (actions.removeFromLeft (80).reduced (2));
        noteArea = bottom.removeFromTop (46);
        statusArea = bottom.removeFromTop (24);
    }

    void ChopSurface::paint (juce::Graphics& g)
    {
        g.fillAll (c.canvas);
        HaloLookAndFeel::drawPanel (g, getLocalBounds().toFloat().reduced (8.0f), c.paper);

        auto header = headerArea;
        g.setFont (HaloType::label (11.0f));
        g.setColour (c.orange);
        g.drawText ("CHOP", header.removeFromTop (16), juce::Justification::centredLeft);
        g.setFont (HaloType::body (17.0f));
        g.setColour (c.ink);
        g.drawText (session.item().title, header.withTrimmedRight (130), juce::Justification::centredLeft, true);

        g.setFont (HaloType::label (11.0f, false));
        g.setColour (c.inkSoft);
        g.drawText ("SENSITIVITY", sensitivityLabel, juce::Justification::centredLeft);
        g.drawText ("SLICES", slicesLabel, juce::Justification::centredLeft);
        g.setFont (HaloType::mono (11.0f));
        g.drawText ("click a slice or press 1-9, 0 to hear it  |  double-click: add cut  |  drag a marker: move  |  right-click a marker or Delete: remove",
                    hintArea, juce::Justification::centredLeft, true);

        const auto plan = session.plan();
        const int count = session.slices.sliceCount();

        // Plan summary beside the pad grid.
        auto p = planArea;
        g.setFont (HaloType::label (11.0f));
        g.setColour (c.inkSoft);
        g.drawText ("PADS", p.removeFromTop (18), juce::Justification::centredLeft);
        g.setFont (HaloType::mono (12.0f));
        g.setColour (c.ink);
        g.drawText (juce::String (count) + (count == 1 ? " slice" : " slices"), p.removeFromTop (20), juce::Justification::centredLeft);
        g.drawText ("from pad " + juce::String (ep40::gridLabels[(size_t) plan.startGridIndex].data())
                        + " of group " + ep40::groupLabels[(size_t) plan.group].data(),
                    p.removeFromTop (20), juce::Justification::centredLeft);
        if (plan.overflowCount > 0)
        {
            g.setColour (c.warning);
            g.drawFittedText (juce::String (plan.overflowCount) + " slice(s) don't fit on this group's pads. They're still exported.",
                              p.removeFromTop (40), juce::Justification::topLeft, 3);
        }
        g.setColour (c.inkSoft);
        g.setFont (HaloType::mono (11.0f));
        g.drawFittedText ("Click a pad to start there.", p.removeFromTop (30), juce::Justification::topLeft, 2);

        g.setFont (HaloType::label (11.0f, false));
        g.setColour (c.inkSoft);
        g.drawText ("EP-40 QUALITY", qualityLabel, juce::Justification::centredLeft);
        g.drawText ("NAME", nameLabel, juce::Justification::centredLeft);

        g.setFont (HaloType::mono (13.0f));
        g.setColour (c.ink);
        g.drawText (juce::String (formatBytes (plan.totalBytes)) + " on " + juce::String ((int) plan.writes.size()) + " pads, "
                        + Ep40Writer::expectedTime (plan.totalBytes) + " to send",
                    totalsArea.withHeight (20), juce::Justification::centredLeft);
        g.setFont (HaloType::mono (11.0f));
        g.setColour (c.inkSoft);
        g.drawText (juce::String (plan.totalBytes / deviceMemoryBytes * 100.0, 2) + "% of ~122 MB, 16-bit at "
                        + juce::String (resolvedSampleRate (session.treatment, session.audio() != nullptr ? session.audio()->sampleRate : 0.0) / 1000.0, 3) + " kHz",
                    totalsArea.withTrimmedTop (20), juce::Justification::centredLeft);

        const auto blocker = sendBlocker();
        const auto trimmed = trimNote();
        const auto sendNote = blocker.isNotEmpty() ? blocker
            : trimmed.isNotEmpty() ? trimmed
            : model.writesUnlocked()
            ? "SEND TO PADS uploads the slices into empty USER slots and assigns them to these pads in project "
                  + juce::String (model.device().activeProject()) + " (the project selected in LOAD)."
            : model.device().state() == Ep40Library::State::ready ? juce::String ("Back up the EP-40 in BACKUPS (Ctrl+6) to unlock SEND TO PADS. EXPORT works now.")
                                                                   : juce::String ("Connect the EP-40 to send slices to its pads. EXPORT works now.");
        if (blocker.isNotEmpty())
            g.setColour (c.warning);
        else if (trimmed.isNotEmpty())
            g.setColour (c.orange);
        g.drawFittedText (sendNote,
                          noteArea, juce::Justification::centredLeft, 3, 1.0f);
        g.setFont (HaloType::mono (11.5f));
        g.setColour (session.status().startsWith ("Export failed") ? c.warning : c.ink);
        g.drawText (session.status(), statusArea, juce::Justification::centredLeft, true);
    }
}
