#include "UI/Pages/ChopPage.h"
#include "Audio/SampleIO.h"
#include "Core/Ep40MidiMapping.h"
#include "Core/HonestState.h"
#include "UI/Design/Numerals.h"
#include "UI/Design/Paint.h"
#include "UI/Design/Pictograms.h"
#include "UI/Design/Primitives/Gauges.h"

namespace halo
{
    namespace
    {
        const juce::String dot = juce::String::fromUTF8 (" \xc2\xb7 ");
        const juce::String enDash = juce::String::charToString ((juce::juce_wchar) 0x2013);
        const juce::String arrow = juce::String::charToString ((juce::juce_wchar) 0x2192);
        const juce::String ellipsis = juce::String::charToString ((juce::juce_wchar) 0x2026);
        const juce::String plusMinus = juce::String::charToString ((juce::juce_wchar) 0x00B1);
        const juce::String ep = "ep" + enDash + "40";

        constexpr Treatment treatments[] { Treatment::original, Treatment::high, Treatment::balanced,
                                           Treatment::loFi22k, Treatment::loFi11k };
        constexpr SamplePrep::Channels channelModes[] { SamplePrep::Channels::preserve, SamplePrep::Channels::mono,
                                                        SamplePrep::Channels::stereo };
        constexpr int sliceChoices[] { EditSession::sliceFit, 4, 8, 16, EditSession::sliceAll };

        juce::String mb (double bytes) { return juce::String (bytes / (1024.0 * 1024.0), 2); }

        juce::String timeText (double seconds)
        {
            const int s = juce::roundToInt (seconds);
            return s < 60 ? juce::String (s) : juce::String (s / 60) + ":" + juce::String (s % 60).paddedLeft ('0', 2);
        }

        void zoneLabel (juce::Graphics& g, juce::Rectangle<float> r, const juce::String& text, const juce::String& right = {})
        {
            paint::text (g, text, r, HaloFonts::font (Role::label), theme().scr.dim);
            if (right.isNotEmpty())
                paint::text (g, right, r, HaloFonts::font (Role::ui, 11.5f), theme().scr.dim, juce::Justification::centredRight);
        }
    }

    ChopPage::ChopPage (AppModel& m, WriteGate& g) : model (m), gate (g), session (m.editor())
    {
        setOpaque (false);
        setWantsKeyboardFocus (true);
        for (auto* c : std::initializer_list<juce::Component*> { &slicer, &pads, &groups, &quality, &channels, &name, &noticeView,
                                                                 &clearKey, &exportKey, &showKey, &sendKey })
            addAndMakeVisible (c);

        groups.setItems ({ "a", "b", "c", "d" });
        groups.onSelect = [this] (int i) { session.group = i; session.changed(); };
        quality.setItems ({ "original", "high", "balanced", "lo 22k", "lo 11k" });
        quality.onSelect = [this] (int i) { session.treatment = treatments[(size_t) i]; session.changed(); };
        channels.setItems ({ "keep", "mono", "stereo" });
        channels.onSelect = [this] (int i) { session.prep.channelMode = channelModes[(size_t) i]; session.changed(); };
        name.onChange = [this] (const juce::String& n) { session.baseName = n; session.changed(); };
        pads.onPadClicked = [this] (int grid) { session.setStartPad (grid); session.changed(); };
        pads.onWheel = [this] (int detents) { session.setStartPad (juce::jlimit (0, 11, session.startPad + detents)); };

        clearKey.setTooltip ("Remove every cut (shift + encoder 1 press does the same)");
        clearKey.onClick = [this] { session.slices.clear(); session.selectedSlice.reset(); session.changed(); };
        exportKey.onClick = [this] { session.exportSlices(); };
        exportKey.setTooltip ("Export every slice as an EP-40-ready WAV, with a pads.txt guide (ctrl e)");
        showKey.onClick = [this]
        {
            const auto f = session.lastExport();
            (f.exists() ? f : SampleIO::exportFolder()).revealToUser();
        };
        sendKey.onClick = [this] { send(); };
        slicer.onViewChanged = [this] { repaint(); };

        session.addChangeListener (this);
        model.device().addChangeListener (this);
        model.backup().addChangeListener (this);
        model.addListener (this);
        refresh();
    }

    ChopPage::~ChopPage()
    {
        session.removeChangeListener (this);
        model.device().removeChangeListener (this);
        model.backup().removeChangeListener (this);
        model.removeListener (this);
    }

    juce::Rectangle<float> ChopPage::localF (const layout::Rect& r) const
    {
        return { r.x - frame.body.x, r.y - frame.body.y, r.w, r.h };
    }

    void ChopPage::setFrame (const layout::Frame& f)
    {
        frame = f;
        resized();
    }

    //==========================================================================
    juce::String ChopPage::sendBlocker() const
    {
        // Today's copy (ChopSurface), verbatim.
        const auto plan = session.plan();
        if (plan.tooLongCount > 0)
            return juce::String (plan.tooLongCount) + (plan.tooLongCount == 1 ? " slice is" : " slices are") + " over "
                   + juce::String ((int) maxSoundSeconds) + " s (longest " + juce::String (plan.longestSeconds, 1)
                   + " s), the EP-40's limit per sound. Turn on AUTO-TRIM, use more slices, or CLOSE and trim in EDIT.";
        const auto& info = model.device().info();
        if (model.device().state() == Ep40Library::State::ready && info.freeBytes >= 0 && plan.totalBytes > info.freeBytes)
            return "These slices need " + juce::String (formatBytes (plan.totalBytes)) + " but the EP-40 has "
                   + juce::String (formatBytes (info.freeBytes)) + " free. Turn on AUTO-TRIM, use fewer slices, or pick a LO quality.";
        if (session.autoTrim && plan.trimmedCount > 0 && session.sliceCapSeconds() < 0.25)
            return "The EP-40 is nearly full: AUTO-TRIM would cut the slices to " + juce::String (session.sliceCapSeconds() * 1000.0, 0)
                   + " ms. Delete sounds in LOAD, use fewer slices, or pick a LO quality.";
        return {};
    }

    juce::String ChopPage::trimNote() const
    {
        const auto plan = session.plan();
        if (plan.trimmedCount == 0)
            return {};
        const auto why = session.capLimitedBySpace() ? juce::String (" to fit the EP-40's free memory")
                       : session.lengthIsLimit()     ? juce::String (" (your LENGTH)")
                                                     : juce::String (" (the EP-40's limit)");
        return juce::String (session.capLimitedBySpace() || ! session.lengthIsLimit() ? "AUTO-TRIM: " : "LENGTH: ")
               + juce::String (plan.trimmedCount) + (plan.trimmedCount == 1 ? " slice" : " slices")
               + " cut to " + juce::String (session.sliceCapSeconds(), 1) + " s" + why
               + ", from each slice's start, with a fade-out. The shaded tails are dropped.";
    }

    juce::String ChopPage::plainNote() const
    {
        if (model.writesUnlocked())
            return "SEND TO PADS uploads the slices into empty USER slots and assigns them to these pads in project "
                   + juce::String (model.device().activeProject()) + " (the project selected in LOAD).";
        if (model.device().state() == Ep40Library::State::ready)
            return "Back up the EP-40 in BACKUPS (Ctrl+6) to unlock SEND TO PADS. EXPORT works now.";
        return "Connect the EP-40 to send slices to its pads. EXPORT works now.";
    }

    NoticeData ChopPage::notice() const
    {
        if (! session.hasSource())
            return {};
        return NoticeData::from (honest::pickNotice (sendBlocker().toStdString(), trimNote().toStdString(), plainNote().toStdString()));
    }

    void ChopPage::send()
    {
        if (! model.writesUnlocked() || session.slices.sliceCount() == 0 || sendBlocker().isNotEmpty())
            return;
        const auto plan = session.plan();
        const int project = model.device().activeProject();
        WriteSpec spec;
        spec.kind = WriteSpec::Kind::sendToPads;
        spec.confirmLabel = "send";
        spec.body = "Upload " + juce::String ((int) plan.writes.size()) + " slices (" + juce::String (formatBytes (plan.totalBytes))
                    + ") into empty USER slots, and assign them to group " + ep40::groupLabels[(size_t) plan.group].data()
                    + " pads in project " + juce::String (project) + "?" + juce::newLine + juce::newLine
                    + (plan.trimmedCount > 0 ? juce::String (plan.trimmedCount) + " slice(s) are cut to "
                                                   + juce::String (session.sliceCapSeconds(), 1) + " s." + juce::newLine
                                             : juce::String())
                    + "Takes " + Ep40Writer::expectedTime (plan.totalBytes) + " (the EP-40 takes about 40 KB/s; MONO or a LO quality is quicker)." + juce::newLine
                    + "No sound is overwritten. The pads' old sounds stay in the library.";
        spec.from = juce::String ((int) plan.writes.size()) + " slices";
        spec.to = "group " + juce::String (ep40::groupLabels[(size_t) plan.group].data()).toLowerCase() + ", project " + juce::String (project);
        spec.estimateSeconds = (double) plan.totalBytes / Ep40Writer::typicalBytesPerSecond;
        spec.estimateText = Ep40Writer::expectedTime (plan.totalBytes);
        gate.request (spec, [this, plan, project] (GatedWriter writer)
        {
            session.report ("Preparing slices...");
            session.renderPlacedSlices ([this, plan, project, writer] (std::shared_ptr<std::vector<AudioData>> audio)
            {
                std::vector<Ep40Writer::PadJob> jobs;
                for (size_t i = 0; i < plan.writes.size() && i < audio->size(); ++i)
                    jobs.push_back ({ std::move ((*audio)[i]), juce::String (plan.writes[i].name), plan.writes[i].group, plan.writes[i].gridIndex });
                writer.uploadToPads (std::move (jobs), project,
                    [this] (const juce::String& progress) { session.report (progress); },
                    [this] (Ep40Writer::Result r) { session.report (r.ok ? r.message + ". Play them on the EP-40!" : "Send failed: " + r.message); });
            });
        });
    }

    //==========================================================================
    void ChopPage::refresh()
    {
        // AUTO-TRIM sizes the slices to what the EP-40 has free, when it's known.
        const auto& info = model.device().info();
        session.spaceBudgetBytes = model.device().state() == Ep40Library::State::ready && info.freeBytes >= 0 ? info.freeBytes : -1;

        groups.setSelected (session.group);
        for (int i = 0; i < 5; ++i)
            if (session.treatment == treatments[(size_t) i])
                quality.setSelected (i);
        for (int i = 0; i < 3; ++i)
            if (session.prep.channelMode == channelModes[(size_t) i])
                channels.setSelected (i);
        if (! name.hasKeyboardFocus (false))
            name.setText (session.baseName);

        // Pads: where each slice lands.
        const auto plan = session.plan();
        const auto all = session.slices.slices();
        std::array<PadGrid::Pad, 12> p {};
        for (int i = 0; i < 12; ++i)
            p[(size_t) i].empty = true;
        for (const auto& w : plan.writes)
        {
            auto& pad = p[(size_t) w.gridIndex];
            pad.empty = false;
            pad.numeral = juce::String (w.sliceIndex + 1);
            pad.selected = session.selectedSlice == w.sliceIndex;
            if (w.sliceIndex < (int) all.size())
                pad.trimmed = session.playedPart (all[(size_t) w.sliceIndex]).endFrame < all[(size_t) w.sliceIndex].endFrame;
        }
        if (session.hasSource())
            p[(size_t) juce::jlimit (0, 11, plan.startGridIndex)].start = true;
        pads.setPads (p);

        const bool has = session.hasSource() && session.slices.sliceCount() > 0;
        exportKey.setEnabled (has);
        showKey.setVisible (session.lastExport().exists());
        clearKey.setEnabled (has && ! session.slices.cuts().empty());

        // SEND TO PADS: a device-write key, or why it can't send.
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

    void ChopPage::resized()
    {
        if (frame.width <= 0)
            return;
        const auto n = notice();
        L = layout::chopLayout (frame, n.text.toStdString(), n.kind == honest::NoticeKind::blocker || n.kind == honest::NoticeKind::info);
        slicer.setBounds (localF (L.canvas).toNearestInt());

        // Header: the CLEAR CUTS key sits left of the mode words.
        const auto head = localF (L.header);
        const int clearW = clearKey.preferredWidth();
        clearKey.setBounds (juce::Rectangle<float> ((float) clearW, juce::jmin (34.0f, head.getHeight() - 6.0f))
                                .withPosition (head.getRight() - 360.0f - (float) clearW, head.getCentreY() - 17.0f).toNearestInt());

        // Pads zone: GROUP / PROJECT row, then the grid beside the plan column.
        auto padsZone = localF (L.pads);
        auto row = padsZone.removeFromTop (30.0f);
        row.removeFromLeft (52.0f);
        groups.setBounds (row.removeFromLeft (4.0f * 34.0f + 18.0f).toNearestInt());
        padsZone.removeFromTop (8.0f);
        const float planW = L.stacked ? 120.0f : 130.0f;
        auto grid = padsZone.removeFromLeft (padsZone.getWidth() - planW - 12.0f);
        pads.setBounds (grid.withHeight (4.0f * L.padRow + 3.0f * (float) PadGrid::gap).toNearestInt());

        auto out = localF (L.output);
        auto sendZone = localF (L.send);
        if (! L.stacked)
        {
            out.removeFromTop (18.0f);
            quality.setBounds (out.removeFromTop (30.0f).toNearestInt());
            out.removeFromTop (12.0f + 18.0f);
            channels.setBounds (out.removeFromTop (30.0f).withWidth (juce::jmin (300.0f, out.getWidth() * 0.66f)).toNearestInt());
            out.removeFromTop (12.0f + 18.0f);
            name.setBounds (out.removeFromTop (34.0f).withWidth ((float) NameCells::widthForHeight (34)).toNearestInt());

            sendZone.removeFromTop (18.0f + 12.0f + 10.0f); // memory label + dots
            noticeView.setBounds (sendZone.removeFromTop ((float) noticeView.heightForWidth ((int) sendZone.getWidth())).toNearestInt());
            plainArea = sendZone.removeFromTop (48.0f).withTrimmedTop (8.0f);
            auto actions = sendZone.removeFromBottom (14.0f + 44.0f).removeFromTop (44.0f);
            exportKey.setBounds (actions.removeFromLeft ((float) exportKey.preferredWidth()).toNearestInt());
            actions.removeFromLeft (10.0f);
            showKey.setBounds (actions.removeFromLeft ((float) showKey.preferredWidth()).toNearestInt());
            sendKey.setBounds (actions.removeFromRight ((float) sendKey.preferredWidth()).toNearestInt());
        }
        else
        {
            auto row1 = out.removeFromTop (30.0f);
            quality.setBounds (row1.removeFromLeft (row1.getWidth() * 0.6f).toNearestInt());
            row1.removeFromLeft (12.0f);
            channels.setBounds (row1.toNearestInt());
            out.removeFromTop (6.0f);
            auto row2 = out.removeFromTop (36.0f);
            name.setBounds (row2.withSizeKeepingCentre (row2.getWidth(), 32.0f).withWidth ((float) NameCells::widthForHeight (32)).toNearestInt());

            noticeView.setBounds (sendZone.removeFromTop ((float) noticeView.heightForWidth ((int) sendZone.getWidth())).toNearestInt());
            auto actions = sendZone.removeFromBottom (frame.density == layout::Density::ultra ? 30.0f : 36.0f);
            const int keyH = (int) actions.getHeight();
            exportKey.setBounds (actions.removeFromLeft ((float) exportKey.preferredWidth()).withHeight ((float) keyH).toNearestInt());
            actions.removeFromLeft (10.0f);
            showKey.setBounds (actions.removeFromLeft ((float) showKey.preferredWidth()).toNearestInt());
            sendKey.setBounds (actions.removeFromRight ((float) sendKey.preferredWidth()).toNearestInt());
        }
    }

    //==========================================================================
    juce::Rectangle<float> ChopPage::modeWordBounds (bool even) const
    {
        const auto head = localF (L.header);
        const auto font = HaloFonts::face (HaloFonts::Face::interMedium, 14.0f);
        const float evenW = paint::textWidth (font, "even") + 8.0f, transientW = paint::textWidth (font, "transient") + 8.0f;
        const float right = head.getRight() - 196.0f;
        return even ? juce::Rectangle<float> (right - evenW, head.getY(), evenW, head.getHeight())
                    : juce::Rectangle<float> (right - evenW - 10.0f - transientW, head.getY(), transientW, head.getHeight());
    }

    juce::Rectangle<float> ChopPage::projectArrow (int dir) const
    {
        auto row = localF (L.pads).removeFromTop (30.0f);
        row.removeFromLeft (52.0f + 4.0f * 34.0f + 18.0f + 16.0f);
        const float x = row.getX() + 60.0f;
        return dir < 0 ? juce::Rectangle<float> (x, row.getY(), 16.0f, row.getHeight())
                       : juce::Rectangle<float> (x + 46.0f, row.getY(), 16.0f, row.getHeight());
    }

    void ChopPage::mouseUp (const juce::MouseEvent& e)
    {
        if (! e.mouseWasClicked())
            return;
        if (modeWordBounds (false).contains (e.position) && session.evenSlices)
            session.setEvenSlices (false);
        else if (modeWordBounds (true).contains (e.position) && ! session.evenSlices)
            session.setEvenSlices (true);
        for (int dir : { -1, 1 })
            if (projectArrow (dir).contains (e.position) && model.device().state() == Ep40Library::State::ready)
                model.device().readPads (juce::jlimit (1, 9, model.device().activeProject() + dir));
        grabKeyboardFocus();
    }

    bool ChopPage::keyPressed (const juce::KeyPress& key)
    {
        const auto code = key.getKeyCode();
        const auto mods = key.getModifiers();
        if (mods.isCtrlDown() && code == 'E')                          { session.exportSlices(); return true; }
        if (mods.isCtrlDown() && code == juce::KeyPress::returnKey)    { send(); return true; }
        // 1-9 then 0 audition slices 1-10.
        if (! mods.isAnyModifierKeyDown() && code >= '0' && code <= '9')
        {
            session.auditionSlice (code == '0' ? 9 : code - '1');
            return true;
        }
        if ((code == juce::KeyPress::deleteKey || code == juce::KeyPress::backspaceKey) && session.selectedSlice && *session.selectedSlice > 0)
        {
            session.slices.removeCut (*session.selectedSlice - 1);
            session.selectedSlice = *session.selectedSlice - 1;
            session.changed();
            return true;
        }
        if (code == juce::KeyPress::tabKey && session.slices.sliceCount() > 0)
        {
            session.selectedSlice = (session.selectedSlice.value_or (-1) + 1) % session.slices.sliceCount();
            session.changed();
            return true;
        }
        if (code == juce::KeyPress::spaceKey && session.selectedSlice)
        {
            session.auditionSlice (*session.selectedSlice);
            return true;
        }
        if (code == juce::KeyPress::escapeKey)
        {
            session.closeChop();
            return true;
        }
        return false;
    }

    //==========================================================================
    void ChopPage::paint (juce::Graphics& g)
    {
        if (frame.width <= 0)
            return;
        const auto& t = theme();
        const auto audio = session.audio();
        const auto plan = session.plan();
        const int count = session.slices.sliceCount();

        // ---- Header ----
        auto head = localF (L.header);
        {
            auto icon = head.removeFromLeft (head.getHeight() - 6.0f).withSizeKeepingCentre (34.0f, 34.0f);
            g.setColour (t.scr.ink.withAlpha (0.85f));
            g.drawRoundedRectangle (icon.reduced (0.6f), 3.0f, 1.2f);
            pictograms::draw (g, Pict::chop, icon.reduced (8.0f), t.scr.ink, 1.4f);
            head.removeFromLeft (14.0f);
            paint::text (g, "chop", head.withHeight (14.0f), HaloFonts::font (Role::ui, 11.5f), t.scr.dim);
            const auto titleFont = HaloFonts::font (Role::title);
            const auto title = session.hasSource() || session.isLoading() ? session.item().title.toUpperCase() : juce::String ("NOTHING OPEN");
            auto titleRow = head.withTrimmedTop (14.0f);
            const float titleW = juce::jmin (titleRow.getWidth() * 0.4f, paint::textWidth (titleFont, title) + 4.0f);
            paint::text (g, paint::middleEllipsis (title, titleFont, titleW), titleRow.withWidth (titleW), titleFont, t.scr.ink);
            juce::String meta;
            if (audio != nullptr)
                meta = juce::String (audio->durationSeconds(), 1) + " s" + dot + juce::String (audio->sampleRate / 1000.0, 1) + " khz" + dot
                     + (audio->channelCount() == 1 ? "mono" : "stereo") + dot + "from edit";
            else if (! session.isLoading())
                meta = "open a sample in EDIT, then chop it (ctrl 7)";
            paint::text (g, meta, titleRow.withTrimmedLeft (titleW + 20.0f).withTrimmedRight (520.0f), HaloFonts::font (Role::ui, 12.0f),
                         t.scr.dim);

            // Mode words, the big count, esc.
            const auto words = HaloFonts::face (HaloFonts::Face::interMedium, 14.0f);
            for (bool even : { false, true })
            {
                const auto r = modeWordBounds (even);
                const bool on = session.evenSlices == even;
                paint::text (g, even ? "even" : "transient", r, words, on ? t.scr.hot : t.scr.dim, juce::Justification::centred);
                if (on)
                    g.fillRect (r.withTrimmedTop (r.getHeight() - 6.0f).withHeight (2.0f).reduced (2.0f, 0.0f));
            }
            const auto full = localF (L.header);
            numerals::drawWithUnit (g, juce::String (count), "slices", full.getRight() - 170.0f, full.getBottom() - 6.0f, numerals::L,
                                    t.scr.ink, t.scr.dim);
            auto esc = juce::Rectangle<float> (full.getRight() - 30.0f, full.getY() + 2.0f, 30.0f, 16.0f);
            paint::text (g, "esc", esc, HaloFonts::font (Role::ui, 11.5f), t.scr.dim, juce::Justification::centredRight);
        }

        // ---- Overview strip: the whole file and the zoomed view ----
        if (L.showOverview && ! L.overview.empty())
        {
            const auto r = localF (L.overview);
            const auto& w = session.summary();
            if (audio != nullptr && w.bucketCount() > 0)
            {
                const float peak = juce::jmax (0.05f, w.peakMagnitude());
                g.setColour (t.scr.ramp[3]);
                for (int x = 0; x < (int) r.getWidth(); x += 2)
                {
                    const int b = juce::jlimit (0, w.bucketCount() - 1, (int) ((float) x / r.getWidth() * (float) w.bucketCount()));
                    const float a = juce::jmax (std::abs (w.minima[(size_t) b]), std::abs (w.maxima[(size_t) b])) / peak;
                    g.fillRect (r.getX() + (float) x, r.getCentreY() - a * r.getHeight() * 0.5f, 1.0f, juce::jmax (1.0f, a * r.getHeight()));
                }
                const double n = (double) audio->frameCount();
                const float x0 = r.getX() + r.getWidth() * (float) ((double) slicer.viewStart() / n);
                const float x1 = r.getX() + r.getWidth() * (float) ((double) slicer.viewEnd() / n);
                if (x1 - x0 < r.getWidth() - 1.0f)
                {
                    g.setColour (t.scr.ink);
                    g.drawRect (juce::Rectangle<float>::leftTopRightBottom (x0, r.getY(), x1, r.getBottom()), 1.0f);
                }
            }
        }

        // ---- Ruler: 1 s ticks, labels spaced at least 64 px ----
        {
            const auto r = localF (L.ruler);
            paint::hairlineH (g, r.getX(), r.getRight(), r.getBottom() - 1.0f, t.scr.rule);
            if (audio != nullptr && audio->sampleRate > 0.0)
            {
                const double s0 = (double) slicer.viewStart() / audio->sampleRate, s1 = (double) slicer.viewEnd() / audio->sampleRate;
                const double pxPerSecond = r.getWidth() / juce::jmax (0.001, s1 - s0);
                int every = 1;
                while (every * pxPerSecond < 64.0 && every < 4096)
                    every *= 2;
                const auto print = HaloFonts::font (Role::print, 10.5f);
                for (int s = (int) std::ceil (s0); s <= (int) s1; ++s)
                {
                    const float x = r.getX() + (float) ((s - s0) * pxPerSecond);
                    const bool major = s % every == 0;
                    if (! major && pxPerSecond < 5.0)
                        continue;
                    g.setColour (major ? t.scr.dim : t.scr.mute);
                    g.fillRect (x, major ? r.getY() + 2.0f : r.getBottom() - 6.0f, 1.0f, major ? r.getHeight() - 3.0f : 5.0f);
                    if (major)
                        paint::text (g, juce::String (s) + (s + every > (int) s1 ? " s" : ""), { x + 3.0f, r.getY(), 40.0f, r.getHeight() - 2.0f },
                                     print, t.scr.dim);
                }
            }
        }

        // ---- The selected slice's bracket, or the canvas hints; the status line ----
        {
            const auto r = localF (L.bracket);
            const auto all = session.slices.slices();
            if (session.selectedSlice && *session.selectedSlice < (int) all.size() && audio != nullptr)
            {
                const auto& s = all[(size_t) *session.selectedSlice];
                const auto kept = session.playedPart (s);
                const double rate = audio->sampleRate;
                juce::String text = "SLICE " + juce::String (s.index + 1) + dot + juce::String (s.frameCount() / rate, 1) + " S";
                if (kept.endFrame < s.endFrame)
                    text << " " << arrow << " " << juce::String (kept.frameCount() / rate, 1) << " S";
                if (session.player().isPlaying() && session.player().token() == s.index)
                    text << dot << "PLAYING";
                paint::text (g, text, r, HaloFonts::font (Role::label), t.scr.ink);
            }
            else
            {
                const auto longHint = "click a slice or press 1-9, 0 to hear it" + dot + "double-click: add cut" + dot + "drag a marker: move"
                                + dot + "right-click / delete: remove" + dot + "ctrl+wheel: zoom";
            const auto shortHint = "click: hear" + dot + "double-click: cut" + dot + "drag: move" + dot + "right-click: remove" + dot + "ctrl+wheel: zoom";
            const auto hintFont = HaloFonts::font (Role::ui, 11.5f);
            paint::text (g, paint::textWidth (hintFont, longHint) <= r.getWidth() ? longHint : shortHint, r, hintFont, t.scr.dim);
            }
            const auto sr = localF (L.status);
            const auto status = session.status (EditSession::Page::chop);
            const bool failed = status.startsWith ("Export failed") || status.startsWith ("Send failed");
            if (failed)
            {
                const float w = juce::jmin (sr.getWidth(), paint::textWidth (HaloFonts::font (Role::ui, 12.0f), status) + 30.0f);
                auto chip = sr.withLeft (sr.getRight() - w);
                g.setColour (t.honest.danger);
                g.drawRoundedRectangle (chip.reduced (0.5f), 3.0f, 1.0f);
                pictograms::draw (g, Pict::triangle, { chip.getX() + 5.0f, chip.getCentreY() - 6.0f, 12.0f, 12.0f }, t.honest.danger, 1.3f);
                paint::text (g, status, chip.withTrimmedLeft (22.0f), HaloFonts::font (Role::ui, 12.0f), t.scr.ink);
            }
            else
            {
                paint::text (g, status, sr, HaloFonts::font (Role::mono, 11.0f), t.scr.ink, juce::Justification::centredRight);
            }
        }

        // ---- Lower band ----
        paint::hairlineH (g, localF (L.pads).getX(), localF (L.send).getRight(), localF (L.pads).getY() - 4.0f, t.scr.rule);

        // Pads zone: label, project picker, plan column.
        {
            auto zone = localF (L.pads);
            auto row = zone.removeFromTop (30.0f);
            paint::text (g, "PADS", row.withWidth (52.0f), HaloFonts::font (Role::label), t.scr.dim);
            const bool ready = model.device().state() == Ep40Library::State::ready;
            auto left = projectArrow (-1), right = projectArrow (1);
            paint::text (g, "project", { left.getX() - 58.0f, row.getY(), 56.0f, row.getHeight() }, HaloFonts::font (Role::ui, 12.0f), t.scr.dim,
                         juce::Justification::centredRight);
            pictograms::draw (g, Pict::chevronLeft, left.withSizeKeepingCentre (12.0f, 12.0f), ready ? t.scr.ink : t.scr.frame, 1.4f);
            pictograms::draw (g, Pict::chevronRight, right.withSizeKeepingCentre (12.0f, 12.0f), ready ? t.scr.ink : t.scr.frame, 1.4f);
            const auto proj = ready ? juce::String (model.device().activeProject()) : enDash;
            numerals::draw (g, proj, left.getRight() + 15.0f - numerals::width (proj, numerals::S) * 0.5f, row.getCentreY() + numerals::S * 0.5f,
                            numerals::S, ready ? t.scr.ink : t.scr.dim);

            zone.removeFromTop (8.0f);
            const float planW = L.stacked ? 120.0f : 130.0f;
            auto planCol = zone.removeFromRight (planW);
            const auto ui = HaloFonts::font (Role::ui, 12.0f);
            numerals::drawWithUnit (g, juce::String (count), "slices", planCol.getX(), planCol.getY() + numerals::M, numerals::M, t.scr.ink, t.scr.dim);
            auto lines = planCol.withTrimmedTop (numerals::M + 8.0f);
            const auto startLabel = juce::String (ep40::gridLabels[(size_t) juce::jlimit (0, 11, plan.startGridIndex)].data()).toLowerCase();
            paint::text (g, "from pad " + (startLabel == "enter" ? juce::String ("ent") : startLabel), lines.removeFromTop (16.0f),
                         HaloFonts::font (Role::bodyStrong, 12.0f), t.scr.ink);
            const auto groupLabel = juce::String (ep40::groupLabels[(size_t) plan.group].data()).toLowerCase();
            paint::text (g, "group " + groupLabel + dot + "project " + (ready ? juce::String (model.device().activeProject()) : enDash),
                         lines.removeFromTop (16.0f), HaloFonts::font (Role::bodyStrong, 12.0f), t.scr.ink);
            lines.removeFromTop (6.0f);
            if (plan.overflowCount > 0)
            {
                auto box = lines.removeFromTop (54.0f);
                g.setColour (t.honest.danger);
                g.drawRoundedRectangle (box.reduced (0.5f), 3.0f, 1.0f);
                paint::paragraph (g, juce::String (plan.overflowCount) + " slices don't fit on this group's pads. They're still exported.",
                                  box.reduced (6.0f, 4.0f), HaloFonts::font (Role::ui, 11.5f), t.scr.ink, 1.0f);
            }
            else if (count > 0)
            {
                auto fit = lines.removeFromTop (16.0f);
                paint::lamp (g, { fit.getX() + 3.5f, fit.getCentreY() }, t.scr.hot, 7.0f);
                paint::text (g, "all " + juce::String ((int) plan.writes.size()) + " fit group " + groupLabel, fit.withTrimmedLeft (12.0f), ui, t.scr.ink);
            }
            if (! L.stacked)
            {
                lines.removeFromTop (8.0f);
                auto l1 = lines.removeFromTop (16.0f);
                paint::lamp (g, { l1.getX() + 3.5f, l1.getCentreY() }, t.scr.hot, 6.0f);
                paint::text (g, "start pad", l1.withTrimmedLeft (12.0f), ui, t.scr.dim);
                auto l2 = lines.removeFromTop (16.0f);
                auto chip = juce::Rectangle<float> (l2.getX(), l2.getCentreY() - 2.5f, 12.0f, 5.0f);
                paint::hatch (g, chip, t.encoder (3).line, 3.0f, 1.0f);
                g.setColour (t.encoder (3).line);
                g.drawRect (chip, 1.0f);
                paint::text (g, "cut by length", l2.withTrimmedLeft (16.0f), ui, t.scr.dim);
                paint::paragraph (g, "click a pad to start there", lines.removeFromBottom (32.0f), ui, t.scr.dim, 1.0f);
            }
        }

        // Output zone.
        const auto rate = resolvedSampleRate (session.treatment, audio != nullptr ? audio->sampleRate : 0.0);
        const double seconds = (double) plan.totalBytes / Ep40Writer::typicalBytesPerSecond;
        auto totals = [&] (juce::Rectangle<float> r, float cap)
        {
            float x = r.getX();
            const float base = r.getY() + cap + 2.0f;
            x = numerals::drawWithUnit (g, mb ((double) plan.totalBytes), "mb", x, base, cap, t.scr.ink, t.scr.dim) + 22.0f;
            x = numerals::drawWithUnit (g, juce::String ((int) plan.writes.size()), "pads", x, base, cap, t.scr.ink, t.scr.dim) + 22.0f;
            numerals::drawWithUnit (g, "~" + timeText (seconds), seconds < 60.0 ? "s to send" : "min to send", x, base, cap, t.scr.ink, t.scr.dim);
        };
        if (! L.stacked)
        {
            auto out = localF (L.output);
            zoneLabel (g, out.removeFromTop (18.0f), "EP" + enDash + "40 QUALITY",
                       juce::String (rate / 1000.0, rate >= 44000.0 ? 1 : 3) + " khz 16-bit");
            out.removeFromTop (30.0f + 12.0f);
            zoneLabel (g, out.removeFromTop (18.0f), "CHANNELS");
            auto chRow = out.removeFromTop (30.0f);
            paint::text (g, "shared with edit", chRow.withTrimmedLeft ((float) channels.getWidth() + 12.0f), HaloFonts::font (Role::ui, 11.5f), t.scr.dim);
            out.removeFromTop (12.0f);
            zoneLabel (g, out.removeFromTop (18.0f), "NAME", "a" + enDash + "z 0" + enDash + "9" + dot + "8 max");
            auto nameRow = out.removeFromTop (34.0f);
            const auto saveAs = session.baseName.isNotEmpty() ? session.baseName : juce::String ("CHOP");
            auto saveText = nameRow.withTrimmedLeft ((float) NameCells::widthForHeight (34) + 16.0f);
            paint::text (g, "slices save as", saveText.removeFromTop (16.0f), HaloFonts::font (Role::ui, 11.5f), t.scr.dim);
            paint::text (g, saveAs + " 01 " + ellipsis + " " + juce::String (juce::jmax (1, count)).paddedLeft ('0', 2), saveText,
                         HaloFonts::font (Role::mono, 11.0f), t.scr.ink);
            out.removeFromTop (12.0f);
            totals (out, numerals::M);
        }
        else
        {
            auto out = localF (L.output);
            out.removeFromTop (36.0f);
            auto row2 = out.removeFromTop (36.0f);
            totals (row2.withTrimmedLeft ((float) NameCells::widthForHeight (32) + 20.0f).withSizeKeepingCentre (row2.getWidth() - (float) NameCells::widthForHeight (32) - 20.0f, numerals::S + 6.0f),
                    numerals::S);
        }

        // Memory (send zone top at 1440; the output column's last row when stacked).
        {
            const auto& info = model.device().info();
            const bool known = model.device().state() == Ep40Library::State::ready && info.capacityBytes > 0 && info.freeBytes >= 0;
            juce::Rectangle<float> label, dots;
            if (! L.stacked)
            {
                auto s = localF (L.send);
                label = s.removeFromTop (18.0f);
                dots = s.removeFromTop (12.0f);
            }
            else
            {
                auto o = localF (L.output);
                dots = o.removeFromBottom (12.0f).withTrimmedRight (o.getWidth() * 0.45f);
                label = o.removeFromBottom (12.0f).withX (dots.getRight() + 10.0f).withWidth (o.getWidth() * 0.45f - 10.0f).translated (0.0f, 12.0f);
            }
            juce::String facts = "memory unknown: connect the " + ep;
            int used = 0, current = 0;
            if (known)
            {
                const double cap = (double) info.capacityBytes;
                const double usedBytes = cap - (double) info.freeBytes;
                used = juce::roundToInt (usedBytes / cap * 61.0);
                current = juce::roundToInt ((double) plan.totalBytes / cap * 61.0);
                facts = mb (usedBytes) + " used" + dot + "+" + mb ((double) plan.totalBytes) + " this" + dot
                      + mb ((double) info.freeBytes - (double) plan.totalBytes) + " free of " + mb (cap) + " mb";
            }
            if (! L.stacked)
                zoneLabel (g, label, "EP" + enDash + "40 MEMORY", facts);
            else
                paint::text (g, facts, label, HaloFonts::font (Role::ui, 11.0f), t.scr.dim);
            paint::dotTimeline (g, dots, 61, used, current, known && (double) (used + current) / 61.0 > 0.9);
        }

        // Send zone: the backup line over the actions; captions under them.
        {
            const auto view = honest::writeView ([&]
            {
                honest::Inputs in;
                in.source = model.isMock() ? honest::Source::mock : honest::Source::device;
                in.connected = model.canTalkToDevice();
                in.libraryReady = model.device().state() == Ep40Library::State::ready;
                in.writesUnlocked = model.writesUnlocked();
                in.busy = model.deviceWriter().isBusy();
                in.feed = honest::Feed::live;
                return in;
            }());
            auto s = localF (L.send);
            auto captions = s.removeFromBottom (L.stacked ? 0.0f : 14.0f);
            s.removeFromBottom (L.stacked ? (frame.density == layout::Density::ultra ? 30.0f : 36.0f) : 44.0f);
            auto backup = s.removeFromBottom (24.0f).withTrimmedBottom (6.0f);
            const auto b = model.latestBackup();
            pictograms::draw (g, b && model.writesUnlocked() ? Pict::lockOpen : Pict::lockClosed, backup.removeFromLeft (16.0f).withSizeKeepingCentre (14.0f, 14.0f),
                              t.scr.ink, 1.4f);
            backup.removeFromLeft (6.0f);
            if (b)
            {
                paint::lamp (g, { backup.getX() + 3.5f, backup.getCentreY() }, t.scr.hot, 7.0f);
                paint::text (g, Ep40Backup::describe (*b), backup.withTrimmedLeft (12.0f), HaloFonts::font (Role::bodyStrong, 12.0f), t.scr.ink);
            }
            else
            {
                paint::hollowLamp (g, { backup.getX() + 3.5f, backup.getCentreY() }, t.scr.dim, 7.0f);
                paint::text (g, view.available ? juce::String ("no complete backup") : juce::String (view.reason), backup.withTrimmedLeft (12.0f),
                             HaloFonts::font (Role::ui, 12.0f), t.scr.dim);
            }
            // Under an info or blocker notice, the plain explanation still reads (as in the mockup).
            if (! L.stacked && noticeView.get().kind != honest::NoticeKind::plain && session.hasSource())
                paint::paragraph (g, plainNote(), plainArea, HaloFonts::font (Role::ui, 12.0f), t.scr.dim, 2.0f);
            if (! captions.isEmpty())
            {
                paint::text (g, "ctrl e export" + dot + "ctrl enter send", captions, HaloFonts::font (Role::mono, 10.5f), t.scr.dim);
                const auto right = view.available ? ActionKey::captionFor (ActionKey::Variant::deviceWrite) : juce::String (view.reason);
                paint::text (g, right, captions, HaloFonts::font (Role::print, 10.5f), t.scr.dim, juce::Justification::centredRight);
            }
        }
    }

    //==========================================================================
    void ChopPage::paintLane (juce::Graphics& g, juce::Rectangle<float> r, bool compact) const
    {
        const auto& t = theme();
        const auto ui = HaloFonts::font (Role::ui, compact ? 11.0f : 11.5f);
        auto rows = r.reduced (0.0f, 4.0f);
        paint::text (g, "KEYS", rows.removeFromTop (14.0f), HaloFonts::font (Role::label), t.scr.dim);
        rows.removeFromTop (4.0f);
        paint::paragraph (g, "1" + enDash + "9 0 hear" + dot + "dbl-click add cut" + dot + "drag move" + dot + "del remove" + dot
                             + "ctrl+wheel zoom" + dot + "esc edit",
                          rows.removeFromTop (compact ? 30.0f : 36.0f), ui, t.scr.ink, 2.0f);
        paint::text (g, "ctrl e export" + dot + "ctrl enter send", rows.removeFromTop (16.0f), ui, t.scr.dim);
    }

    //==========================================================================
    std::array<std::optional<EncoderBinding>, 4> ChopPage::bindings()
    {
        std::array<std::optional<EncoderBinding>, 4> b;
        auto& s = session;

        // 1. SENSITIVITY: detection re-runs once per gesture (not per notch).
        {
            EncoderBinding e;
            e.label = "SENSITIVITY";
            e.getNorm = [&s] { return s.onsetParameters.sensitivity; };
            e.setNorm = [&s] (double v) { s.onsetParameters.sensitivity = v; s.changed(); };
            e.reset = [&s] { s.onsetParameters.sensitivity = chop::OnsetParameters().sensitivity; s.detect(); };
            e.step = 0.01;
            e.endGesture = [&s] { s.detect(); };
            e.format = [&s] { return juce::String (juce::roundToInt (s.onsetParameters.sensitivity * 100.0)); };
            e.unit = [] { return juce::String ("%"); };
            e.caption = [&s] { return juce::String (s.detectedCuts()) + " cuts seen"; };
            e.hint = [] { return "wheel " + plusMinus + "1%" + dot + "detects when you stop"; };
            e.available = [&s] { return s.hasSource() && ! s.evenSlices; };
            e.reason = [&s] { return s.evenSlices ? "even " + enDash + " no detection" : juce::String ("nothing open"); };
            e.pressLabel = "detect";
            e.press = [&s] { s.detect(); };
            e.shiftPressLabel = "clear cuts";
            e.shiftPress = [&s] { s.slices.clear(); s.selectedSlice.reset(); s.changed(); };
            e.gauge = [&s] (juce::Graphics& g, juce::Rectangle<float> r, juce::Colour c, bool) { gauges::tickDial (g, r, s.onsetParameters.sensitivity, c); };
            b[0] = e;
        }
        // 2. SLICES: fit 4 8 16 all.
        {
            EncoderBinding e;
            e.label = "SLICES";
            e.steps = 5;
            e.getNorm = [&s]
            {
                for (int i = 0; i < 5; ++i)
                    if (sliceChoices[(size_t) i] == s.sliceChoice)
                        return i / 4.0;
                return 0.0;
            };
            e.setNorm = [&s] (double v) { s.setSliceChoice (sliceChoices[(size_t) juce::jlimit (0, 4, (int) std::lround (v * 4.0))]); };
            e.reset = [&s] { s.setSliceChoice (EditSession::sliceFit); };
            e.format = [&s] { return juce::String (s.slices.sliceCount()); };
            e.caption = [] { return "fit" + dot + "4" + dot + "8" + dot + "16" + dot + "all"; };
            e.tag = [&s] { return s.sliceChoice == EditSession::sliceFit ? juce::String ("FIT") : juce::String(); };
            e.available = [&s] { return s.hasSource(); };
            e.reason = [] { return juce::String ("nothing open"); };
            e.pressLabel = "even";
            e.press = [&s] { s.setEvenSlices (! s.evenSlices); };
            e.gauge = [&s] (juce::Graphics& g, juce::Rectangle<float> r, juce::Colour c, bool)
            {
                gauges::dotRow (g, r, 12, juce::jmin (12, s.slices.sliceCount()), c); // the group's 12 pads
            };
            b[1] = e;
        }
        // 3. LENGTH: 1-20 s, 0.5 s steps; the EP-40's 20 s limit is the red tick.
        {
            EncoderBinding e;
            e.label = "LENGTH";
            constexpr double lo = EditSession::minChopSeconds, hi = maxSoundSeconds;
            e.getNorm = [&s] { return (s.chopSeconds - lo) / (hi - lo); };
            e.setNorm = [&s] (double v) { s.setChopSeconds (std::round ((lo + v * (hi - lo)) * 2.0) / 2.0); };
            e.reset = [&s] { s.setChopSeconds (hi); };
            e.step = 0.5 / (hi - lo);
            e.fine = 0.5 / (hi - lo);
            e.coarse = 5.0 / (hi - lo);
            e.format = [&s] { return juce::String (s.chopSeconds, 1); };
            e.unit = [] { return juce::String ("s"); };
            e.caption = [&s]
            {
                const auto plan = s.plan();
                if (plan.trimmedCount == 0)
                    return juce::String ("nothing cut");
                return (s.lengthIsLimit() && ! s.capLimitedBySpace() ? "limits " : "") + juce::String (plan.trimmedCount) + " slices cut";
            };
            e.tag = [] { return juce::String ("AUTO-TRIM"); };
            e.tagOn = [&s] { return s.autoTrim; };
            e.hint = [] { return "wheel " + plusMinus + "0.5 s" + dot + "ctrl " + plusMinus + "5"; };
            e.available = [&s] { return s.hasSource(); };
            e.reason = [] { return juce::String ("nothing open"); };
            e.pressLabel = "auto-trim";
            e.press = [&s] { s.setAutoTrim (! s.autoTrim); };
            e.gauge = [&s] (juce::Graphics& g, juce::Rectangle<float> r, juce::Colour c, bool)
            {
                gauges::ruler (g, r, (s.chopSeconds - lo) / (hi - lo), c, 1.0, "1 s", "20 s");
            };
            b[2] = e;
        }
        // 4. SLICE: select 1...N; press auditions; shift turn nudges its cut 10 ms.
        {
            EncoderBinding e;
            e.label = "SLICE";
            e.getNorm = [&s]
            {
                const int n = s.slices.sliceCount();
                return n > 1 && s.selectedSlice ? (double) *s.selectedSlice / (n - 1) : 0.0;
            };
            e.setNorm = [] (double) {};
            e.turn = [&s] (int detents, encoder::Speed speed)
            {
                const int n = s.slices.sliceCount();
                if (n == 0)
                    return;
                const int step = speed == encoder::Speed::coarse ? 4 : 1;
                s.selectedSlice = juce::jlimit (0, n - 1, s.selectedSlice.value_or (-1) + detents * step);
                s.changed();
            };
            e.reset = [&s] { s.selectedSlice.reset(); s.changed(); };
            e.format = [&s] { return s.selectedSlice ? juce::String (*s.selectedSlice + 1) : enDash; };
            e.unit = [&s] { return "of " + juce::String (s.slices.sliceCount()); };
            e.available = [&s] { return s.hasSource() && s.slices.sliceCount() > 0; };
            e.reason = [] { return juce::String ("no slices yet"); };
            e.pressLabel = "audition";
            e.press = [&s] { if (s.selectedSlice) s.auditionSlice (*s.selectedSlice); };
            e.pressAvailable = [&s] { return s.selectedSlice.has_value(); };
            e.shiftLabel = "NUDGE";
            e.shiftTurn = [&s] (int detents)
            {
                if (! s.selectedSlice || *s.selectedSlice == 0 || s.audio() == nullptr)
                    return;
                const int cut = *s.selectedSlice - 1;
                const int step = (int) std::lround (0.010 * s.audio()->sampleRate);
                s.slices.moveCut (cut, s.slices.cuts()[(size_t) cut] + detents * step, s.minGap());
                s.changed();
            };
            e.hint = [] { return "wheel: next slice" + dot + "shift: nudge its cut 10 ms"; };
            e.gauge = [&s] (juce::Graphics& g, juce::Rectangle<float> r, juce::Colour c, bool)
            {
                const int n = s.slices.sliceCount();
                gauges::timeline (g, r, n > 1 && s.selectedSlice ? (double) *s.selectedSlice / (n - 1) : 0.0, juce::jmax (1, juce::jmin (n - 1, 16)), c);
            };
            b[3] = e;
        }
        return b;
    }
}
