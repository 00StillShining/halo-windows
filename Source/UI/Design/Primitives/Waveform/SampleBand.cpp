#include "UI/Design/Primitives/Waveform/SampleBand.h"
#include "Core/EncoderMath.h"
#include "UI/Design/Paint.h"

namespace halo
{
    namespace
    {
        constexpr float reach = 7.0f;
        const juce::Colour ink { 0xff000005 };
    }

    SampleBand::SampleBand (EditSession& s) : session (s)
    {
        setOpaque (true);
        cache.onReady = [this] { repaint(); };
        session.addChangeListener (this);
        changeListenerCallback (nullptr);
        startTimerHz (30);
    }

    SampleBand::~SampleBand()
    {
        session.removeChangeListener (this);
    }

    void SampleBand::changeListenerCallback (juce::ChangeBroadcaster*)
    {
        auto samples = session.monoSamples();
        if (samples != source || ! session.hasSource())
        {
            source = session.hasSource() ? samples : nullptr;
            cache.setSource (source);
            resetView();
        }
        repaint();
    }

    void SampleBand::timerCallback()
    {
        if (session.player().isPlaying() && session.player().token() == -1)
            repaint();
    }

    void SampleBand::resetView()
    {
        start = 0;
        end = source != nullptr ? (std::int64_t) source->size() : 0;
        repaint();
    }

    void SampleBand::clampView()
    {
        const auto n = source != nullptr ? (std::int64_t) source->size() : 0;
        const auto span = juce::jlimit<std::int64_t> (std::min<std::int64_t> (n, 1024), std::max<std::int64_t> (n, 1), end - start);
        start = juce::jlimit<std::int64_t> (0, std::max<std::int64_t> (0, n - span), start);
        end = start + span;
    }

    void SampleBand::zoomTo (bool atStart)
    {
        if (source == nullptr)
            return;
        const int n = (int) source->size();
        const auto t = session.prep.resolvedTrim (n);
        const double rate = session.audio() != nullptr ? session.audio()->sampleRate : 44100.0;
        const auto span = (std::int64_t) juce::jmin ((double) n, 4.0 * rate);
        const auto centre = (std::int64_t) (atStart ? t.start : t.end);
        start = centre - span / 2;
        end = start + span;
        clampView();
        repaint();
    }

    juce::Rectangle<float> SampleBand::plot() const
    {
        // Inset so a marker at either end keeps its whole line and tab.
        auto r = getLocalBounds().toFloat().reduced (10.0f, 12.0f);
        if (end - start < (source != nullptr ? (std::int64_t) source->size() : 0))
            r.removeFromTop (14.0f); // the overview strip
        return r;
    }

    float SampleBand::frameToX (double f) const
    {
        const auto p = plot();
        return p.getX() + p.getWidth() * (float) ((f - (double) start) / (double) std::max<std::int64_t> (1, end - start));
    }

    double SampleBand::xToFrame (float x) const
    {
        const auto p = plot();
        return juce::jlimit (0.0, (double) (source != nullptr ? source->size() : 0),
                             (double) start + (x - p.getX()) / p.getWidth() * (double) (end - start));
    }

    SampleBand::Marker SampleBand::markerAt (float x) const
    {
        if (source == nullptr)
            return Marker::none;
        const auto t = session.prep.resolvedTrim ((int) source->size());
        const std::pair<Marker, double> marks[] { { Marker::in, (double) t.start }, { Marker::out, (double) t.end },
                                                  { Marker::fadeIn, (double) juce::jmin (t.end, t.start + session.prep.fadeInFrames) },
                                                  { Marker::fadeOut, (double) juce::jmax (t.start, t.end - session.prep.fadeOutFrames) } };
        Marker best = Marker::none;
        float bestD = reach;
        for (const auto& [m, f] : marks)
        {
            const float d = std::abs (frameToX (f) - x);
            if (d <= bestD)
            {
                best = m;
                bestD = d;
            }
        }
        return best;
    }

    void SampleBand::paint (juce::Graphics& g)
    {
        const auto& t = theme();
        const auto bounds = getLocalBounds().toFloat();
        g.fillAll (t.scr.bg);
        g.setColour (t.scr.paper);
        g.fillRoundedRectangle (bounds, 4.0f);

        const auto audio = session.audio();
        if (source == nullptr || audio == nullptr || source->empty())
        {
            const auto text = session.isLoading() ? juce::String ("decoding...")
                            : session.error().isNotEmpty() ? session.error() : juce::String ("no sample open");
            if (session.isLoading())
            {
                // A dot sweep across the band while decoding (no spinner).
                const int dots = 32;
                const int lit = (int) (juce::Time::getMillisecondCounter() / 80) % dots;
                for (int i = 0; i < dots; ++i)
                {
                    const auto c = juce::Point<float> (bounds.getX() + bounds.getWidth() * ((float) i + 0.5f) / dots, bounds.getCentreY() + 18.0f);
                    paint::lamp (g, c, i == lit ? ink : t.scr.paperShade, 4.0f);
                }
            }
            paint::text (g, text, bounds, HaloFonts::font (Role::ui), session.error().isNotEmpty() ? t.honest.danger : ink.withAlpha (0.55f),
                         juce::Justification::centred);
            return;
        }

        const int n = (int) source->size();
        const auto trim = session.prep.resolvedTrim (n);
        const auto p = plot();

        // The overview strip when zoomed.
        if (end - start < (std::int64_t) n)
        {
            auto ov = bounds.reduced (10.0f, 12.0f).removeFromTop (12.0f);
            const auto& w = session.summary();
            const float peak = juce::jmax (0.05f, w.peakMagnitude());
            g.setColour (t.scr.paperShade.darker (0.3f));
            for (int x = 0; x < (int) ov.getWidth(); x += 2)
            {
                const int b = juce::jlimit (0, w.bucketCount() - 1, (int) ((float) x / ov.getWidth() * (float) w.bucketCount()));
                const float a = juce::jmax (std::abs (w.minima[(size_t) b]), std::abs (w.maxima[(size_t) b])) / peak;
                g.fillRect (ov.getX() + (float) x, ov.getCentreY() - a * ov.getHeight() * 0.5f, 1.0f, juce::jmax (1.0f, a * ov.getHeight()));
            }
            g.setColour (ink);
            g.drawRect (juce::Rectangle<float>::leftTopRightBottom (ov.getX() + ov.getWidth() * (float) start / (float) n, ov.getY(),
                                                                    ov.getX() + ov.getWidth() * (float) end / (float) n, ov.getBottom()), 1.0f);
        }

        // Black mirrored lanes: L above, R below for stereo; one lane for mono.
        const int columns = juce::jmax (1, (int) p.getWidth());
        if (const auto* cols = cache.get (columns, start, end); cols != nullptr && cols->count > 0)
        {
            const bool stereo = false; // one lane: the mono mixdown (no per-channel columns yet, so no pretend L / R)
            const float peak = juce::jmax (0.05f, cols->peak);
            const float colW = p.getWidth() / (float) cols->count;
            juce::RectangleList<float> bars;
            const float laneGap = stereo ? 6.0f : 0.0f;
            const float laneH = stereo ? (p.getHeight() - laneGap) * 0.5f : p.getHeight();
            for (int lane = 0; lane < (stereo ? 2 : 1); ++lane)
            {
                const float mid = p.getY() + laneH * 0.5f + (float) lane * (laneH + laneGap);
                for (int c = 0; c < cols->count; ++c)
                {
                    const float a = juce::jmax (std::abs (cols->mins[(size_t) c]), std::abs (cols->maxs[(size_t) c])) / peak;
                    const float h = juce::jmax (0.6f, a * laneH * 0.46f);
                    bars.add (p.getX() + (float) c * colW, mid - h, juce::jmax (1.0f, colW * 0.8f), h * 2.0f);
                }
            }
            g.setColour (ink);
            g.fillRectList (bars);
            if (stereo)
            {
                paint::text (g, "l", { p.getX() + 4.0f, p.getY(), 10.0f, 12.0f }, HaloFonts::font (Role::print, 10.5f), ink.withAlpha (0.6f));
                paint::text (g, "r", { p.getX() + 4.0f, p.getY() + laneH + laneGap, 10.0f, 12.0f }, HaloFonts::font (Role::print, 10.5f), ink.withAlpha (0.6f));
            }
        }

        // Outside the trim: veiled in paperShade and hatched, over the wave,
        // so what's cut away reads as gone.
        for (auto [a, b] : { std::pair { (double) start, (double) trim.start }, std::pair { (double) trim.end, (double) end } })
        {
            const auto r = juce::Rectangle<float>::leftTopRightBottom (juce::jmax (p.getX(), frameToX (a)), p.getY(),
                                                                       juce::jmin (p.getRight(), frameToX (b)), p.getBottom());
            if (r.getWidth() > 0.0f)
            {
                g.setColour (t.scr.paperShade.withAlpha (0.78f));
                g.fillRect (r);
                paint::hatch (g, r, ink.withAlpha (0.22f));
            }
        }

        // Fade wedges (grey) inside the trim.
        auto wedge = [&] (double from, double to, bool fadeIn)
        {
            if (to <= from)
                return;
            juce::Path w;
            const float x0 = frameToX (from), x1 = frameToX (to);
            if (fadeIn)
                w.addTriangle (x0, p.getY(), x1, p.getY(), x0, p.getBottom());
            else
                w.addTriangle (x0, p.getY(), x1, p.getY(), x1, p.getBottom());
            g.setColour (t.encoder (2).line.withAlpha (0.35f));
            g.fillPath (w);
        };
        const double fadeInEnd = juce::jmin ((double) trim.end, (double) trim.start + session.prep.fadeInFrames);
        const double fadeOutStart = juce::jmax ((double) trim.start, (double) trim.end - session.prep.fadeOutFrames);
        wedge (trim.start, fadeInEnd, true);
        wedge (fadeOutStart, trim.end, false);

        // What SEND drops (AUTO-TRIM: only the first 20 s of the selection
        // goes to the EP-40): hatched, an orange edge with a foot tab and the
        // "send ends" chip, so the band says what the notice says.
        if (session.sendIsTrimmed())
        {
            const auto kept = session.sendPrep().resolvedTrim (n);
            const auto tail = juce::Rectangle<float>::leftTopRightBottom (juce::jmax (p.getX(), frameToX (kept.end)), p.getY(),
                                                                          juce::jmin (p.getRight(), frameToX (trim.end)), p.getBottom());
            if (tail.getWidth() > 0.0f)
            {
                const auto orange = t.honest.info;
                g.setColour (t.scr.paperShade.withAlpha (0.55f));
                g.fillRect (tail);
                paint::hatch (g, tail, ink.withAlpha (0.18f));
                g.setColour (orange);
                g.fillRect (juce::Rectangle<float> (tail.getX() - 0.75f, tail.getY(), 1.5f, tail.getHeight()));
                juce::Path foot;
                foot.addTriangle (tail.getX(), tail.getBottom(), tail.getX() + 9.0f, tail.getBottom(), tail.getX(), tail.getBottom() - 9.0f);
                g.fillPath (foot);
                const auto chipFont = HaloFonts::font (Role::mono, 10.5f);
                const juce::String chip = "send ends " + juce::String::charToString ((juce::juce_wchar) 0x00B7) + " "
                                        + juce::String (juce::roundToInt ((kept.end - kept.start) / audio->sampleRate)) + " s";
                const float cw = paint::textWidth (chipFont, chip) + 12.0f;
                if (tail.getWidth() > cw + 14.0f)
                {
                    const auto r = juce::Rectangle<float> (tail.getX() + 8.0f, tail.getBottom() - 30.0f, cw, 17.0f);
                    g.setColour (t.scr.paper);
                    g.fillRect (r);
                    g.setColour (orange);
                    g.drawRect (r, 1.0f);
                    paint::text (g, chip, r, chipFont, ink, juce::Justification::centred);
                }
            }
        }

        // The four markers: full height with a flag tab at the top and a
        // foot at the bottom (IN / OUT flags point into the kept part).
        auto marker = [&] (double frame, juce::Colour c, bool tabsRight, bool big)
        {
            const float x = frameToX (frame);
            if (x < p.getX() - 4.0f || x > p.getRight() + 4.0f)
                return;
            g.setColour (c);
            g.fillRect (juce::Rectangle<float> (x - 1.0f, bounds.getY() + 3.0f, 2.0f, bounds.getHeight() - 6.0f));
            const float tw = big ? 9.0f : 7.0f, th = big ? 11.0f : 8.0f;
            const float tx = tabsRight ? x - 1.0f : x + 1.0f - tw;
            g.fillRect (juce::Rectangle<float> (tx, bounds.getY() + 3.0f, tw, th));
            g.fillRect (juce::Rectangle<float> (tx, bounds.getBottom() - 3.0f - 5.0f, tw, 5.0f));
        };
        if (session.prep.fadeInFrames > 0)
            marker (fadeInEnd, t.encoder (1).line, true, false);
        if (session.prep.fadeOutFrames > 0)
            marker (fadeOutStart, t.encoder (2).line.darker (0.3f), false, false);
        marker (trim.start, t.encoder (0).line, true, true);
        marker (trim.end, t.encoder (3).line, false, true);

        // The playhead while auditioning the edit.
        auto& player = session.player();
        if (player.isPlaying() && player.token() == -1)
        {
            const double f = trim.start + (trim.end - trim.start) * player.progress();
            g.setColour (t.scr.hot);
            g.fillRect (juce::Rectangle<float> (frameToX (f) - 1.0f, bounds.getY(), 2.0f, bounds.getHeight()));
            g.setColour (ink);
            g.drawRect (juce::Rectangle<float> (frameToX (f) - 1.5f, bounds.getY(), 3.0f, bounds.getHeight()), 0.5f);
        }
    }

    void SampleBand::mouseMove (const juce::MouseEvent& e)
    {
        setMouseCursor (markerAt (e.position.x) != Marker::none ? juce::MouseCursor::LeftRightResizeCursor : juce::MouseCursor::NormalCursor);
    }

    void SampleBand::mouseDown (const juce::MouseEvent& e)
    {
        dragging = markerAt (e.position.x);
    }

    void SampleBand::mouseDrag (const juce::MouseEvent& e)
    {
        if (dragging == Marker::none || source == nullptr)
            return;
        const int n = (int) source->size();
        const int frame = (int) xToFrame (e.position.x);
        const auto t = session.prep.resolvedTrim (n);
        switch (dragging)
        {
            case Marker::in:
                session.prep.trimStartFrame = juce::jlimit (0, juce::jmax (0, t.end - 64), frame);
                break;
            case Marker::out:
            {
                const int endFrame = juce::jlimit (juce::jmin (n, t.start + 64), n, frame);
                session.prep.trimEndFrame = endFrame >= n ? std::optional<int>() : std::optional<int> (endFrame);
                break;
            }
            case Marker::fadeIn:
                session.prep.fadeInFrames = juce::jlimit (0, t.end - t.start, frame - t.start);
                break;
            case Marker::fadeOut:
                session.prep.fadeOutFrames = juce::jlimit (0, t.end - t.start, t.end - frame);
                break;
            case Marker::none:
            default:
                break;
        }
        session.changed();
    }

    void SampleBand::mouseUp (const juce::MouseEvent&)
    {
        dragging = Marker::none;
    }

    void SampleBand::mouseWheelMove (const juce::MouseEvent& e, const juce::MouseWheelDetails& w)
    {
        if (source == nullptr)
            return;
        if (! e.mods.isCtrlDown())
        {
            const auto m = markerAt (e.position.x);
            if (m != Marker::none && onMarkerWheel)
            {
                onMarkerWheel (m == Marker::in ? 0 : m == Marker::fadeIn ? 1 : m == Marker::fadeOut ? 2 : 3, e, w);
                return;
            }
        }
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
        const auto n = (std::int64_t) source->size();
        const auto span = end - start;
        if (e.mods.isCtrlDown())
        {
            const double anchor = xToFrame (e.position.x);
            const auto newSpan = (std::int64_t) juce::jlimit (1024.0, (double) n, (double) span * std::pow (0.8, (double) detents));
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
