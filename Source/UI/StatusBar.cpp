#include "UI/StatusBar.h"
#include "UI/HaloColours.h"

namespace halo
{
    namespace
    {
        const auto& c = HaloColours::graphPaper;
    }

    StatusBar::StatusBar (AppModel& m) : model (m)
    {
        model.addListener (this);
        startTimerHz (2);
    }

    StatusBar::~StatusBar()
    {
        model.removeListener (this);
    }

    void StatusBar::displayChanged (const DisplayState& state)
    {
        // The feed mode only changes on connect / first live event: repaint then,
        // not on every note.
        if (state.feedMode != lastFeed)
        {
            lastFeed = state.feedMode;
            repaint();
        }
    }

    float StatusBar::drawChip (juce::Graphics& g, const Chip& chip, float x, float centreY) const
    {
        const auto labelFont = HaloType::label (11.0f, false);
        const auto valueFont = HaloType::label (13.5f, false);
        const float labelWidth = juce::GlyphArrangement::getStringWidth (labelFont, chip.label);
        const float valueWidth = juce::GlyphArrangement::getStringWidth (valueFont, chip.value);

        g.setFont (labelFont);
        g.setColour (c.inkSoft.withAlpha (0.85f));
        g.drawText (chip.label, juce::Rectangle<float> (x, centreY - 9.0f, labelWidth + 2.0f, 18.0f),
                    juce::Justification::centredLeft, false);

        g.setFont (valueFont);
        g.setColour (chip.tint);
        g.drawText (chip.value, juce::Rectangle<float> (x + labelWidth + 7.0f, centreY - 10.0f, valueWidth + 2.0f, 20.0f),
                    juce::Justification::centredLeft, false);

        return x + labelWidth + 7.0f + valueWidth + 30.0f;
    }

    void StatusBar::paint (juce::Graphics& g)
    {
        g.fillAll (c.paper);
        g.setColour (c.ink.withAlpha (0.16f));
        g.fillRect (getLocalBounds().removeFromBottom (1));

        const float centreY = (float) getHeight() * 0.5f;

        g.setColour (c.ink);
        g.setFont (HaloType::wordmark (34.0f));
        g.drawText ("halo", juce::Rectangle<float> (24.0f, 0.0f, 90.0f, (float) getHeight()), juce::Justification::centredLeft);

        const auto& ring = model.ring();
        const auto& life = model.lifecycle();
        const bool live = model.display().feedMode == FeedMode::live;
        const auto green = c.riddimGreen;

        float x = 132.0f;
        if (model.isMock())
            x = drawChip (g, { "SOURCE", "MOCK", c.orange }, x, centreY);

        x = drawChip (g, { "EP-40", model.deviceStatus(), c.ink }, x, centreY);
        x = drawChip (g, { "USB", model.usbStatus(), c.ink }, x, centreY);
        x = drawChip (g, { "STATE", juce::String (life.word()), life.isError() ? c.warning : (live ? green : c.ink) }, x, centreY);
        x = drawChip (g, { "DISPLAY", model.displayStatus(), live ? green : c.ink }, x, centreY);
        x = drawChip (g, { "HALO", juce::String (ring.statusWord()), ring.isError() ? c.warning : c.ink }, x, centreY);

        // Right-aligned audio chips, all from the running route.
        auto& engine = model.monitor();
        const bool running = engine.isRunning();
        auto outputName = running ? engine.route().outputName : noValue;
        if (outputName.length() > 22)
            outputName = outputName.substring (0, 21).trimEnd() + "...";

        juce::String monitorWord = "OFF";
        auto monitorTint = c.inkSoft;
        if (running)
        {
            monitorWord = juce::String (juce::roundToInt (engine.gainDb())) + " dB";
            monitorTint = green;
        }
        else if (engine.currentState() == MonitorEngine::State::failed)
        {
            monitorWord = "ERROR";
            monitorTint = c.warning;
        }

        juce::String recWord = noValue;
        if (engine.isRecording())
        {
            const int s = (int) engine.recordingSeconds();
            recWord = juce::String (s / 60).paddedLeft ('0', 2) + ":" + juce::String (s % 60).paddedLeft ('0', 2);
        }

        const auto labelFont = HaloType::label (11.0f, false);
        const Chip rightChips[] { { "OUTPUT", outputName.toUpperCase(), running ? c.ink : c.inkSoft },
                                  { "MONITOR", monitorWord, monitorTint },
                                  { "REC", recWord, engine.isRecording() ? c.warning : c.inkSoft } };
        float right = (float) getWidth() - 24.0f;
        for (int i = 2; i >= 0; --i)
        {
            const auto& chip = rightChips[i];
            const float w = juce::GlyphArrangement::getStringWidth (labelFont, chip.label)
                          + juce::GlyphArrangement::getStringWidth (HaloType::label (13.5f, false), chip.value) + 9.0f;
            right -= w;
            if (right > x)
                drawChip (g, chip, right, centreY);
            right -= 30.0f;
        }
    }
}
