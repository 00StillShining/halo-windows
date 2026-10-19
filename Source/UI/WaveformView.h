#pragma once

#include "App/EditSession.h"

#include <juce_gui_basics/juce_gui_basics.h>

namespace halo
{
    // Waveform of the session's sample (WaveformStrip.swift / SliceWaveform.swift).
    //  - Edit style: drag the two trim handles; fades are shaded.
    //  - Chop style: cut markers. Drag to move, double-click to add,
    //    right-click a marker to remove it, click a slice to audition it.
    class WaveformView : public juce::Component, private juce::ChangeListener, private juce::Timer
    {
    public:
        enum class Style { edit, chop };

        WaveformView (EditSession&, Style);
        ~WaveformView() override;

        void paint (juce::Graphics&) override;
        void mouseDown (const juce::MouseEvent&) override;
        void mouseDrag (const juce::MouseEvent&) override;
        void mouseUp (const juce::MouseEvent&) override;
        void mouseDoubleClick (const juce::MouseEvent&) override;
        void mouseMove (const juce::MouseEvent&) override;

    private:
        void changeListenerCallback (juce::ChangeBroadcaster*) override { repaint(); }
        void timerCallback() override;

        float frameToX (double frame) const;
        int xToFrame (float x) const;
        int hitCut (float x) const;
        juce::Rectangle<float> plot() const;

        EditSession& session;
        const Style style;
        enum class Drag { none, trimStart, trimEnd, cut } drag = Drag::none;
        int dragCut = -1;
    };
}
