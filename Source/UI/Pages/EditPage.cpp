#include "UI/Pages/EditPage.h"
#include "Audio/SampleIO.h"
#include "Core/HonestState.h"
#include "UI/Design/Motion.h"
#include "UI/Design/Numerals.h"
#include "UI/Design/Paint.h"
#include "UI/Design/Pictograms.h"
#include "UI/Design/Primitives/Gauges.h"
#include "UI/Design/Primitives/Progress.h"

namespace halo
{
    namespace
    {
        const juce::String dot = juce::String::fromUTF8 (" \xc2\xb7 ");
        const juce::String enDash = juce::String::charToString ((juce::juce_wchar) 0x2013);
        const juce::String minus = juce::String::charToString ((juce::juce_wchar) 0x2212);
        const juce::String plusMinus = juce::String::charToString ((juce::juce_wchar) 0x00B1);

        constexpr Treatment treatments[] { Treatment::original, Treatment::high, Treatment::balanced,
                                           Treatment::loFi22k, Treatment::loFi11k };
        constexpr SamplePrep::Channels channelModes[] { SamplePrep::Channels::preserve, SamplePrep::Channels::mono,
                                                        SamplePrep::Channels::stereo };
        const char* channelWords[] { "keep", "mono", "stereo" };
        const char* qualityWords[] { "original", "high", "balanced", "lo 22k", "lo 11k" };

        juce::String secondsText (double s) { return juce::String (s, 3); }

        void label (juce::Graphics& g, juce::Rectangle<float> r, const juce::String& text, const juce::String& right = {})
        {
            paint::text (g, text, r, HaloFonts::font (Role::label), theme().scr.dim);
            if (right.isNotEmpty())
                paint::text (g, right, r, HaloFonts::font (Role::ui, 11.5f), theme().scr.dim, juce::Justification::centredRight);
        }
    }

    EditPage::EditPage (AppModel& m, WriteGate& g) : model (m), gate (g), session (m.editor())
    {
        setOpaque (false);
        setWantsKeyboardFocus (true);
        for (auto* c : std::initializer_list<juce::Component*> { &band, &channels, &quality, &noticeView, &auditionKey, &exportKey,
                                                                 &chopKey, &sendKey, &showKey, &libraryKey })
            addAndMakeVisible (c);
        channels.setItems ({ "keep", "mono", "stereo" });
        channels.onSelect = [this] (int i) { session.prep.channelMode = channelModes[(size_t) i]; session.changed(); };
        quality.setItems ({ "original", "high", "balanced", "lo 22k", "lo 11k" });
        quality.onSelect = [this] (int i) { session.treatment = treatments[(size_t) i]; session.changed(); };

        auditionKey.onClick = [this] { session.audition(); };
        auditionKey.setTooltip ("Hear exactly what EXPORT would write (space)");
        exportKey.onClick = [this] { session.exportEdit(); };
        exportKey.setTooltip ("Export as an EP-40-ready 16-bit WAV (ctrl e)");
        chopKey.onClick = [this] { session.openChop(); };
        sendKey.onClick = [this] { send(); };
        showKey.onClick = [this]
        {
            const auto f = session.lastExport();
            (f.exists() ? f : SampleIO::exportFolder()).revealToUser();
        };
        libraryKey.onClick = [this] { model.setMode (AppMode::capture); };

        session.addChangeListener (this);
        model.device().addChangeListener (this);
        model.backup().addChangeListener (this);
        model.addListener (this);
        refresh();
    }

    EditPage::~EditPage()
    {
        session.removeChangeListener (this);
        model.device().removeChangeListener (this);
        model.backup().removeChangeListener (this);
        model.removeListener (this);
    }

    juce::Rectangle<float> EditPage::localF (const layout::Rect& r) const
    {
        return { r.x - frame.body.x, r.y - frame.body.y, r.w, r.h };
    }

    void EditPage::setFrame (const layout::Frame& f)
    {
        frame = f;
        resized();
    }

    juce::String EditPage::sendBlocker() const
    {
        if (! session.hasSource())
            return {};
        // Over 20 s isn't a blocker: SEND auto-trims (see sendPrep()). Today's copy.
        const auto e = session.sendEstimate();
        const auto& info = model.device().info();
        if (model.device().state() == Ep40Library::State::ready && info.freeBytes >= 0 && e.payloadBytes > info.freeBytes)
            return "Needs " + juce::String (formatBytes (e.payloadBytes)) + " but the EP-40 has " + juce::String (formatBytes (info.freeBytes)) + " free.";
        return {};
    }

    NoticeData EditPage::notice() const
    {
        if (! session.hasSource())
            return {};
        const auto info = session.sendIsTrimmed()
            ? "AUTO-TRIM: over " + juce::String ((int) maxSoundSeconds) + " s, so SEND sends the first " + juce::String ((int) maxSoundSeconds)
                  + " s of the selection, with a fade-out. Drag the trim handles to pick another part, or CHOP it."
            : juce::String();
        const auto plain = model.writesUnlocked() ? juce::String ("SEND TO EP-40 uploads into the first empty USER slot (900-999). It never overwrites a sound.")
                         : model.device().state() == Ep40Library::State::ready ? juce::String ("Back up the EP-40 in BACKUPS (Ctrl+6) to unlock SEND. Until then, EXPORT works.")
                                                                               : juce::String ("Connect the EP-40 to send directly. Until then, EXPORT works.");
        return NoticeData::from (honest::pickNotice (sendBlocker().toStdString(), info.toStdString(), plain.toStdString()));
    }

    void EditPage::send()
    {
        if (! model.writesUnlocked() || ! session.hasSource() || sendBlocker().isNotEmpty())
            return;
        const auto e = session.sendEstimate();
        const int slot = model.device().firstFreeSlot (Ep40Writer::userFirstSlot, Ep40Writer::userLastSlot);
        WriteSpec spec;
        spec.kind = WriteSpec::Kind::send;
        spec.confirmLabel = "send";
        spec.body = "Upload \"" + session.item().title + "\" (" + juce::String (formatBytes (e.payloadBytes))
                    + ") to empty slot " + juce::String (slot) + " in USER?" + juce::newLine + juce::newLine
                    + (session.sendIsTrimmed() ? "It's auto-trimmed to the first " + juce::String ((int) maxSoundSeconds) + " s of your selection." + juce::newLine
                                               : juce::String())
                    + "Takes " + Ep40Writer::expectedTime (e.payloadBytes) + ". Nothing on the EP-40 is overwritten.";
        spec.from = session.item().title;
        spec.to = "slot " + juce::String (slot);
        spec.estimateSeconds = (double) e.payloadBytes / Ep40Writer::typicalBytesPerSecond;
        spec.estimateText = Ep40Writer::expectedTime (e.payloadBytes);
        gate.request (spec, [this] (GatedWriter writer)
        {
            sending = true;
            sendFraction = 0.0;
            sendEta.clear();
            startTimerHz (30);
            session.report ("Sending to the EP-40...", EditSession::Page::edit);
            session.renderSend ([this, writer] (std::shared_ptr<const AudioData> audio)
            {
                auto lastMs = std::make_shared<double> (0.0);
                writer.upload (*audio, session.item().title, 0, [this] (Ep40Writer::Result r)
                {
                    sending = false;
                    stopTimer();
                    session.report (r.ok ? r.message + ". Assign it to a pad in LOAD." : "Send failed: " + r.message, EditSession::Page::edit);
                },
                [this, lastMs] (juce::int64 sent, juce::int64 total)
                {
                    sendFraction = (double) sent / (double) juce::jmax<juce::int64> (1, total);
                    sendEta = Ep40Writer::expectedTime (total - sent) + " left";
                    const double now = juce::Time::getMillisecondCounterHiRes();
                    if (now - *lastMs < 250.0)
                        return;
                    *lastMs = now;
                    // Today's progress text, verbatim.
                    session.report ("Sending to the EP-40: " + juce::String ((int) (100 * sent / juce::jmax<juce::int64> (1, total))) + "%, "
                                    + Ep40Writer::expectedTime (total - sent) + " left", EditSession::Page::edit);
                });
            });
        });
    }

    void EditPage::timerCallback()
    {
        // The reels turn while bytes move (the EP-40 takes about 43 KB/s).
        if (! motion::reduced())
            reelAngle += 0.06;
        repaint (localF (L.right).toNearestInt());
    }

    void EditPage::refresh()
    {
        const bool has = session.hasSource();
        for (int i = 0; i < 3; ++i)
            if (session.prep.channelMode == channelModes[(size_t) i])
                channels.setSelected (i);
        for (int i = 0; i < 5; ++i)
            if (session.treatment == treatments[(size_t) i])
                quality.setSelected (i);
        channels.setRowEnabled (has);
        quality.setRowEnabled (has);
        for (auto* k : { &auditionKey, &exportKey, &chopKey })
            k->setEnabled (has);
        const bool auditioning = session.player().isPlaying() && session.player().token() == -1;
        auditionKey.setLabel (auditioning ? "stop" : "audition");
        auditionKey.setIcon (auditioning ? Pict::stop : Pict::play);
        showKey.setVisible (session.lastExport().exists());
        libraryKey.setVisible (! has && ! session.isLoading());
        if (model.deviceWriter().isBusy())
            sendKey.setVariant (ActionKey::Variant::busy);
        else if (! model.writesUnlocked())
            sendKey.setVariant (ActionKey::Variant::lockedWrite);
        else
        {
            sendKey.setVariant (ActionKey::Variant::deviceWrite);
            sendKey.setEnabled (has && sendBlocker().isEmpty());
        }
        noticeView.set (notice());
        resized();
        repaint();
    }

    juce::Rectangle<float> EditPage::pageWord (bool shape) const
    {
        const auto head = localF (L.header);
        const auto font = HaloFonts::face (HaloFonts::Face::interMedium, 14.0f);
        const float shapeW = paint::textWidth (font, "shape") + 8.0f, trimW = paint::textWidth (font, "trim") + 8.0f;
        const float right = head.getRight() - (libraryKey.isVisible() ? (float) libraryKey.preferredWidth() + 20.0f : 0.0f);
        return shape ? juce::Rectangle<float> (right - shapeW, head.getY(), shapeW, head.getHeight())
                     : juce::Rectangle<float> (right - shapeW - 12.0f - trimW, head.getY(), trimW, head.getHeight());
    }

    juce::Rectangle<float> EditPage::normalizeTag() const
    {
        const auto ch = channels.getBounds().toFloat();
        return { ch.getRight() + 16.0f, ch.getCentreY() - 10.0f, paint::tagWidth ("NORMALIZE"), 20.0f };
    }

    void EditPage::resized()
    {
        if (frame.width <= 0)
            return;
        L = layout::editLayout (frame);
        band.setBounds (localF (L.band).toNearestInt());
        const bool regular = frame.density == layout::Density::regular;

        const auto head = localF (L.header);
        libraryKey.setBounds (juce::Rectangle<float> ((float) libraryKey.preferredWidth(), 34.0f)
                                  .withPosition (head.getRight() - (float) libraryKey.preferredWidth(), head.getCentreY() - 17.0f).toNearestInt());

        auto left = localF (L.left);
        if (regular)
            left.removeFromTop (18.0f);
        channels.setBounds (left.removeFromTop (30.0f).withWidth (juce::jmin (300.0f, left.getWidth() * 0.55f)).toNearestInt());
        left.removeFromTop (regular ? 10.0f + 18.0f : 6.0f);
        quality.setBounds (left.removeFromTop (30.0f).toNearestInt());

        auto right = localF (L.right);
        noticeView.setBounds (right.removeFromTop ((float) noticeView.heightForWidth ((int) right.getWidth())).toNearestInt());
        auto status = right.removeFromBottom (22.0f);
        showKey.setBounds (status.removeFromRight ((float) showKey.preferredWidth()).withSizeKeepingCentre ((float) showKey.preferredWidth(), 22.0f).toNearestInt());
        auto actions = right.removeFromBottom (regular ? 44.0f : 36.0f);
        float x = actions.getX();
        for (auto* k : { &auditionKey, &exportKey, &chopKey })
        {
            k->setBounds (juce::Rectangle<float> (x, actions.getY(), (float) k->preferredWidth(), actions.getHeight()).toNearestInt());
            x += (float) k->preferredWidth() + 10.0f;
        }
        sendKey.setBounds (actions.removeFromRight ((float) sendKey.preferredWidth()).toNearestInt());
        const bool showActions = ! sending;
        for (auto* k : { &auditionKey, &exportKey, &chopKey, &sendKey })
            k->setVisible (showActions);
    }

    void EditPage::mouseUp (const juce::MouseEvent& e)
    {
        if (! e.mouseWasClicked())
            return;
        for (bool shape : { false, true })
            if (pageWord (shape).contains (e.position) && shapeLatched != shape)
            {
                shapeLatched = shape;
                if (onPageChanged)
                    onPageChanged();
                repaint();
            }
        if (session.hasSource() && normalizeTag().contains (e.position))
        {
            session.prep.normalize = ! session.prep.normalize;
            session.changed();
        }
        grabKeyboardFocus();
    }

    bool EditPage::keyPressed (const juce::KeyPress& key)
    {
        const auto mods = key.getModifiers();
        if (mods.isCtrlDown() && key.getKeyCode() == 'E')                       { session.exportEdit(); return true; }
        if (mods.isCtrlDown() && key.getKeyCode() == juce::KeyPress::returnKey) { send(); return true; }
        if (! mods.isAnyModifierKeyDown() && key.getKeyCode() == juce::KeyPress::spaceKey) { session.audition(); return true; }
        return false;
    }

    //==========================================================================
    void EditPage::paint (juce::Graphics& g)
    {
        if (frame.width <= 0)
            return;
        const auto& t = theme();
        const auto audio = session.audio();
        const bool regular = frame.density == layout::Density::regular;

        // Header.
        {
            auto head = localF (L.header);
            auto icon = head.removeFromLeft (head.getHeight() - 6.0f).withSizeKeepingCentre (34.0f, 34.0f);
            g.setColour (t.scr.ink.withAlpha (0.85f));
            g.drawRoundedRectangle (icon.reduced (0.6f), 3.0f, 1.2f);
            pictograms::draw (g, Pict::edit, icon.reduced (8.0f), t.scr.ink, 1.4f);
            head.removeFromLeft (14.0f);
            paint::text (g, "edit", head.withHeight (14.0f), HaloFonts::font (Role::ui, 11.5f), t.scr.dim);
            auto titleRow = head.withTrimmedTop (14.0f);
            const bool has = session.hasSource() || session.isLoading();
            const auto titleFont = HaloFonts::font (Role::title);
            const auto title = has ? session.item().title.toUpperCase() : juce::String ("NOTHING OPEN");
            const float titleW = juce::jmin (titleRow.getWidth() * 0.4f, paint::textWidth (titleFont, title) + 4.0f);
            paint::text (g, paint::middleEllipsis (title, titleFont, titleW), titleRow.withWidth (titleW), titleFont, t.scr.ink);
            juce::String meta = has ? juce::String() : juce::String ("pick a take, grab or file");
            if (audio != nullptr)
                meta = juce::String (audio->sampleRate / 1000.0, 1) + " khz" + dot + "16-bit" + dot + (audio->channelCount() == 1 ? "mono" : "stereo")
                     + dot + secondsText (audio->durationSeconds()) + " s";
            paint::text (g, meta, titleRow.withTrimmedLeft (titleW + 20.0f).withTrimmedRight (360.0f), HaloFonts::font (Role::ui, 12.0f), t.scr.dim);
            const auto words = HaloFonts::face (HaloFonts::Face::interMedium, 14.0f);
            for (bool shape : { false, true })
            {
                const auto r = pageWord (shape);
                const bool on = shape == shapeLatched;
                paint::text (g, shape ? "shape" : "trim", r, words, on ? t.scr.hot : t.scr.dim, juce::Justification::centred);
                if (on)
                    g.fillRect (r.withTrimmedTop (r.getHeight() - 6.0f).withHeight (2.0f).reduced (2.0f, 0.0f));
            }
        }

        // Trim readout: each value in its marker's colour, on the glass.
        if (audio != nullptr)
        {
            auto row = localF (L.trimRow);
            const auto tr = session.prep.resolvedTrim (audio->frameCount());
            const double rate = audio->sampleRate;
            const auto font = HaloFonts::face (HaloFonts::Face::jetBrainsMono, 11.5f);
            float x = row.getX();
            pictograms::draw (g, Pict::play, { x, row.getCentreY() - 5.0f, 10.0f, 10.0f }, t.scr.ink, 1.3f);
            x += 18.0f;
            auto put = [&] (const juce::String& s, juce::Colour c)
            {
                paint::text (g, s, { x, row.getY(), 400.0f, row.getHeight() }, font, c);
                x += paint::textWidth (font, s);
            };
            put ("in ", t.scr.dim);
            put (secondsText (tr.start / rate) + " s", t.encoder (0).line);
            put ("  " + enDash + "  out ", t.scr.dim);
            put (secondsText (tr.end / rate) + " s", t.encoder (3).line);
            put ("  (" + secondsText ((tr.end - tr.start) / rate) + " s)", t.scr.ink);
            put ("   fade in " + juce::String (juce::roundToInt (session.prep.fadeInFrames / rate * 1000.0)) + " ms", t.encoder (1).line);
            put ("   fade out " + juce::String (juce::roundToInt (session.prep.fadeOutFrames / rate * 1000.0)) + " ms", t.encoder (2).line);
        }

        // Lower left: channels + normalize, quality, size, memory.
        {
            auto left = localF (L.left);
            paint::hairlineH (g, left.getX(), localF (L.right).getRight(), left.getY() - 5.0f, t.scr.rule);
            if (regular)
                label (g, left.removeFromTop (18.0f), "CHANNELS", "shared with chop");
            left.removeFromTop (30.0f);
            const bool norm = session.prep.normalize;
            paint::tag (g, normalizeTag().getTopLeft(), "NORMALIZE", norm ? paint::TagStyle::filled : paint::TagStyle::outline,
                        norm ? t.scr.ink : t.scr.frame, norm ? juce::Colour (0xff000005) : t.scr.dim);
            if (regular)
            {
                left.removeFromTop (10.0f);
                label (g, left.removeFromTop (18.0f), "EP" + enDash + "40 QUALITY");
            }
            else
                left.removeFromTop (6.0f);
            left.removeFromTop (30.0f + (regular ? 14.0f : 8.0f));
            if (audio != nullptr)
            {
                const auto e = session.estimate();
                const float cap = regular ? numerals::M : numerals::S;
                const float right = numerals::drawWithUnit (g, juce::String ((double) e.payloadBytes / (1024.0 * 1024.0), 2), "mb", left.getX(),
                                                            left.getY() + cap, cap, t.scr.ink, t.scr.dim);
                paint::text (g, juce::String (e.sampleRate / 1000.0, 1) + " khz 16-bit " + (e.channels == 1 ? "mono" : "stereo") + " "
                                    + secondsText (e.durationSeconds()) + " s",
                             { right + 18.0f, left.getY() + cap - 13.0f, left.getRight() - right - 18.0f, 14.0f }, HaloFonts::font (Role::mono, 11.0f), t.scr.dim);
                left.removeFromTop (cap + 12.0f);
                const double share = (double) e.payloadBytes / deviceMemoryBytes;
                paint::dotTimeline (g, left.removeFromTop (10.0f).withWidth (juce::jmin (left.getWidth(), 420.0f)), 61, 0,
                                    juce::jmax (1, juce::roundToInt (share * 61.0)));
                left.removeFromTop (4.0f);
                juce::String caption = juce::String (share * 100.0, share < 0.01 ? 2 : 1) + "% of ~122 mb (exact bytes, before the device's file overhead)";
                if (session.sendIsTrimmed())
                    caption << ". export writes all of it; send sends " << juce::String ((double) session.sendEstimate().payloadBytes / (1024.0 * 1024.0), 2)
                            << " mb (the first " << juce::String ((int) maxSoundSeconds) << " s)";
                paint::paragraph (g, caption, left, HaloFonts::font (Role::ui, 11.5f), t.scr.dim, 1.0f);
            }
        }

        // Lower right: the notice is a child; the backup line, progress, status.
        {
            auto right = localF (L.right);
            right.removeFromTop ((float) noticeView.getHeight());
            auto status = right.removeFromBottom (22.0f);
            auto actions = right.removeFromBottom (regular ? 44.0f : 36.0f);
            auto backup = right.removeFromBottom (24.0f).withTrimmedBottom (6.0f);
            if (sending)
            {
                auto pr = actions.withTop (juce::jmax (localF (L.right).getY() + (float) noticeView.getHeight() + 4.0f, actions.getY() - 40.0f));
                progress::paint (g, pr, { "SENDING", sendFraction, sendEta, reelAngle });
            }
            const auto b = model.latestBackup();
            pictograms::draw (g, b && model.writesUnlocked() ? Pict::lockOpen : Pict::lockClosed, backup.removeFromLeft (16.0f).withSizeKeepingCentre (14.0f, 14.0f), t.scr.ink, 1.4f);
            backup.removeFromLeft (6.0f);
            if (b)
            {
                paint::lamp (g, { backup.getX() + 3.5f, backup.getCentreY() }, t.scr.hot, 7.0f);
                paint::text (g, Ep40Backup::describe (*b), backup.withTrimmedLeft (12.0f), HaloFonts::font (Role::bodyStrong, 12.0f), t.scr.ink);
            }
            else
            {
                paint::hollowLamp (g, { backup.getX() + 3.5f, backup.getCentreY() }, t.scr.dim, 7.0f);
                // The notice above says what to do; this line only states the backup.
                paint::text (g, "no complete backup", backup.withTrimmedLeft (12.0f), HaloFonts::font (Role::ui, 12.0f), t.scr.dim);
            }

            // EDIT's own status line (CHOP's never shows here).
            const auto line = session.status (EditSession::Page::edit);
            const bool failed = line.startsWith ("Export failed") || line.startsWith ("Send failed");
            status = status.withTrimmedRight (showKey.isVisible() ? (float) showKey.getWidth() + 8.0f : 0.0f);
            if (failed)
            {
                g.setColour (t.honest.danger);
                g.drawRoundedRectangle (status.reduced (0.5f), 3.0f, 1.0f);
                pictograms::draw (g, Pict::triangle, { status.getX() + 5.0f, status.getCentreY() - 6.0f, 12.0f, 12.0f }, t.honest.danger, 1.3f);
                paint::text (g, paint::middleEllipsis (line, HaloFonts::font (Role::ui, 12.0f), status.getWidth() - 28.0f), status.withTrimmedLeft (22.0f),
                             HaloFonts::font (Role::ui, 12.0f), t.scr.ink);
            }
            else
            {
                paint::text (g, paint::middleEllipsis (line, HaloFonts::font (Role::mono, 11.0f), status.getWidth()), status,
                             HaloFonts::font (Role::mono, 11.0f), t.scr.ink);
            }
        }
    }

    void EditPage::paintLane (juce::Graphics& g, juce::Rectangle<float> r, bool) const
    {
        const auto& t = theme();
        auto rows = r.reduced (0.0f, 4.0f);
        paint::text (g, "KEYS", rows.removeFromTop (14.0f), HaloFonts::font (Role::label), t.scr.dim);
        rows.removeFromTop (6.0f);
        const auto ui = HaloFonts::font (Role::ui, 11.5f);
        juce::String head = "space audition";
        if (session.player().isPlaying() && session.player().token() == -1 && session.audio() != nullptr)
            head = "playing" + dot + head;
        paint::text (g, head + dot + "ctrl e export" + dot + "ctrl enter send", rows.removeFromTop (16.0f), ui, t.scr.ink);
        paint::text (g, "drag a marker to trim" + dot + "wheel on a marker nudges it" + dot + "ctrl+wheel zoom", rows.removeFromTop (16.0f), ui, t.scr.dim);
    }

    //==========================================================================
    std::array<std::optional<EncoderBinding>, 4> EditPage::bindings (EncoderBus& bus)
    {
        auto& s = session;
        // One parameter, as both pages need it.
        struct Param
        {
            juce::String label, pressLabel;
            std::function<double()> norm;
            std::function<void (int detents, encoder::Speed)> turn;
            std::function<void (double)> setNorm;
            std::function<juce::String()> format, unit, caption;
            std::function<void()> press, reset;
            std::function<void (juce::Graphics&, juce::Rectangle<float>, juce::Colour)> gauge;
        };
        auto rate = [&s] { return s.audio() != nullptr ? s.audio()->sampleRate : 44100.0; };
        auto frames = [&s] { return s.audio() != nullptr ? s.audio()->frameCount() : 0; };
        auto stepFrames = [rate] (encoder::Speed sp) { return (int) std::lround ((sp == encoder::Speed::fine ? 0.001 : sp == encoder::Speed::coarse ? 0.1 : 0.01) * rate()); };

        std::array<Param, 4> trim, shape;
        // ---- trim page
        trim[0] = { "IN", "audition",
                    [&s, frames] { const int n = frames(); return n > 0 ? (double) s.prep.resolvedTrim (n).start / n : 0.0; },
                    [&s, frames, stepFrames] (int d, encoder::Speed sp)
                    {
                        const int n = frames(); const auto t = s.prep.resolvedTrim (n);
                        s.prep.trimStartFrame = juce::jlimit (0, juce::jmax (0, t.end - 64), t.start + d * stepFrames (sp));
                        s.changed();
                    },
                    [&s, frames] (double v) { const int n = frames(); const auto t = s.prep.resolvedTrim (n); s.prep.trimStartFrame = juce::jlimit (0, juce::jmax (0, t.end - 64), (int) (v * n)); s.changed(); },
                    [&s, frames, rate] { return juce::String (s.prep.resolvedTrim (frames()).start / rate(), 3); },
                    [] { return juce::String ("s"); },
                    [] { return juce::String ("wheel 10 ms" + dot + "alt 1 ms"); },
                    [&s] { s.audition(); },
                    [&s] { s.prep.trimStartFrame = 0; s.changed(); },
                    nullptr };
        auto fadeParam = [&s, rate] (bool in) -> Param
        {
            auto& framesRef = in ? s.prep.fadeInFrames : s.prep.fadeOutFrames;
            Param p;
            p.label = in ? "FADE IN" : "FADE OUT";
            p.pressLabel = in ? "zoom start" : "zoom end";
            p.norm = [&framesRef, rate] { return juce::jlimit (0.0, 1.0, framesRef / rate() * 1000.0 / 2000.0); };
            p.turn = [&s, &framesRef, rate] (int d, encoder::Speed sp)
            {
                const double stepMs = sp == encoder::Speed::fine ? 1.0 : sp == encoder::Speed::coarse ? 100.0 : 10.0;
                const double ms = juce::jlimit (0.0, 2000.0, framesRef / rate() * 1000.0 + d * stepMs);
                framesRef = (int) std::lround (ms / 1000.0 * rate());
                s.changed();
            };
            p.setNorm = [&s, &framesRef, rate] (double v) { framesRef = (int) std::lround (v * 2.0 * rate()); s.changed(); };
            p.format = [&framesRef, rate] { return juce::String (juce::roundToInt (framesRef / rate() * 1000.0)); };
            p.unit = [] { return juce::String ("ms"); };
            p.caption = [] { return juce::String ("0" + enDash + "2000 ms"); };
            p.reset = [&s, &framesRef] { framesRef = 0; s.changed(); };
            p.gauge = [p] (juce::Graphics& g, juce::Rectangle<float> r, juce::Colour c) { gauges::ruler (g, r, p.norm(), c); };
            return p;
        };
        trim[1] = fadeParam (true);
        trim[1].press = [this] { band.zoomTo (true); };
        trim[2] = fadeParam (false);
        trim[2].press = [this] { band.zoomTo (false); };
        trim[3] = { "OUT", "reset trim",
                    [&s, frames] { const int n = frames(); return n > 0 ? (double) s.prep.resolvedTrim (n).end / n : 1.0; },
                    [&s, frames, stepFrames] (int d, encoder::Speed sp)
                    {
                        const int n = frames(); const auto t = s.prep.resolvedTrim (n);
                        const int e = juce::jlimit (juce::jmin (n, t.start + 64), n, t.end + d * stepFrames (sp));
                        s.prep.trimEndFrame = e >= n ? std::optional<int>() : std::optional<int> (e);
                        s.changed();
                    },
                    [&s, frames] (double v)
                    {
                        const int n = frames(); const auto t = s.prep.resolvedTrim (n);
                        const int e = juce::jlimit (juce::jmin (n, t.start + 64), n, (int) (v * n));
                        s.prep.trimEndFrame = e >= n ? std::optional<int>() : std::optional<int> (e);
                        s.changed();
                    },
                    [&s, frames, rate] { return juce::String (s.prep.resolvedTrim (frames()).end / rate(), 3); },
                    [] { return juce::String ("s"); },
                    [&s, frames, rate] { const auto t = s.prep.resolvedTrim (frames()); return juce::String ((t.end - t.start) / rate(), 3) + " s long"; },
                    [&s] { s.prep.trimStartFrame = 0; s.prep.trimEndFrame.reset(); s.changed(); },
                    [&s] { s.prep.trimEndFrame.reset(); s.changed(); },
                    nullptr };
        trim[0].gauge = [n = trim[0].norm] (juce::Graphics& g, juce::Rectangle<float> r, juce::Colour c) { gauges::timeline (g, r, n(), 8, c); };
        trim[3].gauge = [n = trim[3].norm] (juce::Graphics& g, juce::Rectangle<float> r, juce::Colour c) { gauges::timeline (g, r, n(), 8, c); };

        // ---- shape page (the shift layer)
        shape[0] = { "GAIN", "reset",
                     [&s] { return (s.prep.gainDecibels + 24.0) / 36.0; },
                     [&s] (int d, encoder::Speed sp)
                     {
                         const double step = sp == encoder::Speed::fine ? 0.1 : sp == encoder::Speed::coarse ? 3.0 : 0.5;
                         s.prep.gainDecibels = (float) juce::jlimit (-24.0, 12.0, (double) s.prep.gainDecibels + d * step);
                         s.changed();
                     },
                     [&s] (double v) { s.prep.gainDecibels = (float) (std::round ((v * 36.0 - 24.0) * 2.0) / 2.0); s.changed(); },
                     [&s] { const double v = s.prep.gainDecibels; return (v > 0.0 ? "+" : v < 0.0 ? minus : juce::String()) + juce::String (std::abs (v), 1); },
                     [] { return juce::String ("db"); },
                     [] { return minus + "24" + enDash + "+12 db"; },
                     [&s] { s.prep.gainDecibels = 0.0f; s.changed(); },
                     [&s] { s.prep.gainDecibels = 0.0f; s.changed(); },
                     nullptr };
        shape[0].gauge = [n = shape[0].norm] (juce::Graphics& g, juce::Rectangle<float> r, juce::Colour c) { gauges::tickDial (g, r, n(), c); };
        auto discrete = [&s] (int count, std::function<int()> get, std::function<void (int)> set) -> std::function<void (int, encoder::Speed)>
        {
            return [count, get, set, &s] (int d, encoder::Speed) { set (juce::jlimit (0, count - 1, get() + d)); s.changed(); };
        };
        auto channelIndex = [&s] { for (int i = 0; i < 3; ++i) if (s.prep.channelMode == channelModes[(size_t) i]) return i; return 0; };
        auto qualityIndex = [&s] { for (int i = 0; i < 5; ++i) if (s.treatment == treatments[(size_t) i]) return i; return 0; };
        shape[1] = { "CHANNELS", "cycle",
                     [channelIndex] { return channelIndex() / 2.0; },
                     discrete (3, channelIndex, [&s] (int i) { s.prep.channelMode = channelModes[(size_t) i]; }),
                     [&s] (double v) { s.prep.channelMode = channelModes[(size_t) juce::jlimit (0, 2, (int) std::lround (v * 2.0))]; s.changed(); },
                     [channelIndex] { return juce::String (channelWords[channelIndex()]); },
                     [] { return juce::String(); },
                     [] { return juce::String ("keep" + dot + "mono" + dot + "stereo"); },
                     [&s, channelIndex] { s.prep.channelMode = channelModes[(size_t) ((channelIndex() + 1) % 3)]; s.changed(); },
                     [&s] { s.prep.channelMode = SamplePrep::Channels::preserve; s.changed(); },
                     nullptr };
        shape[2] = { "QUALITY", "cycle",
                     [qualityIndex] { return qualityIndex() / 4.0; },
                     discrete (5, qualityIndex, [&s] (int i) { s.treatment = treatments[(size_t) i]; }),
                     [&s] (double v) { s.treatment = treatments[(size_t) juce::jlimit (0, 4, (int) std::lround (v * 4.0))]; s.changed(); },
                     [qualityIndex] { return juce::String (qualityWords[qualityIndex()]); },
                     [] { return juce::String(); },
                     [&s] { return juce::String (resolvedSampleRate (s.treatment, s.audio() != nullptr ? s.audio()->sampleRate : 0.0) / 1000.0, 1) + " khz"; },
                     [&s, qualityIndex] { s.treatment = treatments[(size_t) ((qualityIndex() + 1) % 5)]; s.changed(); },
                     [&s] { s.treatment = Treatment::original; s.changed(); },
                     nullptr };
        shape[3] = { "NORMALIZE", "toggle",
                     [&s] { return s.prep.normalize ? 1.0 : 0.0; },
                     [&s] (int d, encoder::Speed) { s.prep.normalize = d > 0; s.changed(); },
                     [&s] (double v) { s.prep.normalize = v >= 0.5; s.changed(); },
                     [&s] { return juce::String (s.prep.normalize ? "on" : "off"); },
                     [] { return juce::String(); },
                     [] { return juce::String ("peak to 0 db"); },
                     [&s] { s.prep.normalize = ! s.prep.normalize; s.changed(); },
                     [&s] { s.prep.normalize = false; s.changed(); },
                     nullptr };

        // The wheel over a marker turns that marker's encoder (the trim page,
        // even while the shape page is latched).
        std::array<std::function<void (int, encoder::Speed)>, 4> trimTurns { trim[0].turn, trim[1].turn, trim[2].turn, trim[3].turn };
        band.onMarkerWheel = [this, &bus, trimTurns] (int i, const juce::MouseEvent& e, const juce::MouseWheelDetails& w)
        {
            if (! shapeLatched)
            {
                bus.wheel (i, e, w);
                return;
            }
            const float d = std::abs (w.deltaY) >= std::abs (w.deltaX) ? w.deltaY : -w.deltaX;
            if (d != 0.0f)
                trimTurns[(size_t) i] ((d > 0.0f) != w.isReversed ? 1 : -1, e.mods.isAltDown() ? encoder::Speed::fine : encoder::Speed::normal);
        };

        std::array<std::optional<EncoderBinding>, 4> out;
        for (size_t i = 0; i < 4; ++i)
        {
            const Param main = shapeLatched ? shape[i] : trim[i];
            const Param other = shapeLatched ? trim[i] : shape[i];
            EncoderBinding e;
            e.label = main.label;
            e.shiftLabel = other.label;
            e.getNorm = [&bus, main, other] { return bus.shiftActive() ? other.norm() : main.norm(); };
            e.setNorm = main.setNorm;
            e.turn = main.turn;
            e.reset = main.reset;
            e.format = [&bus, main, other] { return bus.shiftActive() ? other.format() : main.format(); };
            e.unit = [&bus, main, other] { return bus.shiftActive() ? other.unit() : main.unit(); };
            e.caption = [&bus, main, other] { return bus.shiftActive() ? other.caption() : main.caption(); };
            e.pressLabel = main.pressLabel;
            e.press = main.press;
            e.shiftPressLabel = other.pressLabel;
            e.shiftPress = other.press;
            e.shiftTurn = [turn = other.turn] (int d) { turn (d, encoder::Speed::normal); };
            e.available = [&s] { return s.hasSource(); };
            e.reason = [] { return juce::String ("nothing open"); };
            e.hint = [] { return juce::String ("wheel turns" + dot + "alt fine" + dot + "ctrl coarse" + dot + "shift: the other page"); };
            e.gauge = [&bus, main, other] (juce::Graphics& g, juce::Rectangle<float> r, juce::Colour c, bool)
            {
                const auto& p = bus.shiftActive() ? other : main;
                if (p.gauge)
                    p.gauge (g, r, c);
            };
            out[i] = e;
        }
        return out;
    }
}
