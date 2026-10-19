#pragma once

#include "App/AppModel.h"
#include "UI/WaveformView.h"

#include <juce_gui_basics/juce_gui_basics.h>

namespace halo
{
    // EDIT mode (EditRail.swift): non-destructive trim / fades / gain /
    // normalise / channels, an EP-40 quality preset, the exact size it will
    // take, audition, and export as an EP-40-ready 16-bit WAV.
    class EditPanel : public juce::Component, private juce::ChangeListener
    {
    public:
        explicit EditPanel (AppModel&);
        ~EditPanel() override;

        void paint (juce::Graphics&) override;
        void resized() override;

    private:
        void changeListenerCallback (juce::ChangeBroadcaster*) override { refresh(); }
        void refresh();
        juce::Slider& addKnob (juce::Slider&, double min, double max, double mid, std::function<void()> onChange);

        AppModel& model;
        EditSession& session;
        WaveformView waveform;
        juce::Slider fadeIn, fadeOut, gain;
        juce::TextButton normalizeButton { "NORMALIZE" }, resetTrimButton { "RESET TRIM" };
        std::array<juce::TextButton, 3> channelButtons;
        std::array<juce::TextButton, 5> qualityButtons;
        juce::TextButton auditionButton { "AUDITION" }, exportButton { "EXPORT" }, chopButton { "CHOP" },
                         sendButton { "SEND TO EP-40" }, showButton { "SHOW" }, libraryButton { "OPEN LIBRARY" };
        juce::Rectangle<int> headerArea, trimArea, knobLabelArea, channelLabel, qualityLabel, sizeArea, statusArea, sendNoteArea;
    };
}
