#pragma once

#include "App/AppModel.h"
#include "UI/WaveformView.h"

#include <juce_gui_basics/juce_gui_basics.h>

namespace halo
{
    // CHOP (ChopSurface.swift): automatic slicing with editable cuts, slice
    // audition (click, or keys 1-9 and 0), a plan of which pads the slices
    // would land on, and export of every slice as an EP-40-ready WAV with a
    // pads.txt guide. SEND TO PADS stays disabled until the protocol is known.
    class ChopSurface : public juce::Component, private juce::ChangeListener
    {
    public:
        explicit ChopSurface (AppModel&);
        ~ChopSurface() override;

        void paint (juce::Graphics&) override;
        void resized() override;
        bool keyPressed (const juce::KeyPress&) override;

    private:
        // 3x4 pad grid showing where each slice lands; click to set the start pad.
        struct PadGrid : public juce::Component
        {
            explicit PadGrid (EditSession& s) : session (s) {}
            void paint (juce::Graphics&) override;
            void mouseUp (const juce::MouseEvent&) override;
            juce::Rectangle<float> cell (int gridIndex) const;
            EditSession& session;
        };

        void changeListenerCallback (juce::ChangeBroadcaster*) override { refresh(); }
        void refresh();

        AppModel& model;
        EditSession& session;
        WaveformView waveform;
        PadGrid pads;
        juce::Slider sensitivity;
        juce::TextButton detectButton { "DETECT" }, clearButton { "CLEAR CUTS" }, closeButton { "CLOSE" },
                         exportButton { "EXPORT SLICES" }, sendButton { "SEND TO PADS" }, showButton { "SHOW" };
        std::array<juce::TextButton, 4> groupButtons;
        std::array<juce::TextButton, 5> qualityButtons;
        juce::TextEditor nameEditor;
        juce::Rectangle<int> headerArea, hintArea, sensitivityLabel, planArea, qualityLabel, nameLabel, totalsArea, statusArea, noteArea;
    };
}
