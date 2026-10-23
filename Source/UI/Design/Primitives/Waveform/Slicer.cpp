#include "UI/Design/Primitives/Waveform/Slicer.h"
#include "Core/EncoderMath.h"
#include "UI/Design/Paint.h"

namespace halo
{
    namespace
    {
        constexpr float pitch = 4.0f, column = 3.0f, handle = 9.0f, reach = 7.0f;
    }

    Slicer::Slicer (EditSession& s) : session (s)
    {
        setOpaque (true);
        cache.onReady = [this] { repaint(); };
        session.addChangeListener (this);
        changeListenerCallback (nullptr);
        startTimerHz (30); // the playhead, while auditioning
    }

    Slicer::~Slicer()
    {
        session.removeChangeListener (this);
    }

    void Slicer::changeListenerCallback (juce::ChangeBroadcaster*)
    {
        auto samples = session.monoSamples();
        if (samples != source)
        {
            source = samples;
            cache.setSource (source);
            resetView();
        }
        repaint();
    }

    void Slicer::timerCallback()
    {
        if (session.player().isPlaying())
            repaint();
    }

    void Slicer::resetView()
    {
        start = 0;
        end = source != nullptr ? (std::int64_t) source->size() : 0;
        if (onViewChanged)
            onViewChanged();
        repaint();
    }

    void Slicer::clampView()
    {
        const auto n = source != nullptr ? (std::int64_t) source->size() : 0;
        const auto span = juce::jlimit<std::int64_t> (std::min<std::int64_t> (n, 2048), std::max<std::int64_t> (n, 1), end - start);
        start = juce::jlimit<std::int64_t> (0, std::max<std::int64_t> (0, n - span), start);
        end = start + span;
        if (onViewChanged)
            onViewChanged();
    }

    juce::Rectangle<float> Slicer::plot() const
    {
        return getLocalBounds().toFloat().reduced (0.0f, 12.0f);
    }

    void Slicer::resized()
    {
        repaint();
    }

    float Slicer::frameToX (double frame) const
    {
        const auto p = plot();
        const double span = (double) std::max<std::int64_t> (1, end - start);
        return p.getX() + p.getWidth() * (float) ((frame - (double) start) / span);
    }

    double Slicer::xToFrame (float x) const
    {
        const auto p = plot();
        const double span = (double) std::max<std::int64_t> (1, end - start);
        return juce::jlimit (0.0, (double) (source != nullptr ? source->size() : 0), (double) start + (x - p.getX()) / p.getWidth() * span);
    }

    int Slicer::cutAt (float x) const
    {
        const auto& cuts = session.slices.cuts();
        int best = -1;
        float bestD = reach;
        for (int i = 0; i < (int) cuts.size(); ++i)
        {
            const float d = std::abs (frameToX (cuts[(size_t) i]) - x);
            if (d <= bestD)
            {
                best = i;
                bestD = d;
            }
        }
        return best;
    }

    int Slicer::handleAt (juce::Point<float> p) const
    {
        const auto r = getLocalBounds().toFloat();
        if (p.y > r.getY() + handle + 2.0f && p.y < r.getBottom() - handle - 2.0f)
            return -1; // handles live at the top and bottom edges
        return cutAt (p.x);
    }

    void Slicer::paint (juce::Graphics& g)
    {
        const auto& t = theme();
        const auto bounds = getLocalBounds().toFloat();
        g.fillAll (t.scr.bg);
        g.setColour (t.scr.wave);
        g.fillRoundedRectangle (bounds, 4.0f);
        const auto p = plot();

        if (source == nullptr || source->empty())
        {
            const auto text = session.isLoading() ? juce::String ("decoding...")
                            : session.error().isNotEmpty() ? session.error()
                                                           : juce::String ("no sample open: pick a take, grab or file, then chop it");
            paint::text (g, text, bounds, HaloFonts::font (Role::ui), t.scr.dim, juce::Justification::centred);
            return;
        }

        const auto all = session.slices.slices();
        const int selected = session.selectedSlice.value_or (-1);
        auto sliceForFrame = [&all] (double frame)
        {
            for (int i = 0; i < (int) all.size(); ++i)
                if (frame >= all[(size_t) i].startFrame && frame < all[(size_t) i].endFrame)
                    return i;
            return (int) all.size() - 1;
        };

        // The selected slice's backdrop.
        if (selected >= 0 && selected < (int) all.size())
        {
            const auto& s = all[(size_t) selected];
            g.setColour (t.scr.waveSel);
            g.fillRect (juce::Rectangle<float>::leftTopRightBottom (juce::jmax (p.getX(), frameToX (s.startFrame)), bounds.getY(),
                                                                    juce::jmin (p.getRight(), frameToX (s.endFrame)), bounds.getBottom()));
        }

        // The stepped columns.
        const int columns = juce::jmax (1, (int) (p.getWidth() / pitch));
        const auto* cols = cache.get (columns, start, end);
        const float mid = p.getCentreY(), half = p.getHeight() * 0.5f;
        if (cols != nullptr && cols->count > 0)
        {
            const float peak = juce::jmax (0.05f, cols->peak);
            const float stepW = p.getWidth() / (float) cols->count;
            for (int c = 0; c < cols->count; ++c)
            {
                const float x = p.getX() + (float) c * stepW;
                const double frame = (double) cols->start + (double) (cols->end - cols->start) * ((double) c + 0.5) / cols->count;
                // A gentle curve so quiet passages still read next to the kicks.
                const float a = std::pow (juce::jmax (std::abs (cols->mins[(size_t) c]), std::abs (cols->maxs[(size_t) c])) / peak, 0.6f);
                const float h = juce::jmax (4.0f, std::round (a * half / 4.0f) * 4.0f); // snapped to 4 px
                const int si = sliceForFrame (frame);
                bool dropped = false;
                if (si >= 0 && si < (int) all.size())
                    dropped = frame >= session.playedPart (all[(size_t) si]).endFrame;
                g.setColour (dropped ? t.scr.droppedTail : si == selected ? t.scr.ramp[7] : t.scr.ramp[4]);
                g.fillRect (juce::Rectangle<float> (x, mid - h, juce::jmin (column, stepW * 0.75f), h * 2.0f));
            }
        }

        // Dropped tails: hatched, an orange edge, a foot tab and the cut-off chip.
        const auto orange = t.encoder (3).line;
        const auto chipFont = HaloFonts::font (Role::mono, 10.5f);
        for (const auto& s : all)
        {
            const auto kept = session.playedPart (s);
            if (kept.endFrame >= s.endFrame)
                continue;
            const auto tail = juce::Rectangle<float>::leftTopRightBottom (juce::jmax (p.getX(), frameToX (kept.endFrame)), bounds.getY(),
                                                                          juce::jmin (p.getRight(), frameToX (s.endFrame)), bounds.getBottom());
            if (tail.getWidth() <= 0.0f)
                continue;
            paint::hatch (g, tail, t.scr.hatch.withAlpha (0.7f));
            g.setColour (orange);
            g.fillRect (juce::Rectangle<float> (tail.getX() - 0.75f, bounds.getY(), 1.5f, bounds.getHeight()));
            juce::Path foot;
            foot.addTriangle (tail.getX(), bounds.getY(), tail.getX() + 8.0f, bounds.getY(), tail.getX(), bounds.getY() + 8.0f);
            g.fillPath (foot);
            const double lost = (s.endFrame - kept.endFrame) / juce::jmax (1.0, session.audio() != nullptr ? session.audio()->sampleRate : 44100.0);
            const juce::String chip = juce::String::charToString ((juce::juce_wchar) 0x2212) + juce::String (lost, 1) + "s";
            const float cw = paint::textWidth (chipFont, chip) + 10.0f;
            if (tail.getWidth() > cw + 8.0f)
            {
                auto r = juce::Rectangle<float> (tail.getCentreX() - cw * 0.5f, bounds.getBottom() - 30.0f, cw, 16.0f);
                g.setColour (t.scr.bg);
                g.fillRect (r);
                g.setColour (orange);
                g.drawRect (r, 1.0f);
                paint::text (g, chip, r, chipFont, orange, juce::Justification::centred);
            }
        }

        // Cut lines with square handles; the selected slice's leading cut in orange.
        const auto& cuts = session.slices.cuts();
        for (int i = 0; i < (int) cuts.size(); ++i)
        {
            const float x = frameToX (cuts[(size_t) i]);
            if (x < p.getX() - 2.0f || x > p.getRight() + 2.0f)
                continue;
            g.setColour (juce::Colours::black);
            g.fillRect (juce::Rectangle<float> (x - 1.5f, bounds.getY(), 3.0f, bounds.getHeight()));
            const bool lead = selected == i + 1;
            g.setColour (lead ? orange : t.scr.ramp[5]);
            g.fillRect (juce::Rectangle<float> (handle, handle).withCentre ({ x, bounds.getY() + handle * 0.5f }));
            g.fillRect (juce::Rectangle<float> (handle, handle).withCentre ({ x, bounds.getBottom() - handle * 0.5f }));
        }

        // Slice numbers, top-left of each slice; the selected one in a white chip.
        const auto numFont = HaloFonts::face (HaloFonts::Face::interMedium, 11.5f);
        for (const auto& s : all)
        {
            const float x0 = frameToX (s.startFrame) + 6.0f, x1 = frameToX (s.endFrame);
            if (x1 - x0 < 14.0f || x1 < p.getX() || x0 > p.getRight())
                continue;
            const juce::String n (s.index + 1);
            auto r = juce::Rectangle<float> (juce::jmax (p.getX() + 4.0f, x0), bounds.getY() + 14.0f, paint::textWidth (numFont, n) + 8.0f, 16.0f);
            if (s.index == selected)
            {
                g.setColour (t.scr.hot);
                g.fillRoundedRectangle (r, 2.0f);
                paint::text (g, n, r, numFont, juce::Colour (0xff000005), juce::Justification::centred);
            }
            else
            {
                paint::text (g, n, r, numFont, t.scr.ink, juce::Justification::centred);
            }
        }

        // The playhead while a slice plays.
        auto& player = session.player();
        if (player.isPlaying() && player.token() >= 0 && player.token() < (int) all.size())
        {
            const auto kept = session.playedPart (all[(size_t) player.token()]);
            const float x = frameToX (kept.startFrame + (kept.endFrame - kept.startFrame) * player.progress());
            g.setColour (t.scr.hot);
            g.fillRect (juce::Rectangle<float> (x - 1.0f, bounds.getY(), 2.0f, bounds.getHeight()));
        }
    }

    void Slicer::mouseMove (const juce::MouseEvent& e)
    {
        setMouseCursor (cutAt (e.position.x) >= 0 ? juce::MouseCursor::LeftRightResizeCursor : juce::MouseCursor::PointingHandCursor);
    }

    void Slicer::mouseDown (const juce::MouseEvent& e)
    {
        dragged = false;
        dragCut = -1;
        if (source == nullptr)
            return;
        const int cut = cutAt (e.position.x);
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
        dragCut = cut;
    }

    void Slicer::mouseDrag (const juce::MouseEvent& e)
    {
        if (dragCut < 0 || source == nullptr)
            return;
        dragged = true;
        session.slices.moveCut (dragCut, (int) xToFrame (e.position.x), session.minGap());
        session.changed();
    }

    void Slicer::mouseUp (const juce::MouseEvent& e)
    {
        if (source != nullptr && ! dragged && dragCut < 0 && e.mods.isLeftButtonDown() && e.getNumberOfClicks() < 2)
            session.auditionSlice (session.slices.sliceAt ((int) xToFrame (e.position.x)));
        dragCut = -1;
        dragged = false;
    }

    void Slicer::mouseDoubleClick (const juce::MouseEvent& e)
    {
        if (source == nullptr)
            return;
        const int frame = (int) xToFrame (e.position.x);
        if (session.slices.addCut (frame, session.minGap()))
        {
            session.selectedSlice = session.slices.sliceAt (frame);
            session.changed();
        }
    }

    void Slicer::mouseWheelMove (const juce::MouseEvent& e, const juce::MouseWheelDetails& w)
    {
        if (source == nullptr || source->empty())
            return;
        wheelAcc += (std::abs (w.deltaY) >= std::abs (w.deltaX) ? w.deltaY : -w.deltaX) * (w.isReversed ? -1.0f : 1.0f);
        const float notch = (float) encoder::kNotch;
        int detents = 0;
        while (std::abs (wheelAcc) >= notch * 0.85f)
        {
            const int dir = wheelAcc > 0 ? 1 : -1;
            wheelAcc -= (float) dir * notch;
            detents += dir;
        }
        if (detents == 0)
            return;

        const double rate = session.audio() != nullptr ? session.audio()->sampleRate : 44100.0;
        if (const int cut = handleAt (e.position); cut >= 0 && ! e.mods.isCtrlDown())
        {
            // Nudge that cut: 10 ms a detent, alt 1 ms.
            const int step = (int) std::lround ((e.mods.isAltDown() ? 0.001 : 0.010) * rate);
            session.slices.moveCut (cut, session.slices.cuts()[(size_t) cut] + detents * step, session.minGap());
            session.changed();
            return;
        }
        const auto n = (std::int64_t) source->size();
        const auto span = end - start;
        if (e.mods.isCtrlDown())
        {
            // Zoom around the pointer.
            const double anchor = xToFrame (e.position.x);
            const double f = std::pow (0.8, (double) detents);
            const auto newSpan = (std::int64_t) juce::jlimit (2048.0, (double) n, (double) span * f);
            const double frac = (anchor - (double) start) / (double) std::max<std::int64_t> (1, span);
            start = (std::int64_t) (anchor - frac * (double) newSpan);
            end = start + newSpan;
        }
        else if (span < n)
        {
            const auto shift = (std::int64_t) ((double) span * 0.1 * (double) -detents);
            start += shift;
            end += shift;
        }
        clampView();
        repaint();
    }
}
