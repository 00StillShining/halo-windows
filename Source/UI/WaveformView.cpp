#include "UI/WaveformView.h"
#include "UI/HaloColours.h"

namespace halo
{
    namespace
    {
        const auto& c = legacyColours();
        constexpr float handleGrab = 8.0f;
    }

    WaveformView::WaveformView (EditSession& s, Style st) : session (s), style (st)
    {
        session.addChangeListener (this);
        startTimerHz (30); // playhead
    }

    WaveformView::~WaveformView()
    {
        session.removeChangeListener (this);
    }

    juce::Rectangle<float> WaveformView::plot() const
    {
        return getLocalBounds().toFloat().reduced (1.0f, 6.0f);
    }

    float WaveformView::frameToX (double frame) const
    {
        const auto p = plot();
        const auto audio = session.audio();
        const int n = audio != nullptr ? audio->frameCount() : 0;
        return n > 0 ? p.getX() + p.getWidth() * (float) (frame / n) : p.getX();
    }

    int WaveformView::xToFrame (float x) const
    {
        const auto p = plot();
        const auto audio = session.audio();
        const int n = audio != nullptr ? audio->frameCount() : 0;
        return juce::jlimit (0, n, (int) std::lround ((x - p.getX()) / p.getWidth() * n));
    }

    int WaveformView::hitCut (float x) const
    {
        const auto& cuts = session.slices.cuts();
        for (int i = 0; i < (int) cuts.size(); ++i)
            if (std::abs (frameToX (cuts[(size_t) i]) - x) <= handleGrab)
                return i;
        return -1;
    }

    void WaveformView::timerCallback()
    {
        if (session.player().isPlaying())
            repaint();
    }

    void WaveformView::paint (juce::Graphics& g)
    {
        const auto p = plot();
        g.setColour (c.paperHigh);
        g.fillRoundedRectangle (getLocalBounds().toFloat(), HaloMetrics::radiusSmall);
        g.setColour (c.ink.withAlpha (0.18f));
        g.drawRoundedRectangle (getLocalBounds().toFloat().reduced (0.5f), HaloMetrics::radiusSmall, 1.0f);

        const auto audio = session.audio();
        if (audio == nullptr || audio->frameCount() == 0)
        {
            g.setFont (HaloType::mono (12.0f));
            g.setColour (c.inkSoft);
            g.drawText (session.isLoading() ? "decoding..." : session.error().isNotEmpty() ? session.error() : "no sample open",
                        getLocalBounds(), juce::Justification::centred);
            return;
        }

        const int n = audio->frameCount();
        const auto trim = session.prep.resolvedTrim (n);

        // Chop: shade the selected slice.
        if (style == Style::chop && session.selectedSlice)
        {
            const auto all = session.slices.slices();
            if (*session.selectedSlice < (int) all.size())
            {
                const auto& s = all[(size_t) *session.selectedSlice];
                g.setColour (c.orange.withAlpha (0.14f));
                g.fillRect (juce::Rectangle<float>::leftTopRightBottom (frameToX (s.startFrame), p.getY(), frameToX (s.endFrame), p.getBottom()));
            }
        }

        // Waveform: one min/max line per pixel column from the summary.
        const auto& w = session.summary();
        const float mid = p.getCentreY(), half = p.getHeight() * 0.48f;
        const float peak = juce::jmax (0.05f, w.peakMagnitude());
        for (int x = 0; x < (int) p.getWidth(); ++x)
        {
            const int b0 = (int) ((float) x / p.getWidth() * (float) w.bucketCount());
            const int b1 = juce::jmax (b0 + 1, (int) ((float) (x + 1) / p.getWidth() * (float) w.bucketCount()));
            float lo = 0.0f, hi = 0.0f;
            for (int b = b0; b < juce::jmin (b1, w.bucketCount()); ++b)
            {
                lo = juce::jmin (lo, w.minima[(size_t) b]);
                hi = juce::jmax (hi, w.maxima[(size_t) b]);
            }
            const float px = p.getX() + (float) x;
            const int frame = (int) ((double) x / p.getWidth() * n);
            const bool kept = style == Style::chop || (frame >= trim.start && frame < trim.end);
            g.setColour (kept ? c.riddimGreen : c.inkSoft.withAlpha (0.3f));
            g.drawVerticalLine ((int) px, mid - hi / peak * half, mid - lo / peak * half + 1.0f);
        }

        if (style == Style::edit)
        {
            // Trimmed-away regions, fades, and the two handles.
            g.setColour (c.ink.withAlpha (0.08f));
            g.fillRect (juce::Rectangle<float>::leftTopRightBottom (p.getX(), p.getY(), frameToX (trim.start), p.getBottom()));
            g.fillRect (juce::Rectangle<float>::leftTopRightBottom (frameToX (trim.end), p.getY(), p.getRight(), p.getBottom()));

            auto fade = [&] (int fromFrame, int toFrame, bool in)
            {
                if (toFrame <= fromFrame)
                    return;
                juce::Path path;
                const float x0 = frameToX (fromFrame), x1 = frameToX (toFrame);
                path.startNewSubPath (x0, in ? p.getBottom() : p.getY());
                path.lineTo (x1, in ? p.getY() : p.getBottom());
                g.setColour (c.orange.withAlpha (0.8f));
                g.strokePath (path, juce::PathStrokeType (1.5f));
            };
            fade (trim.start, juce::jmin (trim.end, trim.start + session.prep.fadeInFrames), true);
            fade (juce::jmax (trim.start, trim.end - session.prep.fadeOutFrames), trim.end, false);

            for (int frame : { trim.start, trim.end })
            {
                const float x = frameToX (frame);
                g.setColour (c.ink);
                g.fillRect (x - 1.0f, p.getY() - 6.0f, 2.0f, p.getHeight() + 12.0f);
                g.fillRect (x - 5.0f, p.getY() - 6.0f, 10.0f, 6.0f);
            }
        }
        else
        {
            // Cut markers with slice numbers (matching the pad plan order).
            const auto& cuts = session.slices.cuts();
            g.setFont (HaloType::label (10.0f));
            for (size_t i = 0; i < cuts.size(); ++i)
            {
                const float x = frameToX (cuts[i]);
                g.setColour (c.ink);
                g.fillRect (x - 1.0f, p.getY() - 6.0f, 2.0f, p.getHeight() + 12.0f);
                g.fillRect (x - 4.0f, p.getY() - 6.0f, 8.0f, 6.0f);
            }
            const auto all = session.slices.slices();

            // AUTO-TRIM: hatch the tail each slice loses.
            for (const auto& s : all)
            {
                const auto kept = session.playedPart (s);
                if (kept.endFrame >= s.endFrame)
                    continue;
                const auto tail = juce::Rectangle<float>::leftTopRightBottom (frameToX (kept.endFrame), p.getY(), frameToX (s.endFrame), p.getBottom());
                g.setColour (c.paperHigh.withAlpha (0.7f));
                g.fillRect (tail);
                g.saveState();
                g.reduceClipRegion (tail.toNearestInt());
                g.setColour (c.ink.withAlpha (0.12f));
                for (float x = tail.getX() - tail.getHeight(); x < tail.getRight(); x += 8.0f)
                    g.drawLine (x, tail.getBottom(), x + tail.getHeight(), tail.getY(), 1.0f);
                g.restoreState();
                g.setColour (c.orange.withAlpha (0.8f));
                g.fillRect (tail.getX() - 0.5f, p.getY(), 1.0f, p.getHeight());
            }

            g.setColour (c.inkSoft);
            for (const auto& s : all)
            {
                const float x0 = frameToX (s.startFrame), x1 = frameToX (s.endFrame);
                if (x1 - x0 > 16.0f)
                    g.drawText (juce::String (s.index + 1), juce::Rectangle<float> (x0 + 3.0f, p.getY(), x1 - x0 - 6.0f, 14.0f),
                                juce::Justification::topLeft);
            }
        }

        // Playhead while auditioning.
        auto& player = session.player();
        if (player.isPlaying())
        {
            double startFrame = trim.start, endFrame = trim.end;
            if (style == Style::chop)
            {
                const auto all = session.slices.slices();
                if (player.token() >= 0 && player.token() < (int) all.size())
                {
                    const auto kept = session.playedPart (all[(size_t) player.token()]);
                    startFrame = kept.startFrame;
                    endFrame = kept.endFrame;
                }
            }
            const float x = frameToX (startFrame + (endFrame - startFrame) * player.progress());
            g.setColour (c.orange);
            g.fillRect (x - 1.0f, p.getY(), 2.0f, p.getHeight());
        }
    }

    void WaveformView::mouseMove (const juce::MouseEvent& e)
    {
        auto shape = juce::MouseCursor::NormalCursor;
        const auto audio = session.audio();
        if (audio != nullptr)
        {
            const float x = (float) e.x;
            if (style == Style::edit)
            {
                const auto t = session.prep.resolvedTrim (audio->frameCount());
                if (std::abs (x - frameToX (t.start)) <= handleGrab || std::abs (x - frameToX (t.end)) <= handleGrab)
                    shape = juce::MouseCursor::LeftRightResizeCursor;
            }
            else if (hitCut (x) >= 0)
            {
                shape = juce::MouseCursor::LeftRightResizeCursor;
            }
        }
        setMouseCursor (shape);
    }

    void WaveformView::mouseDown (const juce::MouseEvent& e)
    {
        const auto audio = session.audio();
        if (audio == nullptr)
            return;
        const float x = (float) e.x;

        if (style == Style::edit)
        {
            const auto t = session.prep.resolvedTrim (audio->frameCount());
            const float ds = std::abs (x - frameToX (t.start)), de = std::abs (x - frameToX (t.end));
            if (juce::jmin (ds, de) <= handleGrab)
                drag = ds <= de ? Drag::trimStart : Drag::trimEnd;
            return;
        }

        const int cut = hitCut (x);
        if (e.mods.isPopupMenu())
        {
            if (cut >= 0)
            {
                session.slices.removeCut (cut);
                session.selectedSlice.reset();
                session.changed();
            }
            return;
        }
        if (cut >= 0)
        {
            drag = Drag::cut;
            dragCut = cut;
            return;
        }
        session.auditionSlice (session.slices.sliceAt (xToFrame (x)));
    }

    void WaveformView::mouseDrag (const juce::MouseEvent& e)
    {
        const auto audio = session.audio();
        if (audio == nullptr || drag == Drag::none)
            return;
        const int frame = xToFrame ((float) e.x);
        const int n = audio->frameCount();

        if (drag == Drag::trimStart)
        {
            const auto t = session.prep.resolvedTrim (n);
            session.prep.trimStartFrame = juce::jlimit (0, juce::jmax (0, t.end - 64), frame);
        }
        else if (drag == Drag::trimEnd)
        {
            const auto t = session.prep.resolvedTrim (n);
            const int end = juce::jlimit (juce::jmin (n, t.start + 64), n, frame);
            session.prep.trimEndFrame = end >= n ? std::optional<int>() : std::optional<int> (end);
        }
        else if (drag == Drag::cut)
        {
            session.slices.moveCut (dragCut, frame, session.minGap());
        }
        session.changed();
    }

    void WaveformView::mouseUp (const juce::MouseEvent&)
    {
        drag = Drag::none;
        dragCut = -1;
    }

    void WaveformView::mouseDoubleClick (const juce::MouseEvent& e)
    {
        if (style != Style::chop || session.audio() == nullptr)
            return;
        if (session.slices.addCut (xToFrame ((float) e.x), session.minGap()))
        {
            session.selectedSlice = session.slices.sliceAt (xToFrame ((float) e.x));
            session.changed();
        }
    }
}
