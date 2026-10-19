#include "UI/Design/Primitives/StatusStrip.h"
#include "Core/HonestState.h"
#include "UI/Design/Numerals.h"
#include "UI/Design/Paint.h"
#include "UI/Design/Pictograms.h"

namespace halo
{
    namespace
    {
        juce::String utf8 (const char* s) { return juce::String::fromUTF8 (s); }
        const juce::String dot = utf8 (" \xc2\xb7 ");
        const juce::String enDash = juce::String::charToString ((juce::juce_wchar) 0x2013);
        const juce::String minus = juce::String::charToString ((juce::juce_wchar) 0x2212);
        const juce::String emDash = juce::String::charToString ((juce::juce_wchar) 0x2014);

        std::string str (const juce::String& s) { return s.toStdString(); }
        juce::String fromStd (const std::string& s) { return juce::String::fromUTF8 (s.c_str()); }

        float ringLevel (const RingState& r)
        {
            switch (r.kind)
            {
                case RingState::Kind::disconnected: return 0.0f;
                case RingState::Kind::discovering:  return 0.1f;
                case RingState::Kind::connected:    return 0.2f;
                case RingState::Kind::monitoring:   return 0.4f;
                case RingState::Kind::recording:    return 1.0f;
                case RingState::Kind::transfer:     return (float) r.transferProgress;
                case RingState::Kind::error:        return 1.0f;
            }
            return 0.0f;
        }

        honest::Inputs honestInputs (AppModel& m)
        {
            honest::Inputs in;
            in.source = m.isMock() ? honest::Source::mock : honest::Source::device;
            const auto feed = m.display().feedMode;
            in.feed = feed == FeedMode::live ? honest::Feed::live : feed == FeedMode::waiting ? honest::Feed::waiting : honest::Feed::preview;
            in.connected = m.isConnected();
            in.portBusy = m.deviceStatus() == "IN USE";
            in.libraryReady = m.device().state() == Ep40Library::State::ready;
            in.writesUnlocked = m.writesUnlocked();
            in.busy = m.deviceWriter().isBusy();
            return in;
        }
    }

    StatusStrip::StatusStrip (AppModel& m) : model (m)
    {
        setOpaque (false); // the glass (with its rounded corners) is the window's
        model.addListener (this);
        startTimer (500); // 2 Hz: cached state only
    }

    StatusStrip::~StatusStrip()
    {
        model.removeListener (this);
    }

    void StatusStrip::displayChanged (const DisplayState& s)
    {
        if (s.feedMode != lastFeed)
        {
            lastFeed = s.feedMode;
            repaint();
        }
    }

    void StatusStrip::timerCallback()
    {
        blink = ! blink;
        repaint();
    }

    layout::StripContent StatusStrip::content() const
    {
        layout::StripContent c;
        const auto in = honestInputs (model);
        const auto honestFlags = honest::flags (in);
        c.mock = honestFlags.mock;
        c.preview = honestFlags.preview;

        const auto& lib = model.device();
        if (model.isMock())
        {
            c.deviceFull = c.deviceShort = "synthetic";
        }
        else if (in.portBusy)
        {
            c.deviceFull = c.deviceShort = "in use by another app";
        }
        else if (! model.isConnected())
        {
            c.deviceFull = c.deviceShort = "no device";
        }
        else
        {
            c.deviceShort = "connected";
            juce::String full = "connected";
            if (lib.info().os.isNotEmpty())
                full << dot << "os " << lib.info().os;
            if (lib.state() == Ep40Library::State::ready)
                full << dot << (int) lib.sounds().size() << " sounds";
            c.deviceFull = str (full);
        }

        const auto feed = model.display().feedMode;
        c.feedWord = feed == FeedMode::live ? "live" : feed == FeedMode::waiting ? "wait" : "preview";
        c.ringWord = str (juce::String (model.ring().statusWord()).toLowerCase());

        auto& engine = model.monitor();
        juce::String out = engine.isRunning() ? engine.route().outputName
                         : engine.chosenOutputName().isNotEmpty() ? engine.chosenOutputName()
                                                                  : "windows default";
        out = out.toLowerCase();
        if (out.length() > 22)
            out = out.substring (0, 11).trimEnd() + juce::String::charToString ((juce::juce_wchar) 0x2026) + out.substring (out.length() - 10).trimStart();
        c.outName = str (out);

        const double db = model.monitorGainDb(); // the one getter (H7)
        c.monText = engine.isRunning()
                  ? str ((db < 0.0 ? minus : juce::String()) + juce::String (std::abs (juce::roundToInt (db))) + " db")
                  : std::string ("off");
        if (engine.isRecording())
        {
            const int s = (int) engine.recordingSeconds();
            c.recText = str (juce::String (s / 60).paddedLeft ('0', 2) + ":" + juce::String (s % 60).paddedLeft ('0', 2));
        }
        else
        {
            c.recText = str (emDash + ":" + emDash);
        }
        c.writesWord = honest::writeView (in).word;
        return c;
    }

    void StatusStrip::paint (juce::Graphics& g)
    {
        const auto& t = theme();
        paint::hairlineH (g, 0.0f, (float) getWidth(), (float) getHeight() - 1.0f, t.scr.rule);

        const auto c = content();
        const auto L = layout::layoutStrip ({ 0.0f, 0.0f, (float) getWidth(), (float) getHeight() }, c);
        const float cy = (float) getHeight() * 0.5f;
        const auto labelFont = HaloFonts::font (Role::label);
        const auto valueFont = HaloFonts::font (Role::uiStatus);
        auto rect = [] (const layout::Rect& r) { return juce::Rectangle<float> (r.x, r.y, r.w, r.h); };
        auto label = [&] (float& x, const juce::String& s)
        {
            paint::text (g, s, { x, 0.0f, 200.0f, (float) getHeight() }, labelFont, t.scr.dim);
            x += layout::StripMetrics::labelWidth (str (s)) + layout::StripMetrics::gapSmall;
        };
        auto value = [&] (float& x, const juce::String& s, juce::Colour colour)
        {
            paint::text (g, s, { x, 0.0f, 400.0f, (float) getHeight() }, valueFont, colour);
            x += layout::StripMetrics::valueWidth (str (s)) + layout::StripMetrics::gapSmall;
        };
        const float lampD = layout::StripMetrics::lamp;
        auto lampAt = [&] (float& x, juce::Colour colour, bool hollow)
        {
            if (hollow) paint::hollowLamp (g, { x + lampD * 0.5f, cy }, colour, lampD);
            else        paint::lamp (g, { x + lampD * 0.5f, cy }, colour, lampD);
            x += lampD + layout::StripMetrics::gapSmall;
        };

        // 1. Wordmark: the ring glyph (arc = ring level; crimson for rec/err) and "halo".
        {
            const auto& ring = model.ring();
            const auto r = rect (L.wordmark.r);
            const auto centre = juce::Point<float> (r.getX() + 9.0f, cy);
            g.setColour (t.scr.ink.withAlpha (0.35f));
            g.drawEllipse (juce::Rectangle<float> (18.0f, 18.0f).withCentre (centre), 1.5f);
            const bool red = ring.kind == RingState::Kind::recording || ring.isError();
            const float level = ringLevel (ring);
            if (level > 0.0f)
            {
                juce::Path arc;
                arc.addCentredArc (centre.x, centre.y, 9.0f, 9.0f, 0.0f, 0.0f, juce::MathConstants<float>::twoPi * level, true);
                g.setColour (red ? t.honest.danger : t.scr.ink);
                g.strokePath (arc, juce::PathStrokeType (2.2f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
            }
            paint::text (g, "halo", r.withTrimmedLeft (26.0f), HaloFonts::font (Role::title), t.scr.ink);
        }

        // 2. The honest slot: always the same place, always the same width.
        {
            const auto r = rect (L.honest.r);
            float x = r.getX();
            const float top = cy - 10.0f;
            if (c.mock)
                x = paint::tag (g, { x, top }, "MOCK", paint::TagStyle::mock).getRight() + 8.0f;
            if (c.preview)
                paint::tag (g, { x, top }, "PREVIEW", paint::TagStyle::preview);
        }

        // 3. Device.
        {
            float x = L.device.r.x;
            const bool busyPort = c.deviceShort == "in use by another app";
            const auto lampColour = model.isMock() ? t.honest.mock : busyPort ? t.honest.danger : model.isConnected() ? t.scr.hot : t.scr.dim;
            lampAt (x, lampColour, ! model.isConnected() && ! model.isMock());
            label (x, "EP" + enDash + "40");
            if (L.device.form == layout::SlotForm::full)
                value (x, fromStd (c.deviceFull), model.isConnected() || model.isMock() ? t.scr.ink : t.scr.dim);
            else if (L.device.form == layout::SlotForm::shortForm)
                value (x, fromStd (c.deviceShort), model.isConnected() || model.isMock() ? t.scr.ink : t.scr.dim);
        }

        // 4. Feed.
        {
            float x = L.feed.r.x;
            const auto feed = model.display().feedMode;
            if (L.feed.form == layout::SlotForm::full)
                label (x, "FEED");
            if (feed == FeedMode::live)          lampAt (x, t.scr.hot, false);
            else if (feed == FeedMode::waiting)  { if (blink) lampAt (x, t.scr.dim, true); else x += lampD + layout::StripMetrics::gapSmall; }
            else                                 lampAt (x, t.honest.preview, true);
            if (L.feed.form != layout::SlotForm::minimal)
                value (x, fromStd (c.feedWord), feed == FeedMode::preview ? t.scr.ink : t.scr.ink);
        }

        // 5. Ring word (err is a filled danger tag).
        if (L.ring.form != layout::SlotForm::hidden)
        {
            float x = L.ring.r.x;
            label (x, "RING");
            if (model.ring().isError())
                paint::tag (g, { x, cy - 10.0f }, "ERR", paint::TagStyle::danger);
            else
                value (x, fromStd (c.ringWord), t.scr.ink);
        }

        // 6. Out (first to hide).
        if (L.out.form != layout::SlotForm::hidden)
        {
            float x = L.out.r.x;
            label (x, "OUT");
            value (x, fromStd (c.outName), model.monitor().isRunning() ? t.scr.ink : t.scr.dim);
        }

        // 7. Mon: the same getter as encoder 1 (a blue dot when it owns it).
        {
            float x = L.mon.r.x;
            label (x, "MON");
            value (x, fromStd (c.monText), model.monitor().isRunning() ? t.scr.ink : t.scr.dim);
            if (monOwned)
                paint::lamp (g, { L.mon.r.right() - 4.0f, cy }, t.encoder (0).line, 6.0f);
        }

        // 8. Rec: idle is a hollow dim ring; recording pulses crimson at 1 Hz.
        {
            float x = L.rec.r.x;
            const bool recording = model.monitor().isRecording();
            if (recording)
            {
                if (blink) lampAt (x, t.honest.danger, false);
                else       { paint::lamp (g, { x + lampD * 0.5f, cy }, t.honest.danger.withAlpha (0.35f), lampD); x += lampD + layout::StripMetrics::gapSmall; }
            }
            else
            {
                lampAt (x, t.scr.dim, true);
            }
            label (x, "REC");
            value (x, fromStd (c.recText), recording ? t.scr.ink : t.scr.dim);
        }

        // 9. Writes: padlock and its word; "locked" / "unlocked" / "n/a" never drops.
        {
            float x = L.writes.r.x;
            const bool unlocked = c.writesWord == "unlocked";
            pictograms::draw (g, unlocked ? Pict::lockOpen : Pict::lockClosed, { x, cy - 7.0f, 14.0f, 14.0f }, t.scr.ink, 1.4f);
            x += 14.0f + layout::StripMetrics::gapSmall;
            if (L.writes.form == layout::SlotForm::full)
                label (x, "WRITES");
            value (x, fromStd (c.writesWord), t.scr.ink);
        }
    }
}
