#include "UI/EditPanel.h"
#include "Audio/SampleIO.h"
#include "UI/HaloColours.h"

namespace halo
{
    namespace
    {
        const auto& c = legacyColours();
        constexpr Treatment treatments[] { Treatment::original, Treatment::high, Treatment::balanced,
                                           Treatment::loFi22k, Treatment::loFi11k };
        constexpr SamplePrep::Channels channelModes[] { SamplePrep::Channels::preserve, SamplePrep::Channels::mono,
                                                        SamplePrep::Channels::stereo };
        constexpr int knobSize = 54;

        juce::String seconds (double s) { return juce::String (s, 3) + " s"; }
    }

    juce::Slider& EditPanel::addKnob (juce::Slider& s, double min, double max, double mid, std::function<void()> onChange)
    {
        s.setSliderStyle (juce::Slider::RotaryHorizontalVerticalDrag);
        s.setTextBoxStyle (juce::Slider::NoTextBox, false, 0, 0);
        s.setRange (min, max);
        s.setSkewFactorFromMidPoint (mid);
        s.setMouseDragSensitivity (180);
        s.onValueChange = [this, onChange]
        {
            onChange();
            session.changed();
        };
        addAndMakeVisible (s);
        return s;
    }

    EditPanel::EditPanel (AppModel& m)
        : model (m), session (m.editor()), waveform (m.editor(), WaveformView::Style::edit)
    {
        addAndMakeVisible (waveform);

        auto msToFrames = [this] (double ms)
        {
            const auto a = session.audio();
            return a != nullptr ? (int) (ms / 1000.0 * a->sampleRate) : 0;
        };
        addKnob (fadeIn, 0.0, 2000.0, 200.0, [this, msToFrames] { session.prep.fadeInFrames = msToFrames (fadeIn.getValue()); });
        addKnob (fadeOut, 0.0, 2000.0, 200.0, [this, msToFrames] { session.prep.fadeOutFrames = msToFrames (fadeOut.getValue()); });
        addKnob (gain, -24.0, 12.0, 0.0, [this] { session.prep.gainDecibels = (float) gain.getValue(); });
        fadeIn.setDoubleClickReturnValue (true, 0.0);
        fadeOut.setDoubleClickReturnValue (true, 0.0);
        gain.setDoubleClickReturnValue (true, 0.0);

        normalizeButton.onClick = [this] { session.prep.normalize = ! session.prep.normalize; session.changed(); };
        resetTrimButton.onClick = [this]
        {
            session.prep.trimStartFrame = 0;
            session.prep.trimEndFrame.reset();
            session.changed();
        };

        const char* channelNames[] { "KEEP", "MONO", "STEREO" };
        for (size_t i = 0; i < channelButtons.size(); ++i)
        {
            channelButtons[i].setButtonText (channelNames[i]);
            channelButtons[i].onClick = [this, i] { session.prep.channelMode = channelModes[i]; session.changed(); };
            addAndMakeVisible (channelButtons[i]);
        }
        for (size_t i = 0; i < qualityButtons.size(); ++i)
        {
            qualityButtons[i].setButtonText (juce::String (label (treatments[i])).replace ("LO-FI ", "LO "));
            qualityButtons[i].onClick = [this, i] { session.treatment = treatments[i]; session.changed(); };
            addAndMakeVisible (qualityButtons[i]);
        }

        auditionButton.onClick = [this] { session.audition(); };
        exportButton.onClick = [this] { session.exportEdit(); };
        chopButton.onClick = [this] { session.openChop(); };
        showButton.onClick = [this]
        {
            const auto f = session.lastExport();
            (f.exists() ? f : SampleIO::exportFolder()).revealToUser();
        };
        libraryButton.onClick = [this] { model.setMode (AppMode::capture); };
        sendButton.onClick = [this]
        {
            if (! model.writesUnlocked() || ! session.hasSource())
                return;
            const auto e = session.sendEstimate();
            const int slot = model.device().firstFreeSlot (Ep40Writer::userFirstSlot, Ep40Writer::userLastSlot);
            juce::AlertWindow::showOkCancelBox (juce::MessageBoxIconType::QuestionIcon, "Send to EP-40",
                "Upload \"" + session.item().title + "\" (" + juce::String (formatBytes (e.payloadBytes))
                    + ") to empty slot " + juce::String (slot) + " in USER?" + juce::newLine + juce::newLine
                    + (session.sendIsTrimmed() ? "It's auto-trimmed to the first " + juce::String ((int) maxSoundSeconds) + " s of your selection." + juce::newLine
                                               : juce::String())
                    + "Takes " + Ep40Writer::expectedTime (e.payloadBytes) + ". Nothing on the EP-40 is overwritten.",
                "Send", "Cancel", this,
                juce::ModalCallbackFunction::create ([this] (int result)
                {
                    if (result == 0)
                        return;
                    session.report ("Sending to the EP-40...");
                    session.renderSend ([this] (std::shared_ptr<const AudioData> audio)
                    {
                        auto lastMs = std::make_shared<double> (0.0);
                        model.deviceWriter().upload (*audio, session.item().title, 0, [this] (Ep40Writer::Result r)
                        {
                            session.report (r.ok ? r.message + ". Assign it to a pad in LOAD." : "Send failed: " + r.message);
                        },
                        [this, lastMs] (juce::int64 sent, juce::int64 total)
                        {
                            const double now = juce::Time::getMillisecondCounterHiRes();
                            if (now - *lastMs < 250.0)
                                return;
                            *lastMs = now;
                            session.report ("Sending to the EP-40: " + juce::String ((int) (100 * sent / juce::jmax<juce::int64> (1, total))) + "%, "
                                            + Ep40Writer::expectedTime (total - sent) + " left");
                        });
                    });
                }));
        };

        for (auto* b : { &normalizeButton, &resetTrimButton, &auditionButton, &exportButton, &chopButton,
                         &sendButton, &showButton, &libraryButton })
            addAndMakeVisible (b);

        session.addChangeListener (this);
        model.device().addChangeListener (this);
        model.backup().addChangeListener (this);
        refresh();
    }

    EditPanel::~EditPanel()
    {
        session.removeChangeListener (this);
        model.device().removeChangeListener (this);
        model.backup().removeChangeListener (this);
    }

    juce::String EditPanel::sendBlocker() const
    {
        if (! session.hasSource())
            return {};
        // Over 20 s isn't a blocker: SEND auto-trims (see sendPrep()).
        const auto e = session.sendEstimate();
        const auto& info = model.device().info();
        if (model.device().state() == Ep40Library::State::ready && info.freeBytes >= 0 && e.payloadBytes > info.freeBytes)
            return "Needs " + juce::String (formatBytes (e.payloadBytes)) + " but the EP-40 has " + juce::String (formatBytes (info.freeBytes)) + " free.";
        return {};
    }

    void EditPanel::refresh()
    {
        const auto audio = session.audio();
        const bool has = audio != nullptr;
        for (auto* comp : std::initializer_list<juce::Component*> { &fadeIn, &fadeOut, &gain, &normalizeButton, &resetTrimButton,
                                                                    &auditionButton, &exportButton, &chopButton })
            comp->setEnabled (has);
        for (auto& b : channelButtons) b.setEnabled (has);
        for (auto& b : qualityButtons) b.setEnabled (has);
        libraryButton.setVisible (! has && ! session.isLoading());

        if (has)
        {
            const double rate = audio->sampleRate;
            fadeIn.setValue (session.prep.fadeInFrames / rate * 1000.0, juce::dontSendNotification);
            fadeOut.setValue (session.prep.fadeOutFrames / rate * 1000.0, juce::dontSendNotification);
            gain.setValue (session.prep.gainDecibels, juce::dontSendNotification);
        }
        normalizeButton.setToggleState (session.prep.normalize, juce::dontSendNotification);
        for (size_t i = 0; i < channelButtons.size(); ++i)
            channelButtons[i].setToggleState (session.prep.channelMode == channelModes[i], juce::dontSendNotification);
        for (size_t i = 0; i < qualityButtons.size(); ++i)
            qualityButtons[i].setToggleState (session.treatment == treatments[i], juce::dontSendNotification);

        const bool auditioning = session.player().isPlaying() && session.player().token() == -1;
        auditionButton.setButtonText (auditioning ? "STOP" : "AUDITION");
        auditionButton.setToggleState (auditioning, juce::dontSendNotification);
        showButton.setVisible (session.lastExport().exists());
        sendButton.setEnabled (has && model.writesUnlocked() && ! model.deviceWriter().isBusy() && sendBlocker().isEmpty());
        repaint();
    }

    void EditPanel::resized()
    {
        auto area = getLocalBounds().reduced (16, 12);
        headerArea = area.removeFromTop (40);
        libraryButton.setBounds (headerArea.withLeft (headerArea.getRight() - 130).withHeight (32));
        area.removeFromTop (4);
        waveform.setBounds (area.removeFromTop (150));
        area.removeFromTop (4);

        trimArea = area.removeFromTop (28);
        resetTrimButton.setBounds (trimArea.removeFromRight (110).reduced (0, 1));
        area.removeFromTop (8);

        auto knobs = area.removeFromTop (knobSize);
        for (auto* k : { &fadeIn, &fadeOut, &gain })
            k->setBounds (knobs.removeFromLeft (84).withSizeKeepingCentre (knobSize, knobSize));
        normalizeButton.setBounds (knobs.removeFromRight (130).withSizeKeepingCentre (130, 34));
        knobLabelArea = area.removeFromTop (30);
        area.removeFromTop (6);

        auto channelsRow = area.removeFromTop (32);
        channelLabel = channelsRow.removeFromLeft (84);
        for (auto& b : channelButtons)
            b.setBounds (channelsRow.removeFromLeft (90).reduced (2));
        area.removeFromTop (6);

        qualityLabel = area.removeFromTop (18);
        auto qualityRow = area.removeFromTop (34);
        const int each = qualityRow.getWidth() / 5;
        for (auto& b : qualityButtons)
            b.setBounds (qualityRow.removeFromLeft (each).reduced (2));
        area.removeFromTop (6);
        sizeArea = area.removeFromTop (40);
        area.removeFromTop (6);

        auto actions = area.removeFromTop (38);
        auditionButton.setBounds (actions.removeFromLeft (110).reduced (2));
        exportButton.setBounds (actions.removeFromLeft (100).reduced (2));
        chopButton.setBounds (actions.removeFromLeft (90).reduced (2));
        sendButton.setBounds (actions.removeFromRight (150).reduced (2));
        sendNoteArea = area.removeFromTop (34);
        auto status = area.removeFromTop (28);
        showButton.setBounds (status.removeFromRight (70).reduced (0, 2));
        statusArea = status;
    }

    void EditPanel::paint (juce::Graphics& g)
    {
        const auto audio = session.audio();

        g.setFont (HaloType::label (11.0f));
        g.setColour (c.inkSoft);
        auto header = headerArea;
        g.drawText ("EDIT", header.removeFromTop (16), juce::Justification::centredLeft);
        g.setFont (HaloType::body (16.0f));
        g.setColour (c.ink);
        g.drawText (session.hasSource() || session.isLoading() ? session.item().title
                                                               : juce::String ("Nothing open: pick a take, grab or file"),
                    header.withTrimmedRight (140), juce::Justification::centredLeft, true);

        g.setFont (HaloType::mono (11.5f));
        if (audio != nullptr)
        {
            const auto t = session.prep.resolvedTrim (audio->frameCount());
            g.setColour (c.ink);
            g.drawText ("TRIM  " + seconds (t.start / audio->sampleRate) + " - " + seconds (t.end / audio->sampleRate)
                            + "   (" + seconds ((t.end - t.start) / audio->sampleRate) + ")",
                        trimArea, juce::Justification::centredLeft, true);
        }

        // Knob captions.
        g.setFont (HaloType::label (10.0f));
        auto captions = knobLabelArea;
        const juce::String names[] { "FADE IN", "FADE OUT", "GAIN" };
        const juce::String values[] { juce::String (juce::roundToInt (fadeIn.getValue())) + " ms",
                                      juce::String (juce::roundToInt (fadeOut.getValue())) + " ms",
                                      (gain.getValue() > 0 ? "+" : "") + juce::String (gain.getValue(), 1) + " dB" };
        for (int i = 0; i < 3; ++i)
        {
            auto cell = captions.removeFromLeft (84);
            g.setColour (c.inkSoft);
            g.drawText (names[i], cell.removeFromTop (14), juce::Justification::centred);
            g.setColour (c.ink);
            g.drawText (values[i], cell, juce::Justification::centred);
        }

        g.setColour (c.inkSoft.withAlpha (0.85f));
        g.drawText ("CHANNELS", channelLabel, juce::Justification::centredLeft);
        g.drawText ("EP-40 QUALITY", qualityLabel, juce::Justification::centredLeft);

        if (audio != nullptr)
        {
            const auto e = session.estimate();
            const double share = (double) e.payloadBytes / deviceMemoryBytes * 100.0;
            auto size = sizeArea;
            g.setFont (HaloType::mono (13.0f));
            g.setColour (c.ink);
            g.drawText (juce::String (formatBytes (e.payloadBytes)) + "   " + juce::String (e.sampleRate / 1000.0, 3)
                            + " kHz  16-bit  " + (e.channels == 1 ? "mono" : "stereo") + "   " + seconds (e.durationSeconds()),
                        size.removeFromTop (20), juce::Justification::centredLeft, true);
            g.setFont (HaloType::mono (11.0f));
            g.setColour (c.inkSoft);
            g.drawText (juce::String (share, share < 1.0 ? 2 : 1) + "% of the EP-40's ~122 MB sample memory (exact bytes, before the device's file overhead)",
                        size, juce::Justification::centredLeft, true);
        }

        g.setFont (HaloType::mono (11.0f));
        g.setColour (c.inkSoft);
        const auto blocker = sendBlocker();
        const auto trimNote = session.sendIsTrimmed()
            ? "AUTO-TRIM: over " + juce::String ((int) maxSoundSeconds) + " s, so SEND sends the first " + juce::String ((int) maxSoundSeconds)
                  + " s of the selection, with a fade-out. Drag the trim handles to pick another part, or CHOP it."
            : juce::String();
        const auto sendNote = blocker.isNotEmpty() ? blocker
                            : trimNote.isNotEmpty() ? trimNote
                            : model.writesUnlocked() ? juce::String ("SEND TO EP-40 uploads into the first empty USER slot (900-999). It never overwrites a sound.")
                            : model.device().state() == Ep40Library::State::ready ? juce::String ("Back up the EP-40 in BACKUPS (Ctrl+6) to unlock SEND. Until then, EXPORT works.")
                            : juce::String ("Connect the EP-40 to send directly. Until then, EXPORT works.");
        if (blocker.isNotEmpty())
            g.setColour (c.warning);
        else if (trimNote.isNotEmpty())
            g.setColour (c.orange);
        g.drawFittedText (sendNote,
                          sendNoteArea, juce::Justification::centredLeft, 2, 1.0f);

        g.setFont (HaloType::mono (11.5f));
        g.setColour (session.status().startsWith ("Export failed") ? c.warning : c.ink);
        g.drawText (session.status(), statusArea, juce::Justification::centredLeft, true);
    }
}
