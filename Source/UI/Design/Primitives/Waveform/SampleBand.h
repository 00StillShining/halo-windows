#pragma once

#include "App/EditSession.h"
#include "UI/Design/Primitives/Waveform/WaveCache.h"

#include <juce_gui_basics/juce_gui_basics.h>

namespace halo
{
    // EDIT's sampler band (docs/redesign/plan.md sections 2.5 and 3.8), the
    // OP-1 field paper band: black mirrored lanes on paper, four colour-coded
    // full-height markers with 6 x 4 tabs at both ends (in = blue, fade-in end
    // = ochre, fade-out start = grey, out = orange; colours of lines and tabs,
    // never of text), grey fade wedges, the outside of the trim hatched over
    // paperShade, and the hot playhead while auditioning. Drag a marker to
    // move it; the wheel over a marker turns its encoder; ctrl+wheel zooms
    // (an overview strip then shows in the band's top 14 px).
    class SampleBand : public juce::Component, private juce::ChangeListener, private juce::Timer
    {
    public:
        explicit SampleBand (EditSession&);
        ~SampleBand() override;

        enum class Marker { none, in, fadeIn, fadeOut, out };
        // The wheel over a marker turns that marker's encoder (0-3).
        std::function<void (int encoder, const juce::MouseEvent&, const juce::MouseWheelDetails&)> onMarkerWheel;
        void zoomTo (bool start); // encoder 2 / 3 press: zoom to the start or the end
        void resetView();

        void paint (juce::Graphics&) override;
        void mouseMove (const juce::MouseEvent&) override;
        void mouseDown (const juce::MouseEvent&) override;
        void mouseDrag (const juce::MouseEvent&) override;
        void mouseUp (const juce::MouseEvent&) override;
        void mouseWheelMove (const juce::MouseEvent&, const juce::MouseWheelDetails&) override;

    private:
        void changeListenerCallback (juce::ChangeBroadcaster*) override;
        void timerCallback() override;
        juce::Rectangle<float> plot() const;
        float frameToX (double) const;
        double xToFrame (float) const;
        Marker markerAt (float x) const;
        void clampView();

        EditSession& session;
        WaveCache cache;
        std::shared_ptr<const std::vector<float>> source;
        std::int64_t start = 0, end = 0;
        Marker dragging = Marker::none;
        float wheelAcc = 0.0f;
    };
}
