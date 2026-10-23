#include "UI/Pages/CapturePage.h"
#include "App/DevPose.h"
#include "Audio/SampleIO.h"
#include "Audio/SessionRecorder.h"
#include "Core/EncoderMath.h"
#include "Core/GrabMath.h"
#include "UI/Design/Numerals.h"
#include "UI/Design/Paint.h"
#include "UI/Design/Pictograms.h"
#include "UI/Design/Primitives/Gauges.h"
#include "UI/Design/Primitives/Progress.h"
#include "UI/Pages/PlayEncoders.h"

namespace halo
{
    namespace
    {
        const juce::String dot = juce::String::fromUTF8 (" \xc2\xb7 ");
        const juce::String enDash = juce::String::charToString ((juce::juce_wchar) 0x2013);
        const juce::Colour black { 0xff000005 };

        juce::String duration (double s)
        {
            const int whole = (int) s;
            return s < 60.0 ? juce::String (s, 1) + " s" : juce::String (whole / 60) + ":" + juce::String (whole % 60).paddedLeft ('0', 2);
        }
        // 00:12.4: the counter.
        juce::String counter (double s)
        {
            const int whole = (int) s;
            return juce::String (whole / 60).paddedLeft ('0', 2) + ":" + juce::String (whole % 60).paddedLeft ('0', 2) + "."
                 + juce::String ((int) ((s - whole) * 10.0));
        }
        int litFor (float db, int segments)
        {
            return std::isfinite (db) ? juce::jlimit (0, segments, (int) std::floor ((db + 48.0f) / 48.0f * (float) segments)) : 0;
        }
    }

    //==========================================================================
    // The library list: 40 px rows with a kind tag, a lazy thumbnail, the
    // title and the facts. Double-click / enter opens the item in EDIT.
    class CapturePage::List : public juce::Component
    {
    public:
        explicit List (CapturePage& p) : page (p) { setWantsKeyboardFocus (true); }

        int selected = -1, first = 0;
        std::function<void()> onSelect, onOpen;

        int visibleRows() const { return juce::jmax (1, (int) std::floor ((float) getHeight() / layout::CaptureLayout::rowHeight)); }
        void ensureVisible (int row)
        {
            if (row < first) first = row;
            else if (row >= first + visibleRows()) first = row - visibleRows() + 1;
            clampScroll();
        }
        void clampScroll() { first = juce::jlimit (0, juce::jmax (0, (int) page.view.size() - visibleRows()), first); }
        void select (int row)
        {
            row = juce::jlimit (-1, (int) page.view.size() - 1, row);
            if (row == selected)
                return;
            selected = row;
            if (row >= 0)
                ensureVisible (row);
            repaint();
            if (onSelect)
                onSelect();
        }
        void move (int delta)
        {
            if (! page.view.empty())
                select (juce::jlimit (0, (int) page.view.size() - 1, (selected < 0 ? -1 : selected) + delta));
        }

        void paint (juce::Graphics& g) override
        {
            const auto& t = theme();
            const auto& items = page.model.library().items();
            const float rowH = layout::CaptureLayout::rowHeight;
            const auto mono = HaloFonts::font (Role::mono, 11.5f);
            const auto name = HaloFonts::font (Role::ui, 13.0f);
            const int last = juce::jmin ((int) page.view.size(), first + visibleRows());
            for (int i = first; i < last; ++i)
            {
                const auto& item = items[(size_t) page.view[(size_t) i]];
                auto r = juce::Rectangle<float> (0.0f, (float) (i - first) * rowH, (float) getWidth() - 10.0f, rowH);
                const bool on = i == selected;
                if (on)
                {
                    g.setColour (t.scr.ramp[7]);
                    g.fillRoundedRectangle (r.reduced (0.0f, 2.0f), 2.5f);
                    if (hasKeyboardFocus (false))
                    {
                        g.setColour (t.scr.hot);
                        g.drawRoundedRectangle (r.reduced (0.5f, 2.5f), 2.5f, 1.0f);
                    }
                }
                else
                    paint::hairlineH (g, r.getX(), r.getRight(), r.getBottom() - 0.5f, t.scr.rule);
                const auto ink = on ? black : t.scr.ink;
                auto inner = r.reduced (10.0f, 0.0f);

                // Kind tag: GRAB outlined ochre, TAKE ink, FILE outline.
                auto tagArea = inner.removeFromLeft (58.0f);
                const auto word = item.kind == Library::Item::Kind::grab ? juce::String ("GRAB")
                                : item.kind == Library::Item::Kind::take ? juce::String ("TAKE") : juce::String ("FILE");
                const auto tl = juce::Point<float> (tagArea.getX(), tagArea.getCentreY() - 9.0f);
                if (item.kind == Library::Item::Kind::take)
                    paint::tag (g, tl, word, paint::TagStyle::filled, on ? black : t.scr.ink, on ? t.scr.ramp[7] : black, 18.0f);
                else
                    paint::tag (g, tl, word, paint::TagStyle::outline, item.kind == Library::Item::Kind::grab ? t.encoder (1).line : (on ? black : t.scr.frame),
                                item.kind == Library::Item::Kind::grab ? (on ? black : t.encoder (1).line) : (on ? black : t.scr.dim), 18.0f);

                // 60 x 18 stepped thumbnail, or an empty outline until ready.
                const auto thumb = inner.removeFromLeft (60.0f).withSizeKeepingCentre (60.0f, 18.0f);
                inner.removeFromLeft (14.0f);
                if (const auto* peaks = page.model.library().peaks (item.file); peaks != nullptr && ! peaks->empty())
                {
                    float peak = 0.05f;
                    for (auto p : *peaks) peak = juce::jmax (peak, p);
                    g.setColour (on ? black : t.scr.ramp[5]);
                    const float w = thumb.getWidth() / (float) peaks->size();
                    for (size_t k = 0; k < peaks->size(); ++k)
                    {
                        const float h = juce::jmax (1.0f, std::pow ((*peaks)[k] / peak, 0.6f) * thumb.getHeight());
                        g.fillRect (thumb.getX() + (float) k * w, thumb.getCentreY() - h * 0.5f, juce::jmax (1.0f, w - 1.0f), h);
                    }
                }
                else
                {
                    g.setColour (on ? black.withAlpha (0.3f) : t.scr.frame);
                    g.drawRect (thumb, 1.0f);
                }

                const juce::String facts = duration (item.seconds) + dot + juce::String (item.sampleRate / 1000.0, 1) + "k " + (item.channels == 1 ? "m" : "st");
                const float fw = paint::textWidth (mono, facts) + 4.0f;
                paint::text (g, facts, inner.removeFromRight (fw), mono, on ? black.withAlpha (0.6f) : t.scr.dim, juce::Justification::centredRight);
                inner.removeFromRight (12.0f);
                paint::text (g, paint::middleEllipsis (item.title, name, inner.getWidth()), inner, name, ink);
            }
            if ((int) page.view.size() > visibleRows())
            {
                const float frac = (float) visibleRows() / (float) page.view.size();
                const float h = juce::jmax (16.0f, (float) getHeight() * frac);
                const float y = ((float) getHeight() - h) * (float) first / (float) juce::jmax (1, (int) page.view.size() - visibleRows());
                g.setColour (t.scr.ramp[3]);
                g.fillRect (juce::Rectangle<float> ((float) getWidth() - 4.0f, y, 3.0f, h));
            }
        }

        int rowAt (float y) const
        {
            const int i = first + (int) (y / layout::CaptureLayout::rowHeight);
            return juce::isPositiveAndBelow (i, (int) page.view.size()) ? i : -1;
        }
        void mouseDown (const juce::MouseEvent& e) override { if (const int i = rowAt (e.position.y); i >= 0) select (i); }
        void mouseDoubleClick (const juce::MouseEvent& e) override
        {
            if (rowAt (e.position.y) >= 0 && onOpen)
                onOpen();
        }
        void mouseWheelMove (const juce::MouseEvent&, const juce::MouseWheelDetails& w) override
        {
            acc += (std::abs (w.deltaY) >= std::abs (w.deltaX) ? w.deltaY : -w.deltaX) * (w.isReversed ? -1.0f : 1.0f);
            const float notch = (float) encoder::kNotch;
            int d = 0;
            while (std::abs (acc) >= notch * 0.85f)
            {
                const int dir = acc > 0 ? 1 : -1;
                acc -= (float) dir * notch;
                d += dir;
            }
            if (d == 0)
                return;
            first -= d * 2;
            clampScroll();
            repaint();
        }
        bool keyPressed (const juce::KeyPress& k) override
        {
            if (k == juce::KeyPress::upKey)       { move (-1); return true; }
            if (k == juce::KeyPress::downKey)     { move (1); return true; }
            if (k == juce::KeyPress::pageUpKey)   { move (-visibleRows()); return true; }
            if (k == juce::KeyPress::pageDownKey) { move (visibleRows()); return true; }
            if (k == juce::KeyPress::returnKey && onOpen) { onOpen(); return true; }
            return false;
        }
        void focusGained (FocusChangeType) override { repaint(); }
        void focusLost (FocusChangeType) override { repaint(); }

    private:
        CapturePage& page;
        float acc = 0.0f;
    };

    //==========================================================================
    CapturePage::CapturePage (AppModel& m) : model (m)
    {
        setOpaque (false);
        setWantsKeyboardFocus (true);
        list = std::make_unique<List> (*this);
        for (auto* c : std::initializer_list<juce::Component*> { list.get(), &search, &kinds, &sorts, &grabLengths, &importKey, &folderKey,
                                                                 &recKey, &monitorKey, &grabKey, &showKey, &editKey, &chopKey, &forgetKey })
            addAndMakeVisible (c);

        const auto& t = theme();
        search.setTextToShowWhenEmpty ("search (ctrl f)", t.scr.dim);
        search.setFont (HaloFonts::font (Role::ui, 13.0f));
        search.setColour (juce::TextEditor::backgroundColourId, t.scr.panel);
        search.setColour (juce::TextEditor::textColourId, t.scr.ink);
        search.setColour (juce::TextEditor::outlineColourId, t.scr.frame);
        search.setColour (juce::TextEditor::focusedOutlineColourId, t.scr.hot);
        search.setColour (juce::CaretComponent::caretColourId, t.scr.hot);
        search.setIndents (10, 6);
        search.onTextChange = [this] { rebuildView(); };
        search.onEscapeKey = [this] { search.clear(); rebuildView(); list->grabKeyboardFocus(); };
        search.onReturnKey = [this] { list->grabKeyboardFocus(); };

        kinds.setItems ({ "all", "takes", "grabs", "files" });
        kinds.setSelected (0);
        kinds.onSelect = [this] (int i) { filter = (Library::Filter) i; rebuildView(); };
        sorts.setItems ({ "date", "name", "length" });
        sorts.setSelected (0);
        sorts.onSelect = [this] (int i) { sort = (Library::Sort) i; rebuildView(); };
        grabLengths.onSelect = [this] (int i)
        {
            model.setGrabLength (model.grabUsesBars() ? grab::barChoices[(size_t) i] : grab::secondsChoices[(size_t) i]);
        };

        list->onSelect = [this] { refresh(); };
        list->onOpen = [this] { openSelected (false); };
        importKey.onClick = [this] { import(); };
        importKey.setTooltip ("Import audio files into the library (they stay where they are)");
        folderKey.onClick = [] { SessionRecorder::recordingsFolder().revealToUser(); };
        folderKey.setTooltip ("Show the recordings folder");
        recKey.onClick = [this] { model.toggleRecording(); };
        monitorKey.onClick = [this] { model.toggleMonitor(); };
        grabKey.onClick = [this] { model.grabNow(); };
        grabKey.setTooltip ("GRAB (ctrl g): keep the last bars (with a clock) or seconds");
        showKey.onClick = [this] { if (const auto& take = model.lastTake()) take->file.revealToUser(); };
        editKey.onClick = [this] { openSelected (false); };
        chopKey.onClick = [this] { openSelected (true); };
        forgetKey.onClick = [this] { if (const auto* item = selectedItem()) model.library().forget (item->file); };
        forgetKey.setTooltip ("Forget this imported file (the file itself stays)");

        model.library().addChangeListener (this);
        model.addListener (this);
        rebuildView();
        updateTimer();
    }

    CapturePage::~CapturePage()
    {
        *alive = false;
        decodePool.removeAllJobs (true, 4000);
        model.library().removeChangeListener (this);
        model.removeListener (this);
    }

    juce::Rectangle<float> CapturePage::localF (const layout::Rect& r) const
    {
        return { r.x - frame.body.x, r.y - frame.body.y, r.w, r.h };
    }

    void CapturePage::setFrame (const layout::Frame& f)
    {
        frame = f;
        resized();
    }

    //==========================================================================
    void CapturePage::rebuildView()
    {
        const auto* before = selectedItem();
        const auto keep = before != nullptr ? before->file : juce::File();
        view = model.library().view (filter, sort, search.getText().trim());
        int sel = -1;
        for (int i = 0; i < (int) view.size(); ++i)
            if (model.library().items()[(size_t) view[(size_t) i]].file == keep)
                sel = i;
        list->selected = -1;
        list->clampScroll();
        list->select (sel < 0 && ! view.empty() ? 0 : sel);
        refresh();
    }

    const Library::Item* CapturePage::selectedItem() const
    {
        const int row = list != nullptr ? list->selected : -1;
        const auto& items = model.library().items();
        if (! juce::isPositiveAndBelow (row, (int) view.size()) || ! juce::isPositiveAndBelow (view[(size_t) row], (int) items.size()))
            return nullptr;
        return &items[(size_t) view[(size_t) row]];
    }

    void CapturePage::refresh()
    {
        // The library changed under the view (a new take, an import, forget).
        int valid = 0;
        for (auto i : view)
            valid += juce::isPositiveAndBelow (i, (int) model.library().items().size()) ? 1 : 0;
        if (valid != (int) view.size() || (view.empty() && ! model.library().items().empty()))
        {
            view = model.library().view (filter, sort, search.getText().trim());
            list->clampScroll();
            if (! juce::isPositiveAndBelow (list->selected, (int) view.size()))
            {
                list->selected = -1;
                if (! view.empty())
                    list->select (0); // calls back into refresh() with a valid selection
                return;
            }
        }
        const auto& engine = model.monitor();
        const bool running = engine.isRunning();
        recKey.setEnabled (running);
        recKey.setLabel (engine.isRecording() ? "stop" : "rec");
        recKey.setIcon (engine.isRecording() ? Pict::stop : Pict::rec);
        recKey.setTooltip (running ? juce::String ("record (ctrl r)") : juce::String ("rec needs monitor on (ctrl m)"));
        monitorKey.setLabel (running ? "monitor on" : "monitor");
        monitorKey.setEnabled (running || engine.ep40InputName().isNotEmpty());
        grabKey.setEnabled (running && ! model.grabber().isBusy());
        const bool bars = model.grabUsesBars();
        juce::StringArray items;
        int sel = 1;
        for (int i = 0; i < 3; ++i)
        {
            const int v = bars ? grab::barChoices[(size_t) i] : grab::secondsChoices[(size_t) i];
            items.add (juce::String (v) + (bars ? " bars" : " s"));
            if (v == (bars ? model.grabber().bars : model.grabber().seconds))
                sel = i;
        }
        grabLengths.setItems (items);
        grabLengths.setSelected (sel);
        showKey.setVisible (model.lastTake().has_value());
        updateTimer();

        const auto* item = selectedItem();
        editKey.setEnabled (item != nullptr);
        chopKey.setEnabled (item != nullptr);
        forgetKey.setVisible (item != nullptr && item->kind == Library::Item::Kind::imported);
        const bool empty = model.library().items().empty();
        list->setVisible (! empty);
        list->repaint();
        repaint();
    }

    void CapturePage::updateTimer()
    {
        // Moving parts run only while something moves: the route's meters,
        // the REC lamp's pulse, or reels still coasting. Idle = no timer.
        const bool moving = model.monitor().isRunning() || reels.speed != 0.0;
        if (moving && ! isTimerRunning())
            startTimerHz (30);
        else if (! moving)
            stopTimer();
    }

    void CapturePage::timerCallback()
    {
        // Tape runs at about one turn every 2.6 s while recording.
        reels.step (model.monitor().isRecording() ? 2.4 : 0.0, 1.0 / 30.0);
        repaint (localF (L.machine).toNearestInt());
        updateTimer();
    }

    //==========================================================================
    void CapturePage::audition()
    {
        const auto* item = selectedItem();
        if (item == nullptr)
            return;
        auto& player = model.editor().player();
        if (player.isPlaying() && player.token() == 20000)
        {
            player.stop();
            return;
        }
        // Decoded off the message thread; long takes can take a moment.
        message = "Loading " + item->title + "...";
        repaint();
        std::weak_ptr<bool> token = alive;
        decodePool.addJob ([this, file = item->file, title = item->title, token]
        {
            juce::String error;
            auto audio = SampleIO::decode (file, error, 120.0);
            juce::MessageManager::callAsync ([this, token, title, error, audio = std::move (audio)]() mutable
            {
                if (token.expired() || ! *token.lock())
                    return;
                if (! audio)
                {
                    message = "Couldn't play " + title + ": " + error;
                    repaint();
                    return;
                }
                message.clear();
                model.editor().playOther (std::make_shared<const AudioData> (std::move (*audio)), 20000);
                repaint();
            });
        });
    }

    void CapturePage::openSelected (bool chop)
    {
        if (const auto* item = selectedItem())
            model.openInEditor (*item, chop);
    }

    void CapturePage::import()
    {
        chooser = std::make_unique<juce::FileChooser> ("Import audio into Halo", juce::File(), SampleIO::supportedPattern());
        chooser->launchAsync (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles
                                  | juce::FileBrowserComponent::canSelectMultipleItems,
                              [this] (const juce::FileChooser& fc)
                              {
                                  juce::StringArray paths;
                                  for (const auto& f : fc.getResults())
                                      paths.add (f.getFullPathName());
                                  filesDropped (paths, 0, 0);
                              });
    }

    bool CapturePage::isInterestedInFileDrag (const juce::StringArray& files)
    {
        for (const auto& f : files)
            if (SampleIO::isSupported (juce::File (f)))
                return true;
        return false;
    }

    void CapturePage::filesDropped (const juce::StringArray& files, int, int)
    {
        dropHover = false;
        if (files.isEmpty())
        {
            repaint();
            return;
        }
        const int added = model.library().importFiles (files);
        message = added > 0 ? "Imported " + juce::String (added) + (added == 1 ? " file" : " files")
                            : "Nothing new to import (unsupported, or already here)";
        rebuildView();
    }

    //==========================================================================
    void CapturePage::resized()
    {
        if (frame.width <= 0)
            return;
        L = layout::captureLayout (frame);
        const bool regular = frame.density == layout::Density::regular;

        auto head = localF (L.header);
        for (auto* k : { &folderKey, &importKey })
        {
            const float w = (float) k->preferredWidth();
            k->setBounds (head.removeFromRight (w).withSizeKeepingCentre (w, 34.0f).toNearestInt());
            head.removeFromRight (10.0f);
        }

        // Tape machine: keys in the right-hand column.
        auto machine = localF (L.machine);
        auto controls = machine.removeFromRight (regular ? 360.0f : 320.0f).reduced (0.0f, 6.0f);
        auto keysRow = controls.removeFromTop (regular ? 44.0f : 36.0f);
        monitorKey.setBounds (keysRow.removeFromLeft ((float) monitorKey.preferredWidth()).toNearestInt());
        keysRow.removeFromLeft (10.0f);
        recKey.setBounds (keysRow.removeFromLeft ((float) recKey.preferredWidth()).toNearestInt());
        controls.removeFromTop (10.0f);
        auto grabRow = controls.removeFromTop (30.0f);
        grabKey.setBounds (grabRow.removeFromRight ((float) grabKey.preferredWidth()).toNearestInt());
        grabRow.removeFromRight (10.0f);
        grabLengths.setBounds (grabRow.toNearestInt());
        auto takeRow = controls.removeFromBottom (22.0f);
        showKey.setBounds (takeRow.removeFromRight ((float) showKey.preferredWidth()).withSizeKeepingCentre ((float) showKey.preferredWidth(), 22.0f).toNearestInt());

        // Library toolbar: search, kind chips, sort chips.
        auto bar = localF (L.toolbar);
        search.setBounds (bar.removeFromLeft (juce::jmin (300.0f, bar.getWidth() * 0.3f)).toNearestInt());
        bar.removeFromLeft (16.0f);
        kinds.setBounds (bar.removeFromLeft (4.0f * 70.0f + 3.0f * 6.0f).toNearestInt());
        sorts.setBounds (bar.removeFromRight (3.0f * 74.0f + 2.0f * 6.0f).toNearestInt());
        list->setBounds (localF (L.list).toNearestInt());

        auto actions = localF (L.actions);
        float x = actions.getX();
        for (auto* k : { &editKey, &chopKey })
        {
            k->setBounds (juce::Rectangle<float> (x, actions.getY(), (float) k->preferredWidth(), actions.getHeight()).toNearestInt());
            x += (float) k->preferredWidth() + 10.0f;
        }
        forgetKey.setBounds (actions.removeFromRight ((float) forgetKey.preferredWidth()).toNearestInt());
    }

    bool CapturePage::keyPressed (const juce::KeyPress& key)
    {
        const auto mods = key.getModifiers();
        if (mods.isCtrlDown() && key.getKeyCode() == 'F') { search.grabKeyboardFocus(); return true; }
        if (! mods.isAnyModifierKeyDown() && key.getKeyCode() == juce::KeyPress::spaceKey) { audition(); return true; }
        if (key == juce::KeyPress::returnKey) { openSelected (false); return true; }
        return list->keyPressed (key);
    }

    //==========================================================================
    void CapturePage::paintMachine (juce::Graphics& g, juce::Rectangle<float> r)
    {
        const auto& t = theme();
        const bool regular = frame.density == layout::Density::regular;
        const auto& engine = model.monitor();
        const bool recording = engine.isRecording();
        const bool running = engine.isRunning();

        g.setColour (t.scr.panel);
        g.fillRoundedRectangle (r, 5.0f);
        auto inner = r.reduced (16.0f, 12.0f);
        inner.removeFromRight (regular ? 360.0f : 320.0f);

        // The counter (Numerals L) and its scrub ruler.
        auto left = inner.removeFromLeft (regular ? 250.0f : 210.0f);
        const auto& take = model.lastTake();
        const double seconds = recording ? engine.recordingSeconds() : take ? take->seconds : 0.0;
        paint::text (g, recording ? "RECORDING" : take ? "LAST TAKE" : "COUNTER", left.removeFromTop (16.0f), HaloFonts::font (Role::label),
                     recording ? t.honest.danger : t.scr.dim);
        const float cap = regular ? numerals::L : numerals::M;
        numerals::draw (g, counter (seconds), left.getX(), left.getY() + 8.0f + cap, cap, t.scr.ink, true);
        left.removeFromTop (cap + 20.0f);
        gauges::ruler (g, left.removeFromTop (22.0f).withTrimmedRight (20.0f), std::fmod (seconds, 60.0) / 60.0, t.scr.ink, -1.0, "0", "60 s");

        // Reels, the tape path, the head block and the hot playhead.
        auto deck = inner.removeFromLeft (juce::jmin (inner.getWidth() - 150.0f, regular ? 300.0f : 230.0f)).reduced (8.0f, 0.0f);
        const float rr = juce::jmin (deck.getHeight() * 0.36f, deck.getWidth() * 0.2f);
        const auto c1 = juce::Point<float> (deck.getX() + rr + 6.0f, deck.getY() + rr + 6.0f);
        const auto c2 = juce::Point<float> (deck.getRight() - rr - 6.0f, c1.y);
        const float wound = (float) juce::jlimit (0.0, 1.0, seconds / 600.0);
        // The take-up reel turns faster as the supply runs (same tape speed).
        progress::reel (g, c1, rr, reels.angle, 1.0f - wound * 0.6f, t.scr.ink);
        progress::reel (g, c2, rr, reels.angle * (1.0 + 0.6 * (1.0 - wound)), wound, t.scr.ink);
        const auto head = juce::Rectangle<float> (14.0f, 14.0f).withCentre ({ (c1.x + c2.x) * 0.5f, c1.y + rr + 18.0f });
        g.setColour (t.scr.ink.withAlpha (0.7f));
        g.drawLine (c1.x - rr * 0.2f, c1.y + rr, head.getX(), head.getCentreY(), 1.2f);
        g.drawLine (head.getRight(), head.getCentreY(), c2.x + rr * 0.2f, c2.y + rr, 1.2f);
        g.setColour (t.scr.ink);
        g.fillRect (head);
        g.setColour (recording ? t.scr.hot : t.scr.dim);
        g.fillRect (juce::Rectangle<float> (head.getCentreX() - 1.0f, head.getY() - 10.0f, 2.0f, 8.0f));

        // The REC lamp (TP-7) over the input ladders: solid while recording,
        // pulsing while the route is up and ready, hollow otherwise.
        auto meters = inner.removeFromLeft (engine.isPrintingFx() ? 92.0f : 58.0f).withTrimmedLeft (12.0f);
        const bool print = engine.isPrintingFx();
        const auto lampC = juce::Point<float> (meters.getX() + 8.0f, meters.getY() + 10.0f);
        if (recording)
        {
            g.setColour (t.honest.danger.withAlpha (0.25f));
            g.fillEllipse (juce::Rectangle<float> (28.0f, 28.0f).withCentre (lampC));
            paint::lamp (g, lampC, t.honest.danger, 16.0f);
        }
        else if (running)
        {
            const float pulse = motion::reduced() ? 1.0f : 0.5f + 0.5f * (float) std::sin (juce::Time::getMillisecondCounterHiRes() * 0.004);
            paint::hollowLamp (g, lampC, t.honest.danger.withAlpha (0.45f + 0.55f * pulse), 16.0f, 2.0f);
        }
        else
            paint::hollowLamp (g, lampC, t.scr.frame, 16.0f, 1.5f);
        paint::text (g, "rec", { lampC.x + 14.0f, lampC.y - 6.0f, 30.0f, 12.0f }, HaloFonts::font (Role::print, 10.5f),
                     recording ? t.honest.danger : t.scr.dim);

        // The input ladders: tap `in` (raw: what takes and GRAB record), and
        // with PRINT FX tap `print` (post-rack, what a print take records).
        auto ladderPair = [&] (juce::Rectangle<float> area, const StereoLevels& lv, const juce::String& tap)
        {
            auto bars = area.withTrimmedBottom (16.0f);
            const int segs = 16;
            gauges::ladder (g, bars.withWidth (6.0f), segs, running ? litFor (lv.peakL, segs) : 0, t.scr.ink, true);
            gauges::ladder (g, bars.withX (bars.getX() + 10.0f).withWidth (6.0f), segs, running ? litFor (lv.peakR, segs) : 0, t.scr.ink, true);
            paint::text (g, tap, { area.getX() - 8.0f, area.getBottom() - 12.0f, 32.0f, 12.0f }, HaloFonts::font (Role::print, 10.5f), t.scr.dim,
                         juce::Justification::centred);
        };
        auto meterArea = meters.withTrimmedTop (30.0f).withWidth (22.0f);
        ladderPair (meterArea, model.inputLevels(), "in");
        if (print)
            ladderPair (meterArea.translated (34.0f, 0.0f), model.printLevels(), "print");

        // GRAB's history: the same tape lane as PLAY, the bracket = exactly
        // what GRAB would keep now.
        auto lane = inner.withTrimmedLeft (16.0f).withTrimmedRight (16.0f);
        if (lane.getWidth() > 120.0f)
        {
            double windowSeconds = 0.0;
            if (model.grabUsesBars())
            {
                if (const auto bpm = model.clockBpm())
                    windowSeconds = model.grabber().bars * 4.0 * 60.0 / *bpm;
            }
            else
                windowSeconds = model.grabber().seconds;
            double buffered = 0.0;
            if (const auto history = engine.history(); history != nullptr && engine.route().sampleRate > 0.0)
                buffered = juce::jmin (30.0, (double) (history->nowFrame() - history->oldestResidentFrame()) / engine.route().sampleRate);
            auto block = lane.withSizeKeepingCentre (lane.getWidth(), 58.0f);
            paint::text (g, "GRAB HISTORY", block.removeFromTop (16.0f), HaloFonts::font (Role::label), t.scr.dim);
            block.removeFromTop (6.0f);
            gauges::tapeLane (g, block.removeFromTop (14.0f), 30.0, buffered, running ? juce::jmin (30.0, windowSeconds) : 0.0, t.encoder (2).line);
            block.removeFromTop (4.0f);
            const auto pf = HaloFonts::font (Role::print, 10.5f);
            paint::text (g, running ? "30 s ago" : "monitor off: nothing buffered", block, pf, t.scr.dim);
            paint::text (g, "now", block, pf, t.scr.dim, juce::Justification::centredRight);
        }
        if (print)
            paint::paragraph (g, "print fx: takes include level and rack", lane.withTop (lane.getBottom() - 30.0f),
                              HaloFonts::font (Role::ui, 11.5f), t.scr.dim, 2.0f);

        // Right column texts: blockers, the last take line.
        auto controls = r.reduced (16.0f, 12.0f).removeFromRight (regular ? 360.0f : 320.0f).reduced (0.0f, 6.0f);
        controls.removeFromTop ((regular ? 44.0f : 36.0f) + 10.0f + 30.0f + 8.0f);
        auto takeRow = controls.removeFromBottom (22.0f);
        juce::String blocker;
        const auto dropped = engine.recordingDroppedSamples();
        if (recording && dropped > 0 && engine.route().sampleRate > 0.0)
            blocker = "warning: " + juce::String (juce::roundToInt ((double) dropped / engine.route().sampleRate * 1000.0)) + " ms dropped (disk too slow)";
        else if (! running && engine.ep40InputName().isEmpty())
            blocker = "no ep" + enDash + "40 audio input found";
        // Short machines: the blocker takes the last-take line's place.
        const bool roomy = controls.getHeight() >= 24.0f + 22.0f + 4.0f;
        if (blocker.isNotEmpty())
        {
            const auto b = roomy ? controls.withHeight (24.0f) : takeRow.withSizeKeepingCentre (takeRow.getWidth(), 24.0f);
            g.setColour (t.honest.danger);
            g.drawRoundedRectangle (b.reduced (0.75f), 3.0f, 1.5f);
            pictograms::draw (g, Pict::triangle, { b.getX() + 6.0f, b.getCentreY() - 6.0f, 12.0f, 12.0f }, t.honest.danger, 1.3f);
            paint::text (g, paint::middleEllipsis (blocker, HaloFonts::font (Role::ui, 12.0f), b.getWidth() - 32.0f), b.withTrimmedLeft (24.0f),
                         HaloFonts::font (Role::ui, 12.0f), t.scr.ink);
        }
        else if (! running)
            paint::text (g, "monitor on to record or grab (ctrl m)", controls.withHeight (20.0f), HaloFonts::font (Role::ui, 12.0f), t.scr.dim);
        if (blocker.isNotEmpty() && ! roomy)
            return;
        takeRow = takeRow.withTrimmedRight (showKey.isVisible() ? (float) showKey.getWidth() + 8.0f : 0.0f);
        const auto line = take ? "last take" + dot + take->file.getFileNameWithoutExtension() + dot + duration (take->seconds)
                               : juce::String ("no take yet");
        paint::text (g, paint::middleEllipsis (line, HaloFonts::font (Role::mono, 11.0f), takeRow.getWidth()), takeRow, HaloFonts::font (Role::mono, 11.0f),
                     take ? t.scr.ink : t.scr.dim);
    }

    void CapturePage::paint (juce::Graphics& g)
    {
        if (frame.width <= 0)
            return;
        const auto& t = theme();
        const auto& items = model.library().items();

        // ---- Header ----
        {
            auto head = localF (L.header);
            auto icon = head.removeFromLeft (head.getHeight() - 6.0f).withSizeKeepingCentre (34.0f, 34.0f);
            g.setColour (t.scr.ink.withAlpha (0.85f));
            g.drawRoundedRectangle (icon.reduced (0.6f), 3.0f, 1.2f);
            pictograms::draw (g, Pict::capture, icon.reduced (8.0f), t.scr.ink, 1.4f);
            head.removeFromLeft (14.0f);
            paint::text (g, "capture", head.withHeight (14.0f), HaloFonts::font (Role::ui, 11.5f), t.scr.dim);
            auto titleRow = head.withTrimmedTop (14.0f);
            const auto titleFont = HaloFonts::font (Role::title);
            const juce::String title = "RECORDER + LIBRARY";
            const float titleW = paint::textWidth (titleFont, title) + 4.0f;
            paint::text (g, title, titleRow.withWidth (titleW), titleFont, t.scr.ink);
            const float x = titleRow.getX() + titleW + 24.0f;
            numerals::drawWithUnit (g, juce::String ((int) items.size()), "items", x, titleRow.getBottom() - 5.0f, numerals::S, t.scr.ink, t.scr.dim);
        }

        paintMachine (g, localF (L.machine));

        // ---- Library ----
        if (items.empty())
        {
            // The empty state sits where the list would be: two still reels and the copy.
            const auto area = localF (L.list);
            const auto box = area.withSizeKeepingCentre (juce::jmin (620.0f, area.getWidth()), juce::jmin (110.0f, area.getHeight()));
            const float rr = 22.0f;
            progress::reel (g, { box.getX() + rr + 2.0f, box.getCentreY() }, rr, 0.3, 0.2f, t.scr.dim);
            progress::reel (g, { box.getX() + rr * 3.0f + 14.0f, box.getCentreY() }, rr, 1.1, 0.7f, t.scr.dim);
            paint::paragraph (g, "Nothing here yet. Record a take or GRAB in PLAY, or IMPORT / drop audio files here.",
                              box.withTrimmedLeft (rr * 4.0f + 36.0f).withSizeKeepingCentre (box.getWidth() - rr * 4.0f - 36.0f, 44.0f),
                              HaloFonts::font (Role::body, 14.0f), t.scr.ink, 4.0f);
        }
        else if (view.empty())
            paint::text (g, "nothing matches", localF (L.list).withHeight (40.0f).reduced (10.0f, 0.0f), HaloFonts::font (Role::ui, 12.5f), t.scr.dim);

        // ---- Message line, and the actions' caption ----
        {
            auto msg = localF (L.message);
            if (message.isNotEmpty())
            {
                const bool bad = message.startsWith ("Couldn't");
                if (bad)
                {
                    g.setColour (t.honest.danger);
                    g.drawRoundedRectangle (msg.reduced (0.5f), 3.0f, 1.0f);
                    pictograms::draw (g, Pict::triangle, { msg.getX() + 5.0f, msg.getCentreY() - 6.0f, 12.0f, 12.0f }, t.honest.danger, 1.3f);
                    paint::text (g, message, msg.withTrimmedLeft (22.0f), HaloFonts::font (Role::ui, 12.0f), t.scr.ink);
                }
                else
                {
                    const bool result = message.startsWith ("Imported");
                    if (result)
                        paint::lamp (g, { msg.getX() + 4.0f, msg.getCentreY() }, t.scr.ink, 7.0f);
                    paint::text (g, message, msg.withTrimmedLeft (result ? 14.0f : 0.0f), HaloFonts::font (Role::mono, 11.5f), t.scr.ink);
                }
            }
            else
                paint::text (g, "takes, grabs and imported files" + dot + "drop audio anywhere here to import" + dot + "double-click opens in edit",
                             msg, HaloFonts::font (Role::ui, 12.0f), t.scr.dim);
            if (selectedItem() == nullptr && ! items.empty())
            {
                const auto a = localF (L.actions);
                paint::text (g, "select an item", a.withTrimmedLeft ((float) chopKey.getRight() + 12.0f - a.getX()), HaloFonts::font (Role::ui, 12.0f),
                             t.scr.dim);
            }
        }
    }

    void CapturePage::paintOverChildren (juce::Graphics& g)
    {
       #if JUCE_DEBUG
        const bool posed = DevPose::get ("drop") == 1;
       #else
        const bool posed = false;
       #endif
        if (! dropHover && ! posed)
            return;
        const auto& t = theme();
        const auto r = getLocalBounds().toFloat().reduced (2.0f);
        g.setColour (t.scr.bg.withAlpha (0.82f));
        g.fillRoundedRectangle (r, 6.0f);
        paint::dashedRect (g, r, t.scr.hot, 2.0f, 8.0f, 5.0f, 6.0f);
        paint::text (g, "DROP AUDIO TO IMPORT", r, HaloFonts::font (Role::title), t.scr.ink, juce::Justification::centred);
    }

    void CapturePage::paintLane (juce::Graphics& g, juce::Rectangle<float> r, bool) const
    {
        const auto& t = theme();
        auto rows = r.reduced (0.0f, 4.0f);
        paint::text (g, "KEYS", rows.removeFromTop (14.0f), HaloFonts::font (Role::label), t.scr.dim);
        rows.removeFromTop (6.0f);
        const auto ui = HaloFonts::font (Role::ui, 11.5f);
        paint::text (g, "space audition" + dot + "enter open in edit" + dot + "ctrl f search", rows.removeFromTop (16.0f), ui, t.scr.ink);
        paint::text (g, "ctrl m monitor" + dot + "ctrl r rec" + dot + "ctrl g grab", rows.removeFromTop (16.0f), ui, t.scr.dim);
    }

    //==========================================================================
    std::array<std::optional<EncoderBinding>, 4> CapturePage::bindings()
    {
        std::array<std::optional<EncoderBinding>, 4> out;

        EncoderBinding item;
        item.label = "ITEM";
        item.getNorm = [this] { const int n = (int) view.size(); return n > 1 ? (double) juce::jmax (0, list->selected) / (n - 1) : 0.0; };
        item.setNorm = [this] (double v) { list->select (juce::roundToInt (v * (double) ((int) view.size() - 1))); };
        item.turn = [this] (int d, encoder::Speed sp) { list->move (d * (sp == encoder::Speed::coarse ? 10 : 1)); };
        item.format = [this] { return view.empty() ? enDash : juce::String (list->selected + 1); };
        item.unit = [this] { return view.empty() ? juce::String() : "of " + juce::String ((int) view.size()); };
        item.caption = [this] { const auto* i = selectedItem(); return i != nullptr ? i->title.toLowerCase() : juce::String ("none"); };
        item.pressLabel = "audition";
        item.press = [this] { audition(); };
        item.pressAvailable = [this] { return selectedItem() != nullptr; };
        item.available = [this] { return ! model.library().items().empty(); };
        item.reason = [] { return juce::String ("library empty"); };
        out[0] = item;

        EncoderBinding filt;
        filt.label = "FILTER";
        filt.steps = 4;
        filt.getNorm = [this] { return (int) filter / 3.0; };
        filt.setNorm = [this] (double v) { filter = (Library::Filter) juce::jlimit (0, 3, juce::roundToInt (v * 3.0)); kinds.setSelected ((int) filter); rebuildView(); };
        filt.turn = [this] (int d, encoder::Speed)
        {
            filter = (Library::Filter) juce::jlimit (0, 3, (int) filter + d);
            kinds.setSelected ((int) filter);
            rebuildView();
        };
        filt.format = [this] { return kinds.getItems()[(int) filter]; };
        filt.caption = [this] { return juce::String ((int) view.size()) + " shown"; };
        filt.pressLabel = "open in edit";
        filt.press = [this] { openSelected (false); };
        filt.pressAvailable = [this] { return selectedItem() != nullptr; };
        out[1] = filt;

        out[2] = grabBinding (model, 2);

        auto level = levelBinding (model);
        level.caption = [] { return juce::String ("what you hear, not raw takes"); };
        level.pressLabel = juce::String::fromUTF8 ("\xe2\x97\x8f rec");
        level.press = [this] { model.toggleRecording(); };
        level.pressAvailable = [this] { return model.monitor().isRunning(); };
        out[3] = level;
        return out;
    }
}
