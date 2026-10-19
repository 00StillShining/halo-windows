#include "UI/MonitorPanel.h"
#include "Core/GrabMath.h"
#include "UI/HaloColours.h"

namespace halo
{
    namespace
    {
        const auto& c = HaloColours::graphPaper;

        juce::String formatTime (double seconds)
        {
            const int s = (int) seconds;
            return juce::String (s / 60).paddedLeft ('0', 2) + ":" + juce::String (s % 60).paddedLeft ('0', 2);
        }
    }

    MonitorPanel::MonitorPanel (AppModel& m) : model (m)
    {
        monitorButton.onClick = [this] { model.toggleMonitor(); };
        recordButton.onClick = [this] { model.toggleRecording(); };
        showButton.onClick = [this]
        {
            if (const auto& take = model.lastTake())
                take->file.revealToUser();
        };

        outputBox.onChange = [this]
        {
            const int index = outputBox.getSelectedItemIndex();
            model.setMonitorOutput (index <= 0 ? juce::String() : outputs[index - 1]);
        };

        grabButton.onClick = [this] { model.grabNow(); };
        for (size_t i = 0; i < lengthButtons.size(); ++i)
        {
            lengthButtons[i].onClick = [this, i]
            {
                model.setGrabLength (model.grabUsesBars() ? grab::barChoices[i] : grab::secondsChoices[i]);
            };
            addAndMakeVisible (lengthButtons[i]);
        }

        level.setSliderStyle (juce::Slider::LinearHorizontal);
        level.setTextBoxStyle (juce::Slider::NoTextBox, false, 0, 0); // value drawn in paint()
        level.setRange (MonitorGain::silenceFloorDb, 0.0, 0.5);
        level.setDoubleClickReturnValue (true, MonitorGain::defaultDb);
        level.setValue (model.monitor().gainDb(), juce::dontSendNotification);
        level.onValueChange = [this]
        {
            model.setMonitorGainDb (level.getValue());
            repaint();
        };

        for (auto* comp : std::initializer_list<juce::Component*> { &monitorButton, &recordButton, &showButton, &outputBox, &level, &grabButton })
            addAndMakeVisible (comp);

        model.addListener (this);
        rebuildOutputs();
        refresh();
        startTimerHz (30);
    }

    MonitorPanel::~MonitorPanel()
    {
        model.removeListener (this);
    }

    void MonitorPanel::rebuildOutputs()
    {
        auto& engine = model.monitor();
        outputs.clear();
        for (const auto& name : engine.outputNames())
            if (! MonitorEngine::isEp40Name (name)) // never offer the feedback route
                outputs.add (name);

        outputBox.clear (juce::dontSendNotification);
        const auto fallback = engine.defaultOutputName();
        outputBox.addItem ("Windows default" + (fallback.isNotEmpty() ? " (" + fallback + ")" : juce::String()), 1);
        for (int i = 0; i < outputs.size(); ++i)
            outputBox.addItem (outputs[i], i + 2);

        const int chosen = outputs.indexOf (engine.chosenOutputName());
        outputBox.setSelectedItemIndex (chosen >= 0 ? chosen + 1 : 0, juce::dontSendNotification);
    }

    void MonitorPanel::refresh()
    {
        auto& engine = model.monitor();
        const bool running = engine.isRunning();
        monitorButton.setButtonText (running ? "MONITOR ON" : "MONITOR");
        monitorButton.setToggleState (running, juce::dontSendNotification);
        monitorButton.setEnabled (engine.ep40InputName().isNotEmpty() || running);

        recordButton.setButtonText (engine.isRecording() ? "STOP REC" : "REC");
        recordButton.setToggleState (engine.isRecording(), juce::dontSendNotification);
        recordButton.setEnabled (running);
        showButton.setVisible (model.lastTake().has_value());

        // GRAB lengths: bars when a clock is running with the transport playing.
        const bool bars = model.grabUsesBars();
        const auto& grabber = model.grabber();
        for (size_t i = 0; i < lengthButtons.size(); ++i)
        {
            const int value = bars ? grab::barChoices[i] : grab::secondsChoices[i];
            lengthButtons[i].setButtonText (juce::String (value) + (bars ? "" : "s"));
            lengthButtons[i].setToggleState (value == (bars ? grabber.bars : grabber.seconds), juce::dontSendNotification);
        }
        grabButton.setEnabled (running && ! grabber.isBusy());

        if (! level.isMouseButtonDown())
            level.setValue (engine.gainDb(), juce::dontSendNotification);

        if (outputBox.getNumItems() != engine.outputNames().size() + 1)
            rebuildOutputs();
        repaint();
    }

    void MonitorPanel::timerCallback()
    {
        if (! model.monitor().isRunning())
            return;
        levels = model.monitor().levels();
        if (levels.clippedL || levels.clippedR)
        {
            clipL = clipL || levels.clippedL;
            clipR = clipR || levels.clippedR;
            clipHold = 45; // ~1.5 s
        }
        else if (clipHold > 0 && --clipHold == 0)
        {
            clipL = clipR = false;
        }
        repaint (meterArea.getSmallestIntegerContainer().expanded (2));
        repaint (infoArea);

        // The clock/transport can switch GRAB between bars and seconds.
        const bool bars = model.grabUsesBars();
        if (bars != (lengthButtons[0].getButtonText() == "4"))
            refresh();
    }

    void MonitorPanel::resized()
    {
        auto area = getLocalBounds();
        area.removeFromTop (14 + 18 + 1 + 8); // section title
        auto buttons = area.removeFromTop (38);
        monitorButton.setBounds (buttons.removeFromLeft (buttons.getWidth() * 3 / 5).reduced (0, 2));
        buttons.removeFromLeft (8);
        recordButton.setBounds (buttons.reduced (0, 2));
        area.removeFromTop (6);

        auto grabRow = area.removeFromTop (36);
        grabLabelArea = grabRow.removeFromLeft (44);
        grabButton.setBounds (grabRow.removeFromRight (78).reduced (0, 2));
        grabRow.removeFromRight (8);
        const int each = grabRow.getWidth() / 3;
        for (auto& b : lengthButtons)
            b.setBounds (grabRow.removeFromLeft (each).reduced (2, 3));
        area.removeFromTop (6);

        auto outputRow = area.removeFromTop (26);
        outputRow.removeFromLeft (72);
        outputBox.setBounds (outputRow);
        area.removeFromTop (6);

        auto levelRow = area.removeFromTop (26);
        levelRow.removeFromLeft (72);
        levelValueArea = levelRow.removeFromRight (56);
        level.setBounds (levelRow);
        area.removeFromTop (10);

        meterArea = area.removeFromTop (30).toFloat();
        area.removeFromTop (6);
        infoArea = area.removeFromTop (32);
        takeArea = area.removeFromTop (24);
        showButton.setBounds (takeArea.removeFromRight (64).reduced (0, 1));
    }

    void MonitorPanel::paintMeter (juce::Graphics& g, juce::Rectangle<float> bar, float rmsDb, float peakDb, bool clipped) const
    {
        auto toX = [&bar] (float db) { return bar.getX() + bar.getWidth() * juce::jlimit (0.0f, 1.0f, (db + 60.0f) / 60.0f); };
        g.setColour (c.paperHigh);
        g.fillRect (bar);
        g.setColour (c.riddimGreen.withAlpha (0.85f));
        g.fillRect (bar.withRight (toX (rmsDb)));
        g.setColour (peakDb > -1.5f ? c.orange : c.ink);
        g.fillRect (juce::Rectangle<float> (toX (peakDb) - 1.0f, bar.getY(), 2.0f, bar.getHeight()));
        g.setColour (c.ink.withAlpha (0.18f));
        g.drawRect (bar, 1.0f);
        g.setColour (clipped ? c.warning : c.ink.withAlpha (0.12f));
        g.fillEllipse (bar.getRight() + 6.0f, bar.getCentreY() - 3.5f, 7.0f, 7.0f);
    }

    void MonitorPanel::paint (juce::Graphics& g)
    {
        auto area = getLocalBounds();
        area.removeFromTop (14);
        g.setFont (HaloType::label (11.0f));
        g.setColour (c.inkSoft);
        g.drawText ("MONITOR", area.removeFromTop (18), juce::Justification::centredLeft);
        g.setColour (c.ink.withAlpha (0.14f));
        g.fillRect (area.removeFromTop (1));

        g.setFont (HaloType::label (11.0f, false));
        g.setColour (c.inkSoft.withAlpha (0.85f));
        g.drawText ("OUTPUT", outputBox.getBounds().withX (0).withWidth (70), juce::Justification::centredLeft);
        g.drawText (model.grabUsesBars() ? "BARS" : "GRAB", grabLabelArea, juce::Justification::centredLeft);
        g.drawText ("LEVEL", level.getBounds().withX (0).withWidth (70), juce::Justification::centredLeft);
        g.setFont (HaloType::mono (13.0f));
        g.setColour (c.ink);
        const double db = level.getValue();
        g.drawText (db <= MonitorGain::silenceFloorDb ? juce::String ("MUTE") : juce::String (juce::roundToInt (db)) + " dB",
                    levelValueArea, juce::Justification::centredRight);
        g.setFont (HaloType::label (11.0f, false));
        g.setColour (c.inkSoft.withAlpha (0.85f));

        auto& engine = model.monitor();
        const bool running = engine.isRunning();
        const auto shown = running ? levels : StereoLevels {};
        auto meters = meterArea.withTrimmedRight (16.0f);
        g.drawText ("L", meters.removeFromLeft (14.0f).removeFromTop (14.0f), juce::Justification::centredLeft);
        paintMeter (g, meters.removeFromTop (12.0f), shown.rmsL, shown.peakL, clipL && running);
        meters.removeFromTop (4.0f);
        g.drawText ("R", meterArea.withTrimmedTop (16.0f).withWidth (14.0f), juce::Justification::centredLeft);
        paintMeter (g, meters.removeFromTop (12.0f), shown.rmsR, shown.peakR, clipR && running);

        // Route facts or the honest reason it isn't running.
        g.setFont (HaloType::mono (11.5f));
        if (model.grabMessage().isNotEmpty())
        {
            g.setColour (c.orange);
            g.drawFittedText (model.grabMessage(), infoArea, juce::Justification::centredLeft, 2, 1.0f);
        }
        else if (running)
        {
            const auto& r = engine.route();
            juce::String text = juce::String (r.sampleRate / 1000.0, 1) + " kHz  " + juce::String (r.bufferSize) + " fr  ~"
                              + juce::String (juce::roundToInt (r.latencyMs)) + " ms";
            if (engine.isRecording())
                text = "REC " + formatTime (engine.recordingSeconds()) + "   " + text;
            g.setColour (engine.isRecording() ? c.warning : c.inkSoft);
            g.drawText (text, infoArea, juce::Justification::centredLeft, true);
        }
        else if (engine.currentState() == MonitorEngine::State::failed)
        {
            g.setColour (c.warning);
            g.drawFittedText (engine.lastError(), infoArea, juce::Justification::centredLeft, 2, 1.0f);
        }
        else
        {
            g.setColour (c.inkSoft);
            g.drawText (engine.ep40InputName().isNotEmpty() ? "off" : "no EP-40 audio input found",
                        infoArea, juce::Justification::centredLeft, true);
        }

        if (const auto& take = model.lastTake())
        {
            const auto name = take->file.getFileName();
            const auto grabTitle = grab::titleFromFilename (name.toStdString());
            const auto label = grabTitle ? juce::String::fromUTF8 (grabTitle->c_str()).fromFirstOccurrenceOf ("GRAB", true, false)
                                         : name.upToLastOccurrenceOf (".", false, false).fromFirstOccurrenceOf ("Halo Take ", false, false)
                                               + "  " + juce::String (take->seconds, 1) + " s";
            g.setColour (c.ink);
            g.drawText (label, takeArea, juce::Justification::centredLeft, true);
        }
    }
}
