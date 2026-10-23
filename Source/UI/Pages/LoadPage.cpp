#include "UI/Pages/LoadPage.h"
#include "App/DevPose.h"
#include "App/HaloPaths.h"
#include "Audio/SampleIO.h"
#include "Core/Ep40MidiMapping.h"
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

        juce::String mb (juce::int64 bytes) { return juce::String ((double) bytes / (1024.0 * 1024.0), 1) + " mb"; }
        juce::String padName (int group, int grid)
        {
            return juce::String (ep40::groupLabels[(size_t) group].data()) + " " + juce::String (ep40::gridLabels[(size_t) grid].data());
        }
        juce::String soundTitle (const Ep40Library::Sound& s) { return s.name.isNotEmpty() ? s.name : "sound " + juce::String (s.slot); }
        bool isFailure (const juce::String& s)
        {
            return s.startsWith ("Assign failed") || s.startsWith ("Delete failed") || s.startsWith ("Couldn't") || s.contains ("can't be read");
        }
    }

    LoadPage::LoadPage (AppModel& m, WriteGate& g) : model (m), gate (g), lib (m.device())
    {
        setOpaque (false);
        setWantsKeyboardFocus (true);
        for (auto* c : std::initializer_list<juce::Component*> { &projects, &groups, &padGrid, &browser, &refreshKey, &auditionKey, &saveKey,
                                                                 &assignKey, &deleteKey })
            addAndMakeVisible (c);

        projects.setItems ({ "1", "2", "3", "4", "5", "6", "7", "8", "9" });
        projects.onSelect = [this] (int i) { pendingProject = i + 1; lib.readPads (i + 1); };
        groups.setItems ({ "a", "b", "c", "d" });
        groups.onSelect = [this] (int i) { group = i; refresh(); };

        padGrid.onPadClicked = [this] (int i)
        {
            targetPad = i;
            if (armed)
            {
                armAssign (false);
                confirmAssign (i);
                return;
            }
            // Clicking a pad selects (and plays) the sound on it.
            if (const int slot = lib.padSound (group, i); slot > 0)
            {
                selectSlot (slot);
                audition();
            }
            refresh();
        };

        browser.setEmptyText ("no sounds in this category");
        browser.onCategory = [this] (int i) { category = i - 1; refresh(); };
        browser.onSelect = [this] (int) { refresh(); };
        browser.onActivate = [this] (int) { audition(); };
        browser.paintFacts = [this] (juce::Graphics& g, juce::Rectangle<float> r) { paintFacts (g, r); };

        refreshKey.onClick = [this] { cache.clear(); lib.sync(); };
        auditionKey.onClick = [this] { audition(); };
        auditionKey.setTooltip ("Play the selected sound (space, or double-click it)");
        saveKey.onClick = [this] { saveToLibrary(); };
        saveKey.setTooltip ("Save a copy into Halo's library (CAPTURE)");
        assignKey.onClick = [this] { armAssign (! armed); };
        deleteKey.onClick = [this] { confirmDelete(); };

        lib.addChangeListener (this);
        model.backup().addChangeListener (this);
        model.addListener (this);
       #if JUCE_DEBUG
        if (DevPose::get ("armed") == 1)
            juce::Timer::callAfterDelay (2000, [this] { armedPose = true; armed = true; refresh(); }); // shots: writes stay locked
       #endif
        refresh();
    }

    LoadPage::~LoadPage()
    {
        lib.removeChangeListener (this);
        model.backup().removeChangeListener (this);
        model.removeListener (this);
    }

    juce::Rectangle<float> LoadPage::localF (const layout::Rect& r) const
    {
        return { r.x - frame.body.x, r.y - frame.body.y, r.w, r.h };
    }

    void LoadPage::setFrame (const layout::Frame& f)
    {
        frame = f;
        resized();
    }

    bool LoadPage::available() const
    {
        return (model.canTalkToDevice() || lib.isFixture()) && lib.state() != Ep40Library::State::idle;
    }

    juce::String LoadPage::unavailableText() const
    {
        // Today's copy. The mock source reads as "not connected" (owner: no
        // mock marking), like every other device-only control.
        if (model.isMock() || ! model.isConnected())
            return "Connect the EP-40 over USB. Halo reads its sounds and pads automatically.";
        if (! model.midi().canSend())
            return "The EP-40's MIDI output is in use by another app (a DAW or TE's tool). Close it and replug.";
        return "Waiting for the EP-40 to answer...";
    }

    void LoadPage::setStatus (const juce::String& s)
    {
        status = s;
        repaint();
    }

    //==========================================================================
    void LoadPage::rebuildRows()
    {
        const int keepSlot = selectedSound() != nullptr ? selectedSound()->slot : -1;
        visible.clear();
        const auto& sounds = lib.sounds();
        const auto& cats = lib.info().categories;
        std::vector<BrowserList::Row> rows;
        for (int i = 0; i < (int) sounds.size(); ++i)
        {
            const auto& s = sounds[(size_t) i];
            if (category >= 0 && category < (int) cats.size()
                && (s.slot < cats[(size_t) category].firstSlot || s.slot > cats[(size_t) category].lastSlot))
                continue;
            visible.push_back (i);
            rows.push_back ({ juce::String (s.slot).paddedLeft ('0', 3), soundTitle (s),
                              juce::String (s.seconds(), 2) + " s  " + (s.channels == 2 ? "st" : "m") });
        }
        browser.setRows (std::move (rows));
        int sel = -1;
        for (int r = 0; r < (int) visible.size(); ++r)
            if (sounds[(size_t) visible[(size_t) r]].slot == keepSlot)
                sel = r;
        // Something is always selected when there is something to select.
        browser.setSelected (sel < 0 && ! visible.empty() ? 0 : sel, sel < 0);
    }

    const Ep40Library::Sound* LoadPage::selectedSound() const
    {
        const int row = browser.getSelected();
        if (! juce::isPositiveAndBelow (row, (int) visible.size()) || visible[(size_t) row] >= (int) lib.sounds().size())
            return nullptr;
        return &lib.sounds()[(size_t) visible[(size_t) row]];
    }

    void LoadPage::selectSlot (int slot)
    {
        category = -1;
        rebuildRows();
        for (int r = 0; r < (int) visible.size(); ++r)
            if (lib.sounds()[(size_t) visible[(size_t) r]].slot == slot)
                browser.setSelected (r);
    }

    void LoadPage::refresh()
    {
        juce::StringArray cats { "ALL" };
        for (const auto& c : lib.info().categories)
            cats.add (c.name);
        browser.setCategories (cats);
        browser.setCategory (category + 1);
        rebuildRows();
        projects.setSelected (lib.activeProject() - 1);
        if (lib.activeProject() != shownProject)
            pendingProject = shownProject = lib.activeProject(); // the PROJECT encoder follows a read
        groups.setSelected (group);

        std::array<PadGrid::Pad, 12> pads {};
        for (int i = 0; i < 12; ++i)
        {
            const int slot = lib.padSound (group, i);
            auto& p = pads[(size_t) i];
            if (slot > 0)
            {
                const auto* s = lib.sound (slot);
                p.name = s != nullptr ? soundTitle (*s) : "sound " + juce::String (slot);
                p.corner = juce::String (slot);
                p.selected = selectedSound() != nullptr && selectedSound()->slot == slot;
            }
            else
                p.empty = slot == 0;
        }
        padGrid.setPads (pads);
        padGrid.setArmed (armed);

        const bool on = available();
        for (auto* c : std::initializer_list<juce::Component*> { &projects, &groups, &padGrid, &browser, &auditionKey, &saveKey, &assignKey, &deleteKey })
            c->setVisible (on);
        refreshKey.setEnabled (model.canTalkToDevice() && lib.state() != Ep40Library::State::syncing);
        refreshKey.setTooltip (lib.state() == Ep40Library::State::syncing ? "syncing..." : "Read the EP-40's library again");
        const bool has = selectedSound() != nullptr;
        auditionKey.setEnabled (has);
        saveKey.setEnabled (has);
        const bool busy = model.deviceWriter().isBusy();
        for (auto* k : { &assignKey, &deleteKey })
        {
            const auto base = k == &assignKey ? ActionKey::Variant::deviceWrite : ActionKey::Variant::destructive;
            k->setVariant (busy ? ActionKey::Variant::busy : model.writesUnlocked() ? base : ActionKey::Variant::lockedWrite);
            k->setEnabled (model.writesUnlocked() && has && ! busy);
        }
        assignKey.setLabel (armed ? "armed" : "assign to pad");
        if (armed && (! model.writesUnlocked() || ! has) && ! armedPose)
            armed = false;
        resized();
        repaint();
    }

    //==========================================================================
    void LoadPage::withAudio (int slot, std::function<void (std::shared_ptr<const AudioData>)> use)
    {
        if (const auto it = cache.find (slot); it != cache.end())
            return use (it->second);
        setStatus ("Downloading sound " + juce::String (slot) + "...");
        lib.download (slot, [this, slot, use] (std::optional<AudioData> audio, juce::String error)
        {
            if (! audio)
            {
                setStatus (error);
                return;
            }
            auto shared = std::make_shared<const AudioData> (std::move (*audio));
            cache[slot] = shared;
            setStatus ({});
            use (shared);
        });
    }

    void LoadPage::audition()
    {
        const auto* s = selectedSound();
        if (s == nullptr)
            return;
        auto& player = model.editor().player();
        if (player.isPlaying() && player.token() == 10000 + s->slot)
        {
            player.stop();
            repaint();
            return;
        }
        withAudio (s->slot, [this, slot = s->slot] (std::shared_ptr<const AudioData> audio)
        {
            model.editor().playOther (std::move (audio), 10000 + slot);
            repaint();
        });
    }

    void LoadPage::saveToLibrary()
    {
        const auto* s = selectedSound();
        if (s == nullptr)
            return;
        withAudio (s->slot, [this, slot = s->slot, name = s->name] (std::shared_ptr<const AudioData> audio)
        {
            const auto folder = HaloPaths::fromEp40();
            folder.createDirectory();
            const auto file = folder.getNonexistentChildFile (juce::String (slot).paddedLeft ('0', 3) + " "
                                                                  + (name.isNotEmpty() ? name : juce::String ("sound")), ".wav");
            if (SampleIO::writeWav16 (*audio, file))
            {
                model.library().importFiles ({ file.getFullPathName() });
                setStatus ("Saved " + file.getFileName() + " to the library (CAPTURE)");
            }
            else
                setStatus ("Couldn't write " + file.getFullPathName());
        });
    }

    juce::String LoadPage::armedInstruction() const
    {
        const auto* s = selectedSound();
        return s == nullptr ? juce::String()
                            : "click a pad to put \"" + soundTitle (*s) + "\" on it (project " + juce::String (lib.activeProject()) + ")";
    }

    void LoadPage::armAssign (bool on)
    {
        armed = on && selectedSound() != nullptr && model.writesUnlocked();
        refresh();
    }

    void LoadPage::confirmAssign (int gridIndex)
    {
        const auto* sound = selectedSound();
        if (sound == nullptr || ! model.writesUnlocked())
            return;
        // ASSIGN changes a pad on the device, so it asks first like every other
        // write (plan U20 / H3), naming the pad's current sound.
        const int project = lib.activeProject();
        const int targetGroup = group;
        const auto soundName = soundTitle (*sound);
        const int oldSlot = lib.padSound (targetGroup, gridIndex);
        const auto* old = oldSlot > 0 ? lib.sound (oldSlot) : nullptr;
        const auto oldName = old != nullptr && old->name.isNotEmpty() ? old->name : "sound " + juce::String (oldSlot);
        const auto pad = padName (targetGroup, gridIndex);

        WriteSpec spec;
        spec.kind = WriteSpec::Kind::assign;
        spec.confirmLabel = "assign";
        spec.body = "Put \"" + soundName + "\" on pad " + pad + " in project " + juce::String (project) + "? ";
        spec.body << (oldSlot > 0 ? "It currently plays \"" + oldName + "\". Nothing is deleted: \"" + oldName + "\" stays in the library."
                                  : juce::String ("The pad is empty now."));
        spec.from = soundName;
        spec.to = "pad " + pad.toLowerCase() + ", project " + juce::String (project);
        gate.request (spec, [this, project, targetGroup, gridIndex, slot = sound->slot, soundName, pad] (GatedWriter writer)
        {
            setStatus ("Assigning...");
            writer.assignPad (project, targetGroup, gridIndex, slot, [this, soundName, pad] (Ep40Writer::Result r)
            {
                setStatus (r.ok ? "\"" + soundName + "\" is now on pad " + pad : "Assign failed: " + r.message);
                refresh();
            });
            refresh();
        });
    }

    void LoadPage::confirmDelete()
    {
        const auto* s = selectedSound();
        if (s == nullptr || ! model.writesUnlocked())
            return;
        const int slot = s->slot;
        const auto name = s->name;
        WriteSpec spec;
        spec.kind = WriteSpec::Kind::deleteSound;
        spec.destructive = true;
        spec.confirmLabel = "delete";
        spec.body = "Delete sound " + juce::String (slot) + " \"" + name + "\" from the EP-40?" + juce::newLine + juce::newLine
                    + "Your Halo backup still has a copy. Pads that use it will be silent.";
        spec.from = "sound " + juce::String (slot);
        spec.to = "deleted";
        gate.request (spec, [this, slot, name] (GatedWriter writer)
        {
            setStatus ("Deleting...");
            cache.erase (slot);
            writer.deleteSound (slot, [this, name] (Ep40Writer::Result r)
            {
                setStatus (r.ok ? "Deleted \"" + name + "\"" : "Delete failed: " + r.message);
                refresh();
            });
            refresh();
        });
    }

    //==========================================================================
    juce::Rectangle<float> LoadPage::cancelWord() const
    {
        const auto head = localF (L.header);
        const float w = paint::textWidth (HaloFonts::face (HaloFonts::Face::interMedium, 14.0f), "cancel") + 12.0f;
        return { head.getRight() - (float) refreshKey.preferredWidth() - 20.0f - w, head.getY(), w, head.getHeight() };
    }

    void LoadPage::resized()
    {
        if (frame.width <= 0)
            return;
        L = layout::loadLayout (frame);
        const auto head = localF (L.header);
        const float rw = (float) refreshKey.preferredWidth();
        refreshKey.setBounds (juce::Rectangle<float> (head.getRight() - rw, head.getCentreY() - 17.0f, rw, 34.0f).toNearestInt());

        const auto keys = localF (L.keysRow);
        projects.setGap (L.keyGap);
        groups.setGap (L.keyGap);
        const float pw = 9.0f * L.projectKeyW + 8.0f * L.keyGap, gw = 4.0f * L.groupKeyW + 3.0f * L.keyGap;
        projects.setBounds (juce::Rectangle<float> (keys.getX(), keys.getY(), pw, keys.getHeight()).toNearestInt());
        groups.setBounds (juce::Rectangle<float> (keys.getRight() - gw, keys.getY(), gw, keys.getHeight()).toNearestInt());
        padGrid.setBounds (localF (L.pads).toNearestInt());

        auto list = localF (L.list);
        browser.setBounds (list.toNearestInt());
        const bool wide = list.getWidth() >= 800.0f;
        browser.setColumnWidths (wide ? 160.0f : 130.0f, wide ? 200.0f : 170.0f);

        auto actions = localF (L.actions);
        float x = actions.getX();
        for (auto* k : { &auditionKey, &saveKey, &assignKey })
        {
            k->setBounds (juce::Rectangle<float> (x, actions.getY(), (float) k->preferredWidth(), actions.getHeight()).toNearestInt());
            x += (float) k->preferredWidth() + 10.0f;
        }
        deleteKey.setBounds (actions.removeFromRight ((float) deleteKey.preferredWidth()).toNearestInt());
    }

    void LoadPage::mouseUp (const juce::MouseEvent& e)
    {
        if (armed && e.mouseWasClicked() && cancelWord().contains (e.position))
            armAssign (false);
        grabKeyboardFocus();
    }

    bool LoadPage::keyPressed (const juce::KeyPress& key)
    {
        if (! available())
            return false;
        const auto mods = key.getModifiers();
        if (key == juce::KeyPress::escapeKey && armed)                    { armAssign (false); return true; }
        if (! mods.isAnyModifierKeyDown() && key.getKeyCode() == juce::KeyPress::spaceKey) { audition(); return true; }
        if (key == juce::KeyPress::deleteKey)                             { confirmDelete(); return true; }
        if (key == juce::KeyPress::upKey)   { browser.moveSelection (-1); return true; }
        if (key == juce::KeyPress::downKey) { browser.moveSelection (1); return true; }
        return browser.keyPressed (key);
    }

    //==========================================================================
    void LoadPage::paintFacts (juce::Graphics& g, juce::Rectangle<float> r) const
    {
        const auto& t = theme();
        const auto* s = selectedSound();
        if (s == nullptr)
        {
            paint::text (g, "pick a sound", r.withHeight (BrowserList::rowHeight), HaloFonts::font (Role::ui, 12.0f), t.scr.dim);
            return;
        }
        // The thumbnail: a paper tile once the sound has been downloaded for
        // audition; until then an outline that says so.
        auto tile = r.removeFromTop (juce::jmin (96.0f, r.getHeight() * 0.42f));
        if (const auto it = cache.find (s->slot); it != cache.end() && it->second != nullptr && it->second->frameCount() > 0)
        {
            g.setColour (t.scr.paper);
            g.fillRoundedRectangle (tile, 3.0f);
            const auto mono = it->second->monoMixdown();
            const int cols = juce::jmax (1, (int) (tile.getWidth() - 12.0f) / 3);
            const auto summary = WaveformSummary::build (mono, cols);
            const float peak = juce::jmax (0.05f, summary.peakMagnitude());
            g.setColour (juce::Colour (0xff000005));
            for (int c = 0; c < summary.bucketCount(); ++c)
            {
                const float a = juce::jmax (std::abs (summary.minima[(size_t) c]), std::abs (summary.maxima[(size_t) c])) / peak;
                const float h = juce::jmax (1.0f, a * (tile.getHeight() - 14.0f) * 0.5f);
                g.fillRect (tile.getX() + 6.0f + (float) c * 3.0f, tile.getCentreY() - h, 2.0f, h * 2.0f);
            }
        }
        else
        {
            paint::dashedRect (g, tile.reduced (0.75f), t.scr.frame, 1.0f, 4.0f, 3.0f, 3.0f);
            paint::text (g, "not downloaded", tile, HaloFonts::font (Role::ui, 11.5f), t.scr.dim, juce::Justification::centred);
        }
        r.removeFromTop (10.0f);
        const auto big = HaloFonts::font (Role::bodyStrong, 13.0f);
        paint::text (g, paint::middleEllipsis (soundTitle (*s), big, r.getWidth()), r.removeFromTop (18.0f), big, t.scr.ink);
        const auto mono = HaloFonts::font (Role::mono, 11.0f);
        auto line = [&] (const juce::String& k, const juce::String& v)
        {
            auto row = r.removeFromTop (16.0f);
            paint::text (g, k, row, mono, t.scr.dim);
            paint::text (g, v, row, mono, t.scr.ink, juce::Justification::centredRight);
        };
        line ("slot", juce::String (s->slot).paddedLeft ('0', 3));
        line ("length", juce::String (s->seconds(), 2) + " s");
        line ("channels", s->channels == 2 ? "stereo" : "mono");
        line ("rate", juce::String (s->sampleRate / 1000.0, 3) + " khz");
        line ("size", juce::String (formatBytes ((long long) s->bytes)).toLowerCase());
        // Which pads in this project use it.
        juce::StringArray uses;
        for (int gi = 0; gi < 4; ++gi)
            for (int i = 0; i < 12; ++i)
                if (lib.padSound (gi, i) == s->slot)
                    uses.add (padName (gi, i).toLowerCase());
        if (! uses.isEmpty())
        {
            r.removeFromTop (6.0f);
            paint::paragraph (g, "on pad " + uses.joinIntoString (", ") + " (project " + juce::String (lib.activeProject()) + ")", r,
                              HaloFonts::font (Role::ui, 11.5f), t.scr.dim, 2.0f);
        }
    }

    void LoadPage::paint (juce::Graphics& g)
    {
        if (frame.width <= 0)
            return;
        const auto& t = theme();
        const auto& info = lib.info();
        const bool on = available();

        // ---- Header ----
        {
            auto head = localF (L.header);
            auto icon = head.removeFromLeft (head.getHeight() - 6.0f).withSizeKeepingCentre (34.0f, 34.0f);
            g.setColour (t.scr.ink.withAlpha (0.85f));
            g.drawRoundedRectangle (icon.reduced (0.6f), 3.0f, 1.2f);
            pictograms::draw (g, Pict::device, icon.reduced (8.0f), t.scr.ink, 1.4f);
            head.removeFromLeft (14.0f);
            head.removeFromRight ((float) refreshKey.preferredWidth() + 20.0f);
            paint::text (g, "load", head.withHeight (14.0f), HaloFonts::font (Role::ui, 11.5f), t.scr.dim);
            auto titleRow = head.withTrimmedTop (14.0f);
            const auto titleFont = HaloFonts::font (Role::title);
            const juce::String title = "EP" + enDash + "40 LIBRARY";
            const float titleW = paint::textWidth (titleFont, title) + 4.0f;
            paint::text (g, title, titleRow.withWidth (titleW), titleFont, t.scr.ink);
            auto meta = titleRow.withTrimmedLeft (titleW + 20.0f);
            if (armed)
            {
                // Persistent while armed (turning an encoder never hides it).
                const auto tag = paint::tag (g, { meta.getX(), meta.getCentreY() - 10.0f }, "ASSIGN ARMED", paint::TagStyle::filled, t.scr.hot,
                                             juce::Colour (0xff000005));
                meta.removeFromLeft (tag.getWidth() + 10.0f);
                const auto cancel = cancelWord();
                meta = meta.withRight (cancel.getX() - 8.0f);
                paint::text (g, paint::middleEllipsis (armedInstruction(), HaloFonts::font (Role::ui, 12.5f), meta.getWidth()), meta,
                             HaloFonts::font (Role::ui, 12.5f), t.scr.ink);
                paint::text (g, "cancel", cancel, HaloFonts::face (HaloFonts::Face::interMedium, 14.0f), t.scr.dim, juce::Justification::centred);
            }
            else if (lib.state() == Ep40Library::State::syncing)
            {
                // The sync's steps as a dot timeline, then the step in words.
                static const juce::StringArray steps { "hello", "file system", "storage", "listing", "reading" };
                int step = 0;
                const auto p = lib.progress().toLowerCase();
                for (int i = 0; i < steps.size(); ++i)
                    if (p.contains (steps[i]))
                        step = i;
                paint::dotTimeline (g, meta.withWidth (60.0f).withSizeKeepingCentre (60.0f, 8.0f), 5, step, 1, false, 6.0f);
                paint::text (g, lib.progress(), meta.withTrimmedLeft (72.0f), HaloFonts::font (Role::mono, 11.5f), t.scr.ink);
            }
            else if (lib.state() == Ep40Library::State::failed)
            {
                const auto r = meta.withSizeKeepingCentre (meta.getWidth(), 24.0f);
                g.setColour (t.honest.danger);
                g.drawRoundedRectangle (r.reduced (0.75f), 3.0f, 1.5f);
                pictograms::draw (g, Pict::triangle, { r.getX() + 6.0f, r.getCentreY() - 6.0f, 12.0f, 12.0f }, t.honest.danger, 1.3f);
                paint::text (g, paint::middleEllipsis (lib.error(), HaloFonts::font (Role::ui, 12.0f), r.getWidth() - 32.0f), r.withTrimmedLeft (24.0f),
                             HaloFonts::font (Role::ui, 12.0f), t.scr.ink);
            }
            else if (on)
            {
                paint::text (g, "os " + info.os.toLowerCase() + dot + juce::String ((int) lib.sounds().size()) + " sounds" + dot + "project "
                                    + juce::String (lib.activeProject()),
                             meta, HaloFonts::font (Role::ui, 12.0f), t.scr.dim);
            }
            else
                paint::text (g, "nothing to read", meta, HaloFonts::font (Role::ui, 12.0f), t.scr.dim);
        }

        if (! on)
        {
            // One centred card with a 64 px pictogram and today's copy.
            const auto body = getLocalBounds().toFloat().withTrimmedTop (localF (L.header).getBottom() + 12.0f);
            const float cardW = juce::jmin (560.0f, body.getWidth() - 40.0f);
            const float textH = paint::paragraphHeight (unavailableText(), HaloFonts::font (Role::body, 14.0f), cardW - 56.0f, 4.0f);
            const auto card = body.withSizeKeepingCentre (cardW, juce::jmin (24.0f + 64.0f + 18.0f + 16.0f + 6.0f + textH + 26.0f, body.getHeight() - 20.0f))
                                  .translated (0.0f, -20.0f);
            g.setColour (t.scr.panel);
            g.fillRoundedRectangle (card, 6.0f);
            g.setColour (t.scr.frame);
            g.drawRoundedRectangle (card.reduced (0.5f), 6.0f, 1.0f);
            auto inner = card.reduced (28.0f, 24.0f);
            const bool portBusy = model.isConnected() && ! model.isMock() && ! model.midi().canSend();
            const bool waiting = model.isConnected() && ! model.isMock() && ! portBusy;
            auto pictRow = inner.removeFromTop (64.0f);
            pictograms::draw (g, portBusy ? Pict::triangle : waiting ? Pict::device : Pict::usb, pictRow.withWidth (64.0f),
                              portBusy ? t.honest.danger : t.scr.ink, 2.0f);
            if (waiting && (juce::Time::getMillisecondCounter() / 500) % 2 == 0)
                paint::hollowLamp (g, { pictRow.getX() + 84.0f, pictRow.getCentreY() }, t.scr.ink, 9.0f);
            inner.removeFromTop (18.0f);
            paint::text (g, "EP" + enDash + "40 LIBRARY", inner.removeFromTop (16.0f), HaloFonts::font (Role::label), t.scr.dim);
            inner.removeFromTop (6.0f);
            paint::paragraph (g, unavailableText(), inner, HaloFonts::font (Role::body, 14.0f), t.scr.ink, 4.0f);
            return;
        }

        // ---- Storage: 61 dots and the numbers ----
        {
            auto row = localF (L.storage);
            if (info.capacityBytes > 0 && info.freeBytes >= 0)
            {
                const double used = 1.0 - (double) info.freeBytes / (double) info.capacityBytes;
                const bool full = used > 0.9;
                const float dotsW = juce::jmin (420.0f, row.getWidth() * 0.4f);
                paint::dotTimeline (g, row.removeFromLeft (dotsW).withSizeKeepingCentre (dotsW, 8.0f), 61, juce::roundToInt (used * 61.0), 0, full, 6.0f);
                row.removeFromLeft (16.0f);
                const auto text = mb (info.capacityBytes - info.freeBytes) + " used" + dot + mb (info.freeBytes) + " free of " + mb (info.capacityBytes);
                paint::text (g, text, row, HaloFonts::font (Role::mono, 11.5f), t.scr.ink);
                if (full)
                {
                    const float x = row.getX() + paint::textWidth (HaloFonts::font (Role::mono, 11.5f), text) + 16.0f;
                    const auto r = juce::Rectangle<float> (x, row.getY(), row.getRight() - x, row.getHeight());
                    g.setColour (t.honest.danger);
                    g.drawRoundedRectangle (r.reduced (0.75f), 3.0f, 1.5f);
                    pictograms::draw (g, Pict::triangle, { r.getX() + 6.0f, r.getCentreY() - 6.0f, 12.0f, 12.0f }, t.honest.danger, 1.3f);
                    paint::text (g, "The EP-40 is over 90% full: delete sounds to make room.", r.withTrimmedLeft (24.0f), HaloFonts::font (Role::ui, 12.0f), t.scr.ink);
                }
            }
            else
                paint::text (g, "storage not read yet", row, HaloFonts::font (Role::ui, 12.0f), t.scr.dim);
        }

        // ---- Captions for the key row (none needed: digits and letters read as such) ----
        const auto keys = localF (L.keysRow);
        paint::hairlineV (g, (float) groups.getX() - 7.5f, keys.getY() + 4.0f, keys.getBottom() - 4.0f, t.scr.rule);

        // ---- Status line and the locked line ----
        {
            auto statusRow = localF (L.status);
            if (status.isNotEmpty())
            {
                const bool failed = isFailure (status);
                if (failed)
                {
                    g.setColour (t.honest.danger);
                    g.drawRoundedRectangle (statusRow.reduced (0.5f), 3.0f, 1.0f);
                    pictograms::draw (g, Pict::triangle, { statusRow.getX() + 5.0f, statusRow.getCentreY() - 6.0f, 12.0f, 12.0f }, t.honest.danger, 1.3f);
                    paint::text (g, status, statusRow.withTrimmedLeft (22.0f), HaloFonts::font (Role::ui, 12.0f), t.scr.ink);
                }
                else
                {
                    const bool result = ! status.endsWith ("...");
                    if (result)
                        paint::lamp (g, { statusRow.getX() + 4.0f, statusRow.getCentreY() }, t.scr.ink, 7.0f);
                    paint::text (g, status, statusRow.withTrimmedLeft (result ? 14.0f : 0.0f), HaloFonts::font (Role::mono, 11.5f), t.scr.ink);
                }
            }
            if (! model.writesUnlocked())
            {
                // Inline (no tooltip needed): what unlocks assign and delete.
                const auto actions = localF (L.actions);
                const float x = (float) assignKey.getRight() + 16.0f;
                const auto r = juce::Rectangle<float> (x, actions.getY(), (float) deleteKey.getX() - 16.0f - x, actions.getHeight());
                pictograms::draw (g, Pict::lockClosed, r.withWidth (14.0f).withSizeKeepingCentre (14.0f, 14.0f), t.scr.dim, 1.4f);
                paint::text (g, model.canTalkToDevice()
                                    ? "back up the ep" + enDash + "40 in backups (ctrl 6) to unlock"
                                    : "connect the ep" + enDash + "40 to assign or delete",
                             r.withTrimmedLeft (22.0f), HaloFonts::font (Role::ui, 12.0f), t.scr.dim);
            }
        }
    }

    void LoadPage::paintLane (juce::Graphics& g, juce::Rectangle<float> r, bool) const
    {
        const auto& t = theme();
        auto rows = r.reduced (0.0f, 4.0f);
        paint::text (g, "KEYS", rows.removeFromTop (14.0f), HaloFonts::font (Role::label), t.scr.dim);
        rows.removeFromTop (6.0f);
        const auto ui = HaloFonts::font (Role::ui, 11.5f);
        if (armed)
        {
            paint::text (g, armedInstruction() + dot + "esc cancel", rows.removeFromTop (16.0f), ui, t.scr.ink);
            return;
        }
        paint::text (g, "space audition" + dot + "up / down sound" + dot + "double-click plays", rows.removeFromTop (16.0f), ui, t.scr.ink);
        paint::text (g, "click a pad: play its sound" + dot + "delete key: delete (asks first)", rows.removeFromTop (16.0f), ui, t.scr.dim);
    }

    //==========================================================================
    std::array<std::optional<EncoderBinding>, 4> LoadPage::bindings()
    {
        std::array<std::optional<EncoderBinding>, 4> out;
        auto live = [this] { return available(); };
        auto why = [this] { return juce::String (model.isMock() || ! model.isConnected() ? "no ep" + enDash + "40" : "not read yet"); };

        EncoderBinding cat;
        cat.label = "CATEGORY";
        cat.steps = 0;
        cat.getNorm = [this] { const int n = (int) lib.info().categories.size(); return n > 0 ? (double) (category + 1) / n : 0.0; };
        cat.setNorm = [this] (double v) { category = juce::roundToInt (v * (double) lib.info().categories.size()) - 1; refresh(); };
        cat.turn = [this] (int d, encoder::Speed) { category = juce::jlimit (-1, (int) lib.info().categories.size() - 1, category + d); refresh(); };
        cat.format = [this] { return category < 0 || category >= (int) lib.info().categories.size() ? juce::String ("all")
                                                                                                     : lib.info().categories[(size_t) category].name.toLowerCase(); };
        cat.caption = [this] { return juce::String ((int) visible.size()) + " sounds"; };
        cat.pressLabel = "all";
        cat.press = [this] { category = -1; refresh(); };
        cat.reset = cat.press;
        cat.available = live;
        cat.reason = why;
        out[0] = cat;

        EncoderBinding snd;
        snd.label = "SOUND";
        snd.getNorm = [this] { const int n = browser.rowCount(); return n > 1 ? (double) juce::jmax (0, browser.getSelected()) / (n - 1) : 0.0; };
        snd.setNorm = [this] (double v) { browser.setSelected (juce::roundToInt (v * (browser.rowCount() - 1))); refresh(); };
        snd.turn = [this] (int d, encoder::Speed sp) { browser.moveSelection (d * (sp == encoder::Speed::coarse ? 10 : 1)); };
        snd.format = [this] { const auto* s = selectedSound(); return s != nullptr ? juce::String (s->slot).paddedLeft ('0', 3) : enDash; };
        snd.caption = [this] { const auto* s = selectedSound(); return s != nullptr ? soundTitle (*s).toLowerCase() : juce::String ("none"); };
        snd.pressLabel = "audition";
        snd.press = [this] { audition(); };
        snd.pressAvailable = [this] { return selectedSound() != nullptr; };
        snd.available = live;
        snd.reason = why;
        snd.hint = [] { return juce::String ("wheel: one sound" + dot + "ctrl: ten"); };
        out[1] = snd;

        EncoderBinding prj;
        prj.label = "PROJECT";
        prj.steps = 9;
        prj.getNorm = [this] { return (pendingProject - 1) / 8.0; };
        prj.setNorm = [this] (double v) { pendingProject = 1 + juce::roundToInt (v * 8.0); };
        prj.turn = [this] (int d, encoder::Speed) { pendingProject = juce::jlimit (1, 9, pendingProject + d); };
        prj.format = [this] { return juce::String (pendingProject); };
        prj.caption = [this] { return pendingProject == lib.activeProject() ? juce::String ("showing") : juce::String ("press to read"); };
        prj.gauge = [this] (juce::Graphics& g, juce::Rectangle<float> r, juce::Colour c, bool) { gauges::dotRow (g, r, 9, pendingProject, c); };
        prj.pressLabel = "read pads";
        prj.press = [this] { lib.readPads (pendingProject); };
        prj.available = live;
        prj.reason = why;
        out[2] = prj;

        EncoderBinding pad;
        pad.label = "PAD";
        pad.steps = 12;
        pad.getNorm = [this] { return targetPad / 11.0; };
        pad.setNorm = [this] (double v) { targetPad = juce::roundToInt (v * 11.0); };
        pad.turn = [this] (int d, encoder::Speed) { targetPad = juce::jlimit (0, 11, targetPad + d); };
        pad.format = [this] { return juce::String (ep40::gridLabels[(size_t) targetPad].data()).toLowerCase(); };
        pad.caption = [this] { return "group " + juce::String (ep40::groupLabels[(size_t) group].data()).toLowerCase(); };
        pad.pressLabel = "assign";
        pad.press = [this] { confirmAssign (targetPad); };
        pad.pressAvailable = [this] { return model.writesUnlocked() && selectedSound() != nullptr; };
        pad.available = [this] { return available() && model.writesUnlocked(); };
        pad.reason = [this] { return model.canTalkToDevice() ? juce::String ("back up first (ctrl 6)") : juce::String ("no ep" + enDash + "40"); };
        out[3] = pad;
        return out;
    }
}
