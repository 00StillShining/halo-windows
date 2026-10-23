#include "UI/Pages/RackPage.h"
#include "App/DevPose.h"
#include "Core/EncoderMath.h"
#include "UI/Design/Numerals.h"
#include "UI/Design/Paint.h"
#include "UI/Design/Pictograms.h"
#include "UI/Design/Primitives/Gauges.h"

namespace halo
{
    namespace
    {
        const juce::String dot = juce::String::fromUTF8 (" \xc2\xb7 ");
        const juce::String minus = juce::String::charToString ((juce::juce_wchar) 0x2212);
        const juce::Colour black { 0xff000005 };
        constexpr EchoDivision divisionList[] { EchoDivision::eighth, EchoDivision::dottedEighth, EchoDivision::quarter, EchoDivision::tripletEighth };
        const char* moduleNames[] { "TAPE ECHO", "SPRING", "SWEEP", "LOW END" };
        const char* moduleWords[] { "echo", "spring", "sweep", "low" };

        // The defaults a double-click returns to (today's initial values).
        const RackParameters& defaults()
        {
            static const RackParameters d;
            return d;
        }

        enum class Kind { percent, sweep, lowDb, echoTime };
        struct Param
        {
            const char* label = nullptr;
            std::atomic<float>* value = nullptr;
            const std::atomic<float>* initial = nullptr;
            Kind kind = Kind::percent;
            explicit operator bool() const { return label != nullptr; }
        };

        juce::String divisionWord (EchoDivision d)
        {
            switch (d)
            {
                case EchoDivision::eighth:        return juce::String::fromUTF8 ("\xe2\x85\x9b");
                case EchoDivision::dottedEighth:  return juce::String::fromUTF8 ("\xe2\x85\x9b.");
                case EchoDivision::quarter:       return juce::String::fromUTF8 ("\xc2\xbc");
                case EchoDivision::tripletEighth: return juce::String::fromUTF8 ("\xe2\x85\x9bT");
            }
            return {};
        }

        double msToNorm (double ms) { return std::log (juce::jlimit (40.0, 1200.0, ms) / 40.0) / std::log (30.0); }
        double normToMs (double n) { return 40.0 * std::pow (30.0, juce::jlimit (0.0, 1.0, n)); }
    }

    // The four slots of each module (encoder order; empty = dead cell).
    static std::array<Param, 4> paramsFor (RackParameters& p, int module)
    {
        const auto& d = defaults();
        switch (module)
        {
            case RackPage::echo:
                return { Param { "TIME", &p.echoTimeMs, &d.echoTimeMs, Kind::echoTime }, Param { "FEEDBACK", &p.echoFeedback, &d.echoFeedback },
                         Param { "TONE", &p.echoTone, &d.echoTone }, Param { "MIX", &p.echoMix, &d.echoMix } };
            case RackPage::spring:
                return { Param { "MIX", &p.springMix, &d.springMix }, Param { "DECAY", &p.springDecay, &d.springDecay },
                         Param { "TONE", &p.springTone, &d.springTone }, Param {} };
            case RackPage::sweep:
                return { Param { "SWEEP", &p.sweepMacro, &d.sweepMacro, Kind::sweep }, Param { "RESO", &p.sweepReso, &d.sweepReso }, Param {}, Param {} };
            case RackPage::low:
            default:
                return { Param { "AMOUNT", &p.lowAmount, &d.lowAmount, Kind::lowDb }, Param {}, Param {}, Param {} };
        }
    }

    RackPage::RackPage (AppModel& m) : model (m), params (m.monitor().rackParameters())
    {
        setOpaque (false);
        setWantsKeyboardFocus (true);
        juce::StringArray words;
        for (auto d : divisionList)
            words.add (divisionWord (d));
        divisions.setItems (words);
        divisions.onSelect = [this] (int i) { model.rack().setDivision (divisionList[(size_t) i]); refresh(); };
        tapKey.onClick = [this] { model.rack().tap (AppModel::now()); refresh(); };
        tapKey.setTooltip ("Tap the echo's tempo (used when there's no MIDI clock)");
        for (auto* c : std::initializer_list<juce::Component*> { &divisions, &tapKey, &noticeView })
            addAndMakeVisible (c);
        model.addListener (this);
       #if JUCE_DEBUG
        // Shots: HALO_UI_POSE=module:1..4 focuses a module, sync:1 shows SYNC,
        // allon:1 switches every module on (the page's own state only).
        if (const int posed = DevPose::get ("module"); posed >= 1 && posed <= 4)
            focused = posed - 1;
        if (DevPose::get ("sync") == 1)
            model.rack().setSync (true);
        if (DevPose::get ("allon") == 1)
            for (int i = 0; i < 4; ++i)
                onFlag (i).store (true);
       #endif
        refresh();
        startTimerHz (2);
    }

    bool RackPage::live() const
    {
        return model.monitor().isRunning() && params.engaged.load();
    }

    RackPage::~RackPage()
    {
        model.removeListener (this);
    }

    juce::Rectangle<float> RackPage::localF (const layout::Rect& r) const
    {
        return { r.x - frame.body.x, r.y - frame.body.y, r.w, r.h };
    }

    void RackPage::setFrame (const layout::Frame& f)
    {
        frame = f;
        resized();
    }

    std::atomic<bool>& RackPage::onFlag (int module) const
    {
        switch (module)
        {
            case echo:   return params.echoOn;
            case spring: return params.springOn;
            case sweep:  return params.sweepOn;
            case low:
            default:     return params.lowOn;
        }
    }

    void RackPage::setFocus (int module)
    {
        module = juce::jlimit (0, 3, module);
        if (module == focused)
            return;
        focused = module;
        if (onFocusChanged)
            onFocusChanged();
        refresh();
    }

    void RackPage::toggleModule (int module)
    {
        auto& flag = onFlag (module);
        const bool on = ! flag.load();
        flag.store (on);
        // Switching a module on means you want to hear it: engage the rack too.
        if (on)
            params.engaged.store (true);
        refresh();
    }

    void RackPage::refresh()
    {
        const auto& rack = model.rack();
        for (int i = 0; i < 4; ++i)
            if (rack.division == divisionList[(size_t) i])
                divisions.setSelected (i);
        divisions.setVisible (rack.sync);
        const bool running = model.monitor().isRunning();
        const bool engaged = params.engaged.load();
        juce::String info;
        if (! running)
            info = "start MONITOR to hear the rack (ctrl m)";
        else if (! engaged)
            info = "BYPASSED: click ENGAGE to hear the rack";
        for (int mdl = 0; mdl < 3 && info.isEmpty(); ++mdl)
        {
            const float mix = mdl == echo ? params.echoMix.load() : mdl == spring ? params.springMix.load() : 1.0f;
            if (mdl != sweep && onFlag (mdl).load() && mix <= 0.0f)
                info = juce::String (moduleWords[mdl]) + " mix 0: this module is silent";
        }
        noticeView.set (info.isNotEmpty() ? NoticeData::info (info) : NoticeData::plain ("on the monitor route, before the -1 dBFS limiter"));
        resized();
        repaint();
    }

    void RackPage::timerCallback()
    {
        model.rack().updateClockBpm (model.clockBpm());
        const bool on = live();
        const double dt = 1.0 / (on ? 30.0 : 2.0);
        // Drive: the pre-rack peak, -48...0 dB -> 0...1, smoothed.
        const float db = model.preRackLevels().peakL;
        const float target = on && std::isfinite (db) ? juce::jlimit (0.0f, 1.0f, (db + 48.0f) / 48.0f) : 0.0f;
        drive += (target - drive) * (float) (1.0 - std::exp (-dt / 0.12));
        const bool coasting = echoReels.step (on && params.echoOn.load() ? 1.2 + 2.0 * drive : 0.0, dt);
        if (! motion::reduced())
        {
            ringPhase += dt * (0.25 + 1.2 * drive);
            springPhase += dt * (2.0 + 10.0 * drive);
            conePhase += dt * (4.0 + 8.0 * drive);
        }
        // 30 Hz while the rack is live (meters, machines) or reels coast; else a 2 Hz poll.
        const int hz = on || coasting ? 30 : 2;
        if (getTimerInterval() != 1000 / hz)
            startTimerHz (hz);
        repaint();
    }

    //==========================================================================
    juce::Rectangle<float> RackPage::headerTag (int index) const
    {
        const auto head = localF (L.header);
        const char* words[] { "ENGAGED", "PRINT FX", "MONITOR" };
        float x = head.getX() + 300.0f;
        for (int i = 0; i < index; ++i)
            x += paint::tagWidth (words[i]) + 10.0f;
        return { x, head.getCentreY() - 2.0f, paint::tagWidth (words[index]), 20.0f };
    }

    juce::Rectangle<float> RackPage::moduleWord (int module) const
    {
        const auto head = localF (L.header);
        const auto font = HaloFonts::face (HaloFonts::Face::interMedium, 14.0f);
        float right = head.getRight();
        for (int i = 3; i >= 0; --i)
        {
            const float w = paint::textWidth (font, moduleWords[i]) + 12.0f;
            right -= w;
            if (i == module)
                return { right, head.getY(), w, head.getHeight() };
            right -= 8.0f;
        }
        return {};
    }

    juce::Rectangle<float> RackPage::syncTag() const
    {
        const auto lower = localF (L.lower);
        return { lower.getX() + 92.0f, lower.getY() + 24.0f, paint::tagWidth ("SYNC"), 20.0f };
    }

    void RackPage::resized()
    {
        if (frame.width <= 0)
            return;
        L = layout::rackLayout (frame);
        auto lower = localF (L.lower);
        const auto extras = lower.removeFromLeft (juce::jmin (560.0f, lower.getWidth() * 0.48f));
        const auto sync = syncTag();
        tapKey.setBounds (juce::Rectangle<float> (sync.getRight() + 12.0f, sync.getCentreY() - 15.0f, (float) tapKey.preferredWidth(), 30.0f).toNearestInt());
        divisions.setBounds (juce::Rectangle<float> (extras.getX() + 92.0f, sync.getBottom() + 12.0f, 4.0f * 52.0f + 3.0f * 6.0f, 30.0f).toNearestInt());
        auto right = lower.withTrimmedLeft (24.0f).withTrimmedTop (22.0f);
        noticeView.setBounds (right.withHeight ((float) noticeView.heightForWidth ((int) right.getWidth())).toNearestInt());
    }

    void RackPage::mouseUp (const juce::MouseEvent& e)
    {
        if (! e.mouseWasClicked())
            return;
        const auto p = e.position;
        if (headerTag (0).contains (p))
        {
            params.engaged.store (! params.engaged.load());
            refresh();
        }
        else if (headerTag (1).contains (p) && ! model.monitor().isRecording())
        {
            model.monitor().setPrintFx (! model.monitor().isPrintingFx());
            refresh();
        }
        else if (headerTag (2).contains (p))
            model.toggleMonitor();
        else if (syncTag().contains (p))
        {
            model.rack().setSync (! model.rack().sync);
            refresh();
        }
        for (int i = 0; i < 4; ++i)
        {
            if (moduleWord (i).contains (p))
                setFocus (i);
            const auto tile = localF (L.tiles[(size_t) i]);
            if (tile.contains (p))
            {
                // The ON / BYPASS tag toggles; anywhere else focuses.
                if (juce::Rectangle<float> (tile.getRight() - 90.0f, tile.getY(), 90.0f, 34.0f).contains (p))
                    toggleModule (i);
                setFocus (i);
            }
        }
        grabKeyboardFocus();
    }

    bool RackPage::keyPressed (const juce::KeyPress& key)
    {
        if (! key.getModifiers().isAnyModifierKeyDown())
            for (int i = 0; i < 4; ++i)
                if (key.getKeyCode() == '1' + i)
                {
                    setFocus (i);
                    return true;
                }
        if (key == juce::KeyPress::leftKey)  { setFocus (focused - 1); return true; }
        if (key == juce::KeyPress::rightKey) { setFocus (focused + 1); return true; }
        if (key == juce::KeyPress::spaceKey) { toggleModule (focused); return true; }
        return false;
    }

    //==========================================================================
    void RackPage::paintLadders (juce::Graphics& g, juce::Rectangle<float> r, const StereoLevels& lv, const juce::String& tap)
    {
        const auto& t = theme();
        const bool running = model.monitor().isRunning();
        auto litFor = [] (float db) { return std::isfinite (db) ? juce::jlimit (0, 16, (int) std::floor ((db + 48.0f) / 48.0f * 16.0f)) : 0; };
        auto bars = r.withSizeKeepingCentre (16.0f, juce::jmin (150.0f, r.getHeight() - 60.0f));
        gauges::ladder (g, bars.withWidth (6.0f), 16, running ? litFor (lv.peakL) : 0, t.scr.ink, true);
        gauges::ladder (g, bars.withTrimmedLeft (10.0f), 16, running ? litFor (lv.peakR) : 0, t.scr.ink, true);
        paint::text (g, tap, { r.getCentreX() - 30.0f, bars.getBottom() + 6.0f, 60.0f, 12.0f }, HaloFonts::font (Role::print, 10.5f), t.scr.dim,
                     juce::Justification::centred);
    }

    void RackPage::paintIllustration (juce::Graphics& g, int module, juce::Rectangle<float> r, juce::Colour line, bool live)
    {
        g.setColour (line);
        const auto c = r.getCentre();
        const float s = juce::jmin (r.getWidth(), r.getHeight());
        switch (module)
        {
            case echo:
            {
                // Two reels and the rings travelling out of the head.
                const float rr = s * 0.2f;
                for (float dx : { -rr * 1.25f, rr * 1.25f })
                {
                    const auto rc = c.translated (dx, -rr * 0.35f);
                    g.drawEllipse (juce::Rectangle<float> (rr * 2.0f, rr * 2.0f).withCentre (rc), 1.6f);
                    g.drawEllipse (juce::Rectangle<float> (rr * 0.5f, rr * 0.5f).withCentre (rc), 1.4f);
                    for (int k = 0; k < 3; ++k)
                    {
                        const float a = (float) echoReels.angle + juce::MathConstants<float>::twoPi * (float) k / 3.0f;
                        g.drawLine ({ rc.getPointOnCircumference (rr * 0.25f, a), rc.getPointOnCircumference (rr * 0.85f, a) }, 1.4f);
                    }
                }
                const auto head = c.translated (0.0f, rr * 1.15f);
                g.fillRect (juce::Rectangle<float> (10.0f, 8.0f).withCentre (head));
                const float fb = params.echoFeedback.load();
                const int rings = 1 + (int) (fb * 4.0f);
                for (int k = 0; k < rings; ++k)
                {
                    const float prog = (float) std::fmod ((live ? ringPhase : 0.0) + (double) k / rings, 1.0);
                    const float rad = 8.0f + prog * s * 0.32f;
                    g.setColour (line.withAlpha (live && ! motion::reduced() ? (1.0f - prog) : 0.35f + 0.15f * (float) k));
                    juce::Path arc;
                    arc.addCentredArc (head.x, head.y, rad, rad * 0.55f, 0.0f, juce::MathConstants<float>::pi * 0.55f,
                                       juce::MathConstants<float>::pi * 1.45f, true);
                    g.strokePath (arc, juce::PathStrokeType (1.3f));
                }
                break;
            }
            case spring:
            {
                // A coil between two mounts.
                juce::Path coil;
                const float x0 = r.getX() + r.getWidth() * 0.12f, x1 = r.getRight() - r.getWidth() * 0.12f;
                const float amp = s * 0.14f * (0.6f + 0.4f * params.springDecay.load());
                const int turns = 9;
                coil.startNewSubPath (x0, c.y);
                for (int i = 0; i <= turns * 16; ++i)
                {
                    const float u = (float) i / (float) (turns * 16);
                    const float wob = live ? (0.04f + 0.18f * drive) * (float) std::sin (springPhase + u * 20.0) : 0.0f;
                    coil.lineTo (x0 + (x1 - x0) * u, c.y + amp * ((float) std::sin (u * juce::MathConstants<float>::twoPi * (float) turns) + wob));
                }
                g.strokePath (coil, juce::PathStrokeType (1.4f));
                g.fillRect (juce::Rectangle<float> (6.0f, amp * 3.0f).withCentre ({ x0 - 3.0f, c.y }));
                g.fillRect (juce::Rectangle<float> (6.0f, amp * 3.0f).withCentre ({ x1 + 3.0f, c.y }));
                break;
            }
            case sweep:
            {
                // The filter's response: LP / BP / HP by the macro, a peak by the reso.
                const float macro = params.sweepMacro.load(), reso = params.sweepReso.load();
                auto plot = r.reduced (r.getWidth() * 0.1f, r.getHeight() * 0.18f);
                juce::Path curve;
                for (int i = 0; i <= 60; ++i)
                {
                    const float u = (float) i / 60.0f;
                    const float fc = 0.15f + 0.7f * macro;
                    const float d = (u - fc) / 0.12f;
                    float y;
                    if (macro < 0.45f)      y = u < fc ? 0.0f : juce::jmin (1.0f, d * d * 0.5f);            // LP
                    else if (macro > 0.55f) y = u > fc ? 0.0f : juce::jmin (1.0f, d * d * 0.5f);            // HP
                    else                    y = juce::jmin (1.0f, d * d * 0.25f);                           // BP
                    y -= reso * 0.6f * std::exp (-d * d * 2.0f);
                    const auto pt = juce::Point<float> (plot.getX() + u * plot.getWidth(), plot.getY() + plot.getHeight() * 0.35f + y * plot.getHeight() * 0.6f);
                    if (i == 0) curve.startNewSubPath (pt); else curve.lineTo (pt);
                }
                g.strokePath (curve, juce::PathStrokeType (1.6f));
                const auto f = HaloFonts::font (Role::print, 10.5f);
                const char* names[] { "LP", "BP", "HP" };
                for (int i = 0; i < 3; ++i)
                {
                    const bool on = (i == 0 && macro < 0.45f) || (i == 2 && macro > 0.55f) || (i == 1 && macro >= 0.45f && macro <= 0.55f);
                    paint::text (g, names[i], { plot.getX() + plot.getWidth() * (float) i * 0.5f - 12.0f, plot.getBottom() + 4.0f, 24.0f, 12.0f }, f,
                                 line.withAlpha (on ? 1.0f : 0.4f), juce::Justification::centred);
                }
                break;
            }
            case low:
            default:
            {
                // A speaker cone; the surround breathes with the amount.
                const float rr = s * 0.3f;
                const float breathe = live ? 1.0f + (0.01f + 0.06f * drive) * params.lowAmount.load() * (float) std::sin (conePhase) : 1.0f;
                g.drawEllipse (juce::Rectangle<float> (rr * 2.0f, rr * 2.0f).withCentre (c), 1.6f);
                g.drawEllipse (juce::Rectangle<float> (rr * 1.6f * breathe, rr * 1.6f * breathe).withCentre (c), 1.3f);
                g.drawEllipse (juce::Rectangle<float> (rr * 0.6f, rr * 0.6f).withCentre (c), 1.4f);
                g.fillEllipse (juce::Rectangle<float> (rr * 0.3f, rr * 0.3f).withCentre (c));
                break;
            }
        }
    }

    void RackPage::paintTile (juce::Graphics& g, int module, juce::Rectangle<float> r)
    {
        const auto& t = theme();
        const bool on = onFlag (module).load();
        const bool live = on && params.engaged.load() && model.monitor().isRunning();
        if (on)
        {
            g.setColour (t.scr.paper);
            g.fillRoundedRectangle (r, 4.0f);
        }
        else
        {
            g.setColour (t.scr.panel);
            g.fillRoundedRectangle (r, 4.0f);
            paint::hatch (g, r.reduced (1.0f), t.scr.hatch.withAlpha (0.35f));
            g.setColour (t.scr.frame);
            g.drawRoundedRectangle (r.reduced (0.5f), 4.0f, 1.0f);
        }
        if (module == focused)
        {
            g.setColour (t.scr.hot);
            g.drawRoundedRectangle (r.expanded (3.0f), 6.0f, 2.5f);
        }
        const auto ink = on ? black : t.scr.dim;
        auto inner = r.reduced (12.0f, 10.0f);

        // Title and the ON / BYPASS tag.
        auto top = inner.removeFromTop (20.0f);
        paint::text (g, moduleNames[module], top, HaloFonts::font (Role::label), ink);
        const juce::String tagText = on ? "ON" : "BYPASS";
        const float tw = paint::tagWidth (tagText);
        paint::tag (g, { top.getRight() - tw, top.getY() }, tagText, on ? paint::TagStyle::filled : paint::TagStyle::outline,
                    on ? black : t.scr.frame, on ? t.scr.paper : t.scr.dim, 20.0f);

        // The corner values: label, black Numerals XS, the encoder's shade bar.
        const auto slots = paramsFor (params, module);
        const float cornerH = 40.0f;
        auto bottom = inner.removeFromBottom (cornerH);
        auto upper = inner.removeFromTop (cornerH);
        inner.removeFromTop (4.0f);
        inner.removeFromBottom (4.0f);
        const auto& rack = model.rack();
        auto corner = [&] (int slot, juce::Rectangle<float> area, bool rightSide)
        {
            const auto& p = slots[(size_t) slot];
            const auto& e = t.encoder (slot);
            // Encoder-shade dot in the corner, also on dead slots (they stay dim).
            if (! p)
                return;
            juce::String value, unit;
            switch (p.kind)
            {
                case Kind::echoTime:
                    value = juce::String (juce::roundToInt (rack.effectiveMs()));
                    unit = rack.sync ? "ms " + divisionWord (rack.division) : "ms";
                    break;
                case Kind::sweep:
                {
                    const float v = p.value->load();
                    value = v < 0.45f ? "LP" : v > 0.55f ? "HP" : "BP";
                    break;
                }
                case Kind::lowDb:
                    value = "+" + juce::String (p.value->load() * 6.0f, 1);
                    unit = "db";
                    break;
                case Kind::percent:
                default:
                    value = juce::String (juce::roundToInt (p.value->load() * 100.0f));
                    unit = "%";
                    break;
            }
            const auto labelFont = HaloFonts::font (Role::print, 10.5f);
            const float cap = numerals::XS;
            auto labelRow = area.removeFromTop (13.0f);
            paint::text (g, p.label, labelRow, labelFont, ink.withAlpha (on ? 0.62f : 1.0f),
                         rightSide ? juce::Justification::centredRight : juce::Justification::centredLeft);
            const float baseline = area.getY() + 6.0f + cap;
            bool figures = true;
            for (auto ch : value)
                figures = figures && numerals::hasGlyph (ch);
            const auto unitFont = numerals::unitFont (cap);
            const float valueW = figures ? numerals::width (value, cap) : paint::textWidth (HaloFonts::font (Role::uiLight, cap * 1.2f), value);
            const float unitW = unit.isNotEmpty() ? paint::textWidth (unitFont, unit) + 3.0f : 0.0f;
            const float total = valueW + unitW;
            const float barX = rightSide ? area.getRight() - 3.0f : area.getX();
            const float x = rightSide ? barX - 6.0f - total : barX + 3.0f + 6.0f;
            g.setColour (on ? e.skirt : t.scr.frame);
            g.fillRect (juce::Rectangle<float> (barX, baseline - cap, 3.0f, cap));
            if (figures)
                numerals::draw (g, value, x, baseline, cap, ink);
            else
                paint::textAt (g, value, x, baseline, HaloFonts::font (Role::uiLight, cap * 1.2f), ink);
            if (unit.isNotEmpty())
                paint::textAt (g, unit, x + valueW + 3.0f, baseline, unitFont, ink.withAlpha (0.7f));
        };
        const float half = upper.getWidth() * 0.5f;
        corner (0, upper.withWidth (half), false);
        corner (1, upper.withTrimmedLeft (half), true);
        corner (2, bottom.withWidth (half), false);
        corner (3, bottom.withTrimmedLeft (half), true);

        // Encoder-shade dots in the four corners of the tile.
        for (int slot = 0; slot < 4; ++slot)
        {
            const auto pt = juce::Point<float> (slot % 2 == 0 ? r.getX() + 5.0f : r.getRight() - 5.0f, slot < 2 ? r.getY() + 5.0f : r.getBottom() - 5.0f);
            if (slots[(size_t) slot])
                paint::lamp (g, pt, on ? t.encoder (slot).skirt : t.scr.frame, 4.0f);
        }

        paintIllustration (g, module, inner, on ? black : t.scr.dim, live);
        if (! on)
            paint::text (g, "bypassed", inner.removeFromBottom (14.0f), HaloFonts::font (Role::ui, 11.5f), t.scr.dim, juce::Justification::centred);
    }

    void RackPage::paint (juce::Graphics& g)
    {
        if (frame.width <= 0)
            return;
        const auto& t = theme();
        const bool running = model.monitor().isRunning();
        const bool engaged = params.engaged.load();
        const bool printing = model.monitor().isPrintingFx();

        // ---- Header ----
        {
            auto head = localF (L.header);
            auto icon = head.removeFromLeft (head.getHeight() - 6.0f).withSizeKeepingCentre (34.0f, 34.0f);
            g.setColour (t.scr.ink.withAlpha (0.85f));
            g.drawRoundedRectangle (icon.reduced (0.6f), 3.0f, 1.2f);
            pictograms::draw (g, Pict::rack, icon.reduced (8.0f), t.scr.ink, 1.4f);
            head.removeFromLeft (14.0f);
            paint::text (g, "rack", head.withHeight (14.0f), HaloFonts::font (Role::ui, 11.5f), t.scr.dim);
            paint::text (g, "DUB", head.withTrimmedTop (14.0f), HaloFonts::font (Role::title), t.scr.ink);

            const auto e = headerTag (0);
            paint::tag (g, e.getTopLeft(), engaged ? "ENGAGED" : "BYPASSED", engaged ? paint::TagStyle::filled : paint::TagStyle::outline,
                        engaged ? t.scr.ink : t.scr.frame, engaged ? black : t.scr.dim);
            const auto p = headerTag (1);
            paint::tag (g, p.getTopLeft(), "PRINT FX", printing ? paint::TagStyle::filled : paint::TagStyle::outline,
                        printing ? t.scr.ink : t.scr.frame, printing ? black : t.scr.dim);
            const auto mo = headerTag (2);
            paint::tag (g, mo.getTopLeft(), "MONITOR", running ? paint::TagStyle::filled : paint::TagStyle::outline,
                        running ? t.scr.ink : t.scr.frame, running ? black : t.scr.dim);
            if (model.monitor().isRecording())
                paint::text (g, "print fx: can't change while recording", { mo.getRight() + 14.0f, mo.getY(), 280.0f, 20.0f },
                             HaloFonts::font (Role::ui, 11.5f), t.scr.dim);

            const auto words = HaloFonts::face (HaloFonts::Face::interMedium, 14.0f);
            for (int i = 0; i < 4; ++i)
            {
                const auto r = moduleWord (i);
                const bool on = i == focused;
                paint::text (g, moduleWords[i], r, words, on ? t.scr.hot : t.scr.dim, juce::Justification::centred);
                if (on)
                {
                    g.setColour (t.scr.hot);
                    g.fillRect (r.withTrimmedTop (r.getHeight() - 6.0f).withHeight (2.0f).reduced (2.0f, 0.0f));
                }
            }
        }

        // ---- The chain ----
        {
            const auto canvas = localF (L.canvas);
            const auto print = HaloFonts::font (Role::print, 10.5f);
            const float midY = localF (L.tiles[0]).getCentreY();
            auto arrow = [&] (float x0, float x1)
            {
                if (x1 - x0 < 6.0f)
                    return;
                paint::hairlineH (g, x0, x1 - 4.0f, midY, t.scr.frame);
                juce::Path head;
                head.addTriangle (x1, midY, x1 - 6.0f, midY - 3.5f, x1 - 6.0f, midY + 3.5f);
                g.setColour (t.scr.frame);
                g.fillPath (head);
            };
            // in port
            const auto in = localF (L.inPort);
            g.setColour (t.scr.ink);
            g.drawEllipse (juce::Rectangle<float> (14.0f, 14.0f).withCentre ({ in.getCentreX(), midY }), 1.5f);
            paint::text (g, "in", { in.getX(), midY + 10.0f, in.getWidth(), 12.0f }, print, t.scr.dim, juce::Justification::centred);
            paintLadders (g, localF (L.preLadders), model.preRackLevels(), "pre-rack");
            arrow (in.getCentreX() + 8.0f, localF (L.preLadders).getX() - 2.0f);
            arrow (localF (L.preLadders).getRight() + 4.0f, localF (L.tiles[0]).getX() - 4.0f);
            for (int i = 0; i < 4; ++i)
            {
                paintTile (g, i, localF (L.tiles[(size_t) i]));
                if (i < 3)
                    arrow (localF (L.tiles[(size_t) i]).getRight() + 5.0f, localF (L.tiles[(size_t) i + 1]).getX() - 5.0f);
            }
            // limiter block
            const auto lim = localF (L.limiter);
            const auto box = lim.withSizeKeepingCentre (lim.getWidth(), 64.0f).withCentre ({ lim.getCentreX(), midY });
            arrow (localF (L.tiles[3]).getRight() + 5.0f, box.getX() - 3.0f);
            g.setColour (t.scr.frame);
            g.drawRoundedRectangle (box, 4.0f, 1.2f);
            paint::text (g, "LIMITER", box.withTrimmedTop (10.0f).withHeight (14.0f), HaloFonts::font (Role::label), t.scr.ink, juce::Justification::centred);
            paint::text (g, minus + "1 dbfs", box.withTrimmedTop (30.0f).withHeight (16.0f), HaloFonts::font (Role::mono, 11.0f), t.scr.dim,
                         juce::Justification::centred);
            arrow (box.getRight() + 3.0f, localF (L.outLadders).getX() - 2.0f);
            paintLadders (g, localF (L.outLadders), model.meterLevels(), "out");
            const auto out = localF (L.outPort);
            arrow (localF (L.outLadders).getRight() + 4.0f, out.getCentreX() - 9.0f);
            g.setColour (t.scr.ink);
            g.fillEllipse (juce::Rectangle<float> (14.0f, 14.0f).withCentre ({ out.getCentreX(), midY }));
            paint::text (g, "out", { out.getX(), midY + 10.0f, out.getWidth(), 12.0f }, print, t.scr.dim, juce::Justification::centred);
            juce::ignoreUnused (canvas);
        }

        // ---- Lower band: the echo's time source ----
        {
            auto lower = localF (L.lower);
            paint::hairlineH (g, lower.getX(), lower.getRight(), lower.getY() - 4.0f, t.scr.rule);
            const auto& rack = model.rack();
            paint::text (g, "ECHO TIME", lower.withHeight (16.0f).withTrimmedTop (2.0f), HaloFonts::font (Role::label), t.scr.dim);
            const auto sync = syncTag();
            paint::text (g, "mode", sync.withX (lower.getX()).withWidth (80.0f), HaloFonts::font (Role::ui, 12.0f), t.scr.dim);
            paint::tag (g, sync.getTopLeft(), rack.sync ? "SYNC" : "FREE", rack.sync ? paint::TagStyle::filled : paint::TagStyle::outline,
                        rack.sync ? t.scr.ink : t.scr.frame, rack.sync ? black : t.scr.ink);
            const auto bpm = rack.tempoBpm();
            const juce::String source = juce::String (rack.tempoSource()).toLowerCase() + (bpm ? " " + juce::String (juce::roundToInt (*bpm)) + " bpm" : juce::String());
            paint::text (g, source, { (float) tapKey.getRight() + 14.0f, sync.getY(), 220.0f, sync.getHeight() }, HaloFonts::font (Role::mono, 11.5f),
                         bpm ? t.scr.ink : t.scr.dim);
            if (rack.sync)
            {
                paint::text (g, "division", juce::Rectangle<float> (lower.getX(), (float) divisions.getY(), 80.0f, 30.0f),
                             HaloFonts::font (Role::ui, 12.0f), t.scr.dim);
                paint::text (g, "time " + juce::String (juce::roundToInt (rack.effectiveMs())) + " ms" + (bpm ? juce::String() : juce::String (" (no tempo: free time)")),
                             { (float) divisions.getRight() + 14.0f, (float) divisions.getY(), 260.0f, 30.0f }, HaloFonts::font (Role::mono, 11.5f), t.scr.dim);
            }
            else
                paint::text (g, "free time " + juce::String (juce::roundToInt (rack.freeMs)) + " ms" + dot + "sync follows the clock, then tap",
                             juce::Rectangle<float> (lower.getX(), sync.getBottom() + 12.0f, 520.0f, 30.0f), HaloFonts::font (Role::ui, 12.0f), t.scr.dim);
        }
    }

    void RackPage::paintLane (juce::Graphics& g, juce::Rectangle<float> r, bool) const
    {
        const auto& t = theme();
        auto rows = r.reduced (0.0f, 4.0f);
        paint::text (g, "KEYS", rows.removeFromTop (14.0f), HaloFonts::font (Role::label), t.scr.dim);
        rows.removeFromTop (6.0f);
        const auto ui = HaloFonts::font (Role::ui, 11.5f);
        paint::text (g, "1" + juce::String::fromUTF8 ("\xe2\x80\x93") + "4 or click a tile: focus" + dot + "space: on / bypass", rows.removeFromTop (16.0f), ui, t.scr.ink);
        paint::text (g, "the encoders follow the focused module" + dot + "double-click resets", rows.removeFromTop (16.0f), ui, t.scr.dim);
    }

    //==========================================================================
    std::array<std::optional<EncoderBinding>, 4> RackPage::bindings()
    {
        std::array<std::optional<EncoderBinding>, 4> out;
        const auto slots = paramsFor (params, focused);
        const int module = focused;
        auto& rack = model.rack();
        for (int i = 0; i < 4; ++i)
        {
            const auto p = slots[(size_t) i];
            if (! p)
                continue;
            EncoderBinding e;
            e.label = p.label;
            if (p.kind == Kind::echoTime)
            {
                // FREE: 40-1200 ms on a log scale. SYNC: the four divisions.
                e.getNorm = [&rack] { return rack.sync ? 0.0 : msToNorm (rack.freeMs); };
                e.setNorm = [this, &rack] (double v) { if (! rack.sync) rack.setFreeMs (std::round (normToMs (v))); repaint(); };
                e.turn = [this, &rack] (int d, encoder::Speed sp)
                {
                    if (rack.sync)
                    {
                        int at = 0;
                        for (int k = 0; k < 4; ++k)
                            if (rack.division == divisionList[(size_t) k]) at = k;
                        rack.setDivision (divisionList[(size_t) juce::jlimit (0, 3, at + d)]);
                    }
                    else
                    {
                        const double step = sp == encoder::Speed::fine ? 0.002 : sp == encoder::Speed::coarse ? 0.05 : 0.01;
                        rack.setFreeMs (std::round (normToMs (msToNorm (rack.freeMs) + d * step)));
                    }
                    refresh();
                };
                e.reset = [this, &rack] { rack.setFreeMs (360.0); refresh(); };
                e.format = [&rack] { return rack.sync ? divisionWord (rack.division) : juce::String (juce::roundToInt (rack.freeMs)); };
                e.unit = [&rack] { return rack.sync ? juce::String() : juce::String ("ms"); };
                e.caption = [&rack] { return rack.sync ? juce::String (juce::roundToInt (rack.effectiveMs())) + " ms (sync)" : juce::String ("free"); };
                e.gauge = [&rack] (juce::Graphics& g, juce::Rectangle<float> r, juce::Colour c, bool)
                {
                    gauges::ruler (g, r, msToNorm (rack.effectiveMs()), c, -1.0, "40", "1200");
                };
                e.pressLabel = "tap";
                e.press = [this, &rack] { rack.tap (AppModel::now()); refresh(); };
            }
            else
            {
                auto* value = p.value;
                const float initial = p.initial->load();
                const auto kind = p.kind;
                e.getNorm = [value] { return (double) value->load(); };
                e.setNorm = [this, value] (double v) { value->store ((float) juce::jlimit (0.0, 1.0, v)); repaint(); };
                e.step = 0.01;
                e.fine = 0.002;
                e.coarse = 0.1;
                e.reset = [this, value, initial] { value->store (initial); repaint(); };
                e.format = [value, kind]
                {
                    const float v = value->load();
                    if (kind == Kind::sweep) return juce::String (v < 0.45f ? "LP" : v > 0.55f ? "HP" : "BP");
                    if (kind == Kind::lowDb) return "+" + juce::String (v * 6.0f, 1);
                    return juce::String (juce::roundToInt (v * 100.0f));
                };
                e.unit = [kind] { return kind == Kind::lowDb ? juce::String ("db") : kind == Kind::sweep ? juce::String() : juce::String ("%"); };
                e.caption = [this, module]
                {
                    return onFlag (module).load() ? juce::String (moduleWords[module]) : juce::String (moduleWords[module]) + ": bypassed";
                };
                e.gauge = [value] (juce::Graphics& g, juce::Rectangle<float> r, juce::Colour c, bool)
                {
                    gauges::tickDial (g, r, value->load(), c);
                };
            }
            // Presses: echo = tap, sync, on/off, engage; the others = on/off, engage.
            if (module == echo)
            {
                if (i == 1) { e.pressLabel = "sync"; e.press = [this, &rack] { rack.setSync (! rack.sync); refresh(); }; }
                if (i == 2) { e.pressLabel = "on/off"; e.press = [this] { toggleModule (echo); }; }
                if (i == 3) { e.pressLabel = "engage"; e.press = [this] { params.engaged.store (! params.engaged.load()); refresh(); }; }
                if (i == 2)
                {
                    // TONE's shift layer: WOW.
                    e.shiftLabel = "WOW";
                    e.shiftTurn = [this] (int d) { params.echoWow.store (juce::jlimit (0.0f, 1.0f, params.echoWow.load() + 0.01f * (float) d)); repaint(); };
                }
            }
            else
            {
                if (i == 0) { e.pressLabel = "on/off"; e.press = [this, module] { toggleModule (module); }; }
                if (i == 1) { e.pressLabel = "engage"; e.press = [this] { params.engaged.store (! params.engaged.load()); refresh(); }; }
            }
            e.hint = [] { return juce::String ("wheel 1%" + dot + "alt 0.2%" + dot + "ctrl 10%"); };
            out[(size_t) i] = e;
        }
        return out;
    }
}
