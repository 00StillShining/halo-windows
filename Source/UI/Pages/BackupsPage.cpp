#include "UI/Pages/BackupsPage.h"
#include "App/DevPose.h"
#include "Core/EncoderMath.h"
#include "UI/Design/Numerals.h"
#include "UI/Design/Paint.h"
#include "UI/Design/Pictograms.h"
#include "UI/Design/Primitives/Progress.h"

namespace halo
{
    namespace
    {
        const juce::String dot = juce::String::fromUTF8 (" \xc2\xb7 ");
        const juce::String enDash = juce::String::charToString ((juce::juce_wchar) 0x2013);
        const juce::Colour black { 0xff000005 };

        bool isError (const juce::String& status)
        {
            return status.startsWith ("Backup finished with problems") || status.startsWith ("EP-40 disconnected") || status.startsWith ("Can't create");
        }

        juce::String minutesLeft (double s)
        {
            if (s < 0.0)
                return {};
            const int left = (int) s;
            return "about " + (left >= 60 ? juce::String (left / 60) + " min" : juce::String (left) + " s") + " left";
        }

        // Debug shot poses: fixtures only, never device state.
        std::vector<Ep40Backup::Entry> fixtureShelf()
        {
            std::vector<Ep40Backup::Entry> v;
            const char* when[] { "2026-10-18T21:04:00", "2026-10-12T19:40:00", "2026-10-02T23:15:00", "2026-09-21T18:02:00", "2026-09-03T12:30:00" };
            for (int i = 0; i < 5; ++i)
            {
                Ep40Backup::Entry e;
                e.serial = "EP40-0D2F";
                e.os = "2.5.0";
                e.created = when[i];
                e.sounds = i == 1 ? 212 : 477 - i * 3;
                e.projects = 9;
                e.complete = i != 1;
                v.push_back (e);
            }
            return v;
        }
    }

    BackupsPage::BackupsPage (AppModel& m) : model (m)
    {
        setOpaque (false);
        setWantsKeyboardFocus (true);
        for (auto* c : std::initializer_list<juce::Component*> { &backupKey, &cancelKey, &folderKey, &showKey })
            addAndMakeVisible (c);
        backupKey.onClick = [this] { model.backup().start(); };
        backupKey.setTooltip ("Copy every sound and project to Documents\\Halo\\Backups (read-only: nothing on the EP-40 changes)");
        cancelKey.onClick = [this] { model.backup().cancel(); };
        folderKey.onClick = []
        {
            Ep40Backup::backupsFolder().createDirectory();
            Ep40Backup::backupsFolder().revealToUser();
        };
        showKey.onClick = [this]
        {
            const auto v = currentView();
            if (juce::isPositiveAndBelow (selected, (int) v.shelf.size()) && pose < 0)
                v.shelf[(size_t) selected].folder.revealToUser();
        };
       #if JUCE_DEBUG
        pose = DevPose::get ("backup"); // 1 locked, 2 unlocked, 3 running, 4 error, 5 incomplete (cancelled)
       #endif
        model.backup().addChangeListener (this);
        model.device().addChangeListener (this);
        model.backup().rescanAsync();
        refresh();
    }

    BackupsPage::~BackupsPage()
    {
        model.backup().removeChangeListener (this);
        model.device().removeChangeListener (this);
    }

    juce::Rectangle<float> BackupsPage::localF (const layout::Rect& r) const
    {
        return { r.x - frame.body.x, r.y - frame.body.y, r.w, r.h };
    }

    void BackupsPage::setFrame (const layout::Frame& f)
    {
        frame = f;
        resized();
    }

    juce::String BackupsPage::explainer()
    {
        // Today's copy, verbatim.
        return "Copies every sound (bit-exact WAV + its settings) and all 9 projects to Documents\\Halo\\Backups. "
               "Read-only: nothing on the EP-40 changes. A full backup takes a while (about 1 MB every 12 s over USB MIDI); "
               "keep the EP-40 connected and awake.";
    }

    BackupsPage::View BackupsPage::currentView() const
    {
        View v;
        if (pose >= 1)
        {
            v.serial = "EP40-0D2F";
            v.plate = pose == 2 ? Plate::unlocked : Plate::locked;
            v.shelf = fixtureShelf();
            if (pose == 1 || pose == 5)
                for (auto& e : v.shelf)
                    e.complete = false;
            v.running = pose == 3;
            v.total = 9 + 477;
            v.done = 58 + 9;
            v.secondsLeft = 24 * 60;
            v.status = pose == 3 ? "Saving sound 58 of 477, about 24 min left"
                     : pose == 4 ? "Backup finished with problems (1), so writes stay locked: sound 212: timeout"
                     : pose == 5 ? "Backup cancelled (incomplete, so writes stay locked)" : juce::String();
            v.failed = pose == 4;
            if (pose == 4)
                v.shelf.front().complete = false;
            return v;
        }
        auto& b = model.backup();
        const auto& info = model.device().info();
        const bool ready = model.device().state() == Ep40Library::State::ready;
        v.serial = info.serial;
        v.plate = ! ready ? Plate::noDevice : model.latestBackup() ? Plate::unlocked : Plate::locked;
        v.running = b.isRunning();
        v.done = b.itemsDone();
        v.total = b.itemsTotal();
        v.secondsLeft = b.secondsLeft();
        v.status = b.status();
        v.failed = ! b.isRunning() && isError (b.status());
        v.shelf = b.entries();
        std::sort (v.shelf.begin(), v.shelf.end(), [] (const auto& x, const auto& y) { return x.createdTime() > y.createdTime(); });
        return v;
    }

    void BackupsPage::refresh()
    {
        const auto v = currentView();
        backupKey.setEnabled (v.plate != Plate::noDevice && ! v.running);
        cancelKey.setVisible (v.running);
        showKey.setVisible (! v.running && juce::isPositiveAndBelow (selected, (int) v.shelf.size()));
        if ((v.running || reels.speed != 0.0) && ! isTimerRunning())
            startTimerHz (24);
        resized();
        repaint();
    }

    //==========================================================================
    void BackupsPage::timerCallback()
    {
        const bool running = currentView().running;
        const bool moving = reels.step (running ? 1.6 : 0.0, 1.0 / 24.0);
        repaint (localF (L.canvas).toNearestInt());
        if (! running && ! moving)
            stopTimer();
    }

    void BackupsPage::resized()
    {
        if (frame.width <= 0)
            return;
        const float explainW = juce::jmin (860.0f, frame.body.w * 0.62f);
        const float explainH = paint::paragraphHeight (explainer(), HaloFonts::font (Role::body), explainW, 4.0f);
        L = layout::backupsLayout (frame, explainH);
        auto plate = localF (L.plate).reduced (20.0f, 0.0f);
        auto keys = plate.removeFromRight (juce::jmax ((float) backupKey.preferredWidth(), (float) folderKey.preferredWidth()) + 8.0f);
        const bool tall = plate.getHeight() >= 100.0f;
        const float keyH = tall ? 44.0f : 36.0f;
        auto stack = keys.withSizeKeepingCentre (keys.getWidth(), tall ? keyH * 2.0f + 10.0f : keyH);
        if (tall)
        {
            backupKey.setBounds (stack.removeFromTop (keyH).toNearestInt());
            stack.removeFromTop (10.0f);
            folderKey.setBounds (stack.removeFromTop (keyH).withWidth ((float) folderKey.preferredWidth()).toNearestInt());
        }
        else
        {
            backupKey.setBounds (stack.toNearestInt());
            folderKey.setBounds (juce::Rectangle<float> ((float) backupKey.getX() - 10.0f - (float) folderKey.preferredWidth(), stack.getY(),
                                                         (float) folderKey.preferredWidth(), keyH).toNearestInt());
        }
        cancelKey.setBounds (backupKey.getBounds().withWidth (cancelKey.preferredWidth()).translated (-(cancelKey.preferredWidth() + 10), 0));
        const auto canvas = localF (L.canvas);
        showKey.setBounds (juce::Rectangle<float> (canvas.getRight() - (float) showKey.preferredWidth(), canvas.getY(), (float) showKey.preferredWidth(), 22.0f)
                               .toNearestInt());
    }

    int BackupsPage::columns() const
    {
        const auto shelf = localF (L.canvas).withTrimmedTop (30.0f);
        return juce::jmax (1, (int) ((shelf.getWidth() + 16.0f) / (layout::BackupsLayout::cassetteW + 16.0f)));
    }

    juce::Rectangle<float> BackupsPage::cassetteBounds (int index) const
    {
        const auto shelf = localF (L.canvas).withTrimmedTop (30.0f);
        const int cols = columns();
        const int row = index / cols - firstRow, col = index % cols;
        return { shelf.getX() + (float) col * (layout::BackupsLayout::cassetteW + 16.0f), shelf.getY() + (float) row * (layout::BackupsLayout::cassetteH + 14.0f),
                 layout::BackupsLayout::cassetteW, layout::BackupsLayout::cassetteH };
    }

    int BackupsPage::cassetteAt (juce::Point<float> p) const
    {
        const auto v = currentView();
        if (v.running)
            return -1;
        for (int i = 0; i < (int) v.shelf.size(); ++i)
            if (cassetteBounds (i).contains (p) && localF (L.canvas).contains (p))
                return i;
        return -1;
    }

    void BackupsPage::mouseDown (const juce::MouseEvent& e)
    {
        const int i = cassetteAt (e.position);
        if (i >= 0)
        {
            selected = i;
            refresh();
        }
        grabKeyboardFocus();
    }

    void BackupsPage::mouseDoubleClick (const juce::MouseEvent& e)
    {
        const int i = cassetteAt (e.position);
        const auto v = currentView();
        if (i >= 0 && pose < 0)
            v.shelf[(size_t) i].folder.revealToUser();
    }

    void BackupsPage::mouseWheelMove (const juce::MouseEvent&, const juce::MouseWheelDetails& w)
    {
        wheelAcc += (std::abs (w.deltaY) >= std::abs (w.deltaX) ? w.deltaY : -w.deltaX) * (w.isReversed ? -1.0f : 1.0f);
        const float notch = (float) encoder::kNotch;
        int d = 0;
        while (std::abs (wheelAcc) >= notch * 0.85f)
        {
            const int dir = wheelAcc > 0 ? 1 : -1;
            wheelAcc -= (float) dir * notch;
            d += dir;
        }
        const auto v = currentView();
        const int rows = ((int) v.shelf.size() + columns() - 1) / columns();
        const int visible = juce::jmax (1, (int) ((localF (L.canvas).getHeight() - 30.0f + 14.0f) / (layout::BackupsLayout::cassetteH + 14.0f)));
        firstRow = juce::jlimit (0, juce::jmax (0, rows - visible), firstRow - d);
        repaint();
    }

    bool BackupsPage::keyPressed (const juce::KeyPress& key)
    {
        const auto v = currentView();
        const int n = (int) v.shelf.size();
        if (n == 0)
            return false;
        int next = selected;
        if (key == juce::KeyPress::rightKey) next = selected + 1;
        else if (key == juce::KeyPress::leftKey) next = selected - 1;
        else if (key == juce::KeyPress::downKey) next = selected + columns();
        else if (key == juce::KeyPress::upKey) next = selected - columns();
        else if (key == juce::KeyPress::returnKey && juce::isPositiveAndBelow (selected, n) && pose < 0)
        {
            v.shelf[(size_t) selected].folder.revealToUser();
            return true;
        }
        else
            return false;
        selected = juce::jlimit (0, n - 1, next);
        refresh();
        return true;
    }

    //==========================================================================
    void BackupsPage::paintPlate (juce::Graphics& g, juce::Rectangle<float> r, const View& v)
    {
        const auto& t = theme();
        g.setColour (t.scr.panel);
        g.fillRoundedRectangle (r, 6.0f);
        g.setColour (v.plate == Plate::unlocked ? t.scr.ink.withAlpha (0.5f) : t.scr.frame);
        g.drawRoundedRectangle (r.reduced (0.5f), 6.0f, 1.0f);
        auto inner = r.reduced (20.0f, 12.0f);
        inner.removeFromRight ((float) backupKey.getWidth() + 30.0f + (r.getHeight() < 100.0f ? (float) folderKey.getWidth() + 10.0f : 0.0f));

        const float lockSize = juce::jmin (64.0f, inner.getHeight());
        auto lock = inner.removeFromLeft (lockSize + 8.0f).withSizeKeepingCentre (lockSize, lockSize);
        if (v.plate == Plate::noDevice)
            paint::hollowLamp (g, lock.getCentre(), t.scr.dim, 22.0f, 2.0f);
        else
            pictograms::draw (g, v.plate == Plate::unlocked ? Pict::lockOpen : Pict::lockClosed, lock, t.scr.ink, 2.4f);
        inner.removeFromLeft (20.0f);

        const bool roomy = inner.getHeight() >= 90.0f;
        auto rows = inner.withSizeKeepingCentre (inner.getWidth(), roomy ? 84.0f : 64.0f);
        if (v.plate == Plate::noDevice)
        {
            paint::text (g, "Connect the EP-40 (see LOAD) to back it up", rows, HaloFonts::font (Role::bodyStrong, 15.0f), t.scr.ink);
            return;
        }
        const bool unlocked = v.plate == Plate::unlocked;
        auto tagRow = rows.removeFromTop (22.0f);
        const auto tag = paint::tag (g, tagRow.getTopLeft(), unlocked ? "WRITES UNLOCKED" : "WRITES LOCKED",
                                     unlocked ? paint::TagStyle::filled : paint::TagStyle::outline, unlocked ? t.scr.ink : t.scr.frame,
                                     unlocked ? black : t.scr.ink, 22.0f);
        if (unlocked)
            paint::lamp (g, { tag.getRight() + 12.0f, tag.getCentreY() }, t.scr.hot, 8.0f);
        else
            paint::hollowLamp (g, { tag.getRight() + 12.0f, tag.getCentreY() }, t.scr.dim, 8.0f);
        rows.removeFromTop (8.0f);
        const auto line = "This EP-40 (" + v.serial + ") has " + (unlocked ? juce::String ("a complete backup: writes are unlocked")
                                                                          : juce::String ("no complete backup yet: writes are locked"));
        paint::text (g, line, rows.removeFromTop (20.0f), HaloFonts::font (Role::bodyStrong, 14.0f), t.scr.ink);
        if (roomy || ! unlocked)
        {
            rows.removeFromTop (4.0f);
            if (! unlocked)
                paint::text (g, "SEND, ASSIGN and DELETE unlock after one", rows.removeFromTop (18.0f), HaloFonts::font (Role::ui, 12.5f), t.honest.info);
            else if (const auto b = model.latestBackup())
                paint::text (g, Ep40Backup::describe (*b), rows.removeFromTop (18.0f), HaloFonts::font (Role::mono, 11.5f), t.scr.dim);
        }
    }

    void BackupsPage::paintProgress (juce::Graphics& g, juce::Rectangle<float> r, const View& v)
    {
        const auto& t = theme();
        // Big reels on the left, turning while bytes move.
        const double angle = reels.angle;
        auto reelArea = r.removeFromLeft (juce::jmin (300.0f, r.getWidth() * 0.26f));
        const float rr = juce::jmin (reelArea.getHeight() * 0.32f, reelArea.getWidth() * 0.22f);
        const float f = v.total > 0 ? (float) v.done / (float) v.total : 0.0f;
        progress::reel (g, { reelArea.getX() + rr + 8.0f, reelArea.getCentreY() }, rr, angle, 1.0f - f, t.scr.ink);
        progress::reel (g, { reelArea.getRight() - rr - 8.0f, reelArea.getCentreY() }, rr, angle, f, t.scr.ink);
        r.removeFromLeft (24.0f);

        // The count, the time left, today's status line.
        auto head = r.removeFromTop (numerals::XL + 10.0f);
        paint::tag (g, head.getTopLeft(), "BACKING UP", paint::TagStyle::filled, t.scr.ink, black);
        const float x = numerals::draw (g, juce::String (juce::jmax (0, v.done - 9) ), head.getX() + 120.0f, head.getBottom() - 4.0f, numerals::XL, t.scr.ink);
        paint::textAt (g, "/ " + juce::String (v.total - 9) + " sounds", x + 10.0f, head.getBottom() - 4.0f, HaloFonts::font (Role::uiLight, 18.0f), t.scr.dim);
        paint::text (g, minutesLeft (v.secondsLeft), head.withTrimmedLeft (head.getWidth() * 0.6f), HaloFonts::font (Role::uiLight, 16.0f), t.scr.dim,
                     juce::Justification::bottomRight);
        r.removeFromTop (10.0f);
        paint::text (g, v.status.toLowerCase(), r.removeFromTop (18.0f), HaloFonts::font (Role::mono, 11.5f), t.scr.ink);
        r.removeFromTop (8.0f);

        // One dot per project (9) and sound: saved ink, current blinking hot, pending hollow.
        auto matrix = r.withTrimmedBottom (22.0f);
        const int n = juce::jmax (1, v.total);
        float pitch = juce::jlimit (5.0f, 18.0f, std::sqrt (matrix.getWidth() * matrix.getHeight() / (float) n));
        while (pitch > 5.0f && (float) ((n + (int) (matrix.getWidth() / pitch) - 1) / juce::jmax (1, (int) (matrix.getWidth() / pitch))) * pitch > matrix.getHeight())
            pitch -= 0.5f;
        const int cols = juce::jmax (1, (int) (matrix.getWidth() / pitch));
        const float d = pitch * 0.62f;
        juce::RectangleList<float> saved;
        juce::Path pending;
        juce::Rectangle<float> current;
        for (int i = 0; i < n; ++i)
        {
            const auto dotR = juce::Rectangle<float> (d, d).withCentre ({ matrix.getX() + ((float) (i % cols) + 0.5f) * pitch,
                                                                          matrix.getY() + ((float) (i / cols) + 0.5f) * pitch });
            if (dotR.getBottom() > matrix.getBottom())
                break;
            if (i < v.done) saved.add (dotR);
            else if (i == v.done) current = dotR;
            else pending.addEllipse (dotR.reduced (0.5f));
        }
        g.setColour (t.scr.ink);
        for (const auto& s : saved)
            g.fillRect (s); // squares for projects and sounds alike: a matrix reads as one block
        g.setColour (t.scr.ramp[3]);
        g.strokePath (pending, juce::PathStrokeType (1.0f));
        if (! current.isEmpty() && (motion::reduced() || (juce::Time::getMillisecondCounter() / 400) % 2 == 0))
        {
            g.setColour (t.scr.hot);
            g.fillEllipse (current.expanded (1.0f));
        }
        paint::text (g, "keep the ep" + enDash + "40 connected and awake" + dot + "the first 9 dots are the projects",
                     r.removeFromBottom (18.0f), HaloFonts::font (Role::ui, 12.0f), t.scr.dim);
    }

    void BackupsPage::paintCassette (juce::Graphics& g, juce::Rectangle<float> r, const Ep40Backup::Entry* e, bool isSelected, bool failedRun)
    {
        const auto& t = theme();
        const bool complete = e != nullptr && e->complete;
        g.setColour (e == nullptr ? t.scr.panel.withAlpha (0.4f) : t.scr.panel);
        g.fillRoundedRectangle (r, 6.0f);
        g.setColour (isSelected ? t.scr.hot : t.scr.frame);
        g.drawRoundedRectangle (r.reduced (isSelected ? 1.0f : 0.5f), 6.0f, isSelected ? 2.0f : 1.0f);
        // The label area and the two reel windows.
        auto label = r.reduced (10.0f, 8.0f).removeFromTop (60.0f);
        g.setColour (e == nullptr ? t.scr.frame : t.scr.paper.withAlpha (complete ? 1.0f : 0.55f));
        g.fillRoundedRectangle (label, 3.0f);
        auto window = juce::Rectangle<float> (r.getCentreX() - 50.0f, label.getBottom() + 3.0f, 100.0f, 21.0f);
        g.setColour (t.scr.bg);
        g.fillRoundedRectangle (window, 8.0f);
        for (float cx : { window.getX() + 14.0f, window.getRight() - 14.0f })
        {
            g.setColour (t.scr.ink.withAlpha (e == nullptr ? 0.3f : 0.75f));
            g.drawEllipse (juce::Rectangle<float> (15.0f, 15.0f).withCentre ({ cx, window.getCentreY() }), 1.2f);
            g.fillEllipse (juce::Rectangle<float> (5.0f, 5.0f).withCentre ({ cx, window.getCentreY() }));
        }
        if (e == nullptr)
            return;

        auto inner = label.reduced (8.0f, 5.0f);
        const auto time = e->createdTime();
        const float cap = numerals::XS;
        numerals::draw (g, time.formatted ("%d.%m").trimCharactersAtStart ("0"), inner.getX(), inner.getY() + cap, cap, black);
        paint::textAt (g, time.formatted ("%H:%M") + "  " + juce::String (time.getYear()), inner.getX() + 58.0f, inner.getY() + cap,
                       HaloFonts::font (Role::mono, 11.0f), black.withAlpha (0.7f));
        inner.removeFromTop (cap + 6.0f);
        paint::text (g, juce::String (e->sounds) + " sounds" + dot + juce::String (e->projects) + " projects" + dot + "os " + e->os,
                     inner.removeFromTop (14.0f), HaloFonts::font (Role::print, 10.5f), black.withAlpha (0.75f), juce::Justification::topLeft);
        if (! complete)
            paint::text (g, "incomplete" + dot + "doesn't unlock writes", inner.removeFromTop (14.0f), HaloFonts::font (Role::print, 10.5f),
                         black.withAlpha (0.6f), juce::Justification::topLeft);

        // Bottom: COMPLETE (ink) or INCOMPLETE (hatched outline); a failed run is the only crimson.
        auto foot = r.reduced (10.0f, 7.0f).removeFromBottom (18.0f);
        if (failedRun)
            paint::tag (g, foot.getTopLeft(), "FAILED", paint::TagStyle::danger, t.honest.danger, t.scr.ink, 18.0f);
        else if (complete)
            paint::tag (g, foot.getTopLeft(), "COMPLETE", paint::TagStyle::filled, t.scr.ink, black, 18.0f);
        else
        {
            // A hatched outline tag: the hatch sits in its own swatch, never under the word.
            const auto f = HaloFonts::font (Role::tag);
            const auto tagR = foot.withWidth (paint::textWidth (f, "INCOMPLETE") + 34.0f);
            g.setColour (t.scr.frame);
            g.drawRoundedRectangle (tagR.reduced (0.5f), 2.5f, 1.0f);
            paint::hatch (g, tagR.withWidth (14.0f).reduced (2.0f), t.scr.dim, 4.0f, 1.0f);
            paint::text (g, "INCOMPLETE", tagR.withTrimmedLeft (20.0f), f, t.scr.dim);
        }
    }

    void BackupsPage::paintShelf (juce::Graphics& g, juce::Rectangle<float> r, const View& v)
    {
        const auto& t = theme();
        paint::text (g, "SHELF", r.withHeight (16.0f), HaloFonts::font (Role::label), t.scr.dim);
        paint::text (g, juce::String ((int) v.shelf.size()) + " backups" + dot + "double-click shows the folder",
                     r.withHeight (16.0f).withTrimmedLeft (64.0f), HaloFonts::font (Role::ui, 12.0f), t.scr.dim);
        if (v.failed || (v.status.startsWith ("Backup cancelled")))
        {
            // The last run's line: a blocker for an error, plain for a cancel.
            const auto row = r.withTrimmedTop (0.0f).withHeight (22.0f).withTrimmedLeft (r.getWidth() * 0.42f).withTrimmedRight ((float) showKey.getWidth() + 10.0f);
            if (v.failed)
            {
                g.setColour (t.honest.danger);
                g.drawRoundedRectangle (row.reduced (0.75f), 3.0f, 1.5f);
                pictograms::draw (g, Pict::triangle, { row.getX() + 6.0f, row.getCentreY() - 6.0f, 12.0f, 12.0f }, t.honest.danger, 1.3f);
                paint::text (g, paint::middleEllipsis (v.status, HaloFonts::font (Role::ui, 12.0f), row.getWidth() - 30.0f), row.withTrimmedLeft (24.0f),
                             HaloFonts::font (Role::ui, 12.0f), t.scr.ink);
            }
            else
                paint::text (g, v.status, row, HaloFonts::font (Role::ui, 12.0f), t.scr.dim, juce::Justification::centredRight);
        }
        if (v.shelf.empty())
        {
            auto area = r.withTrimmedTop (30.0f);
            const auto c = cassetteBounds (0);
            paintCassette (g, c, nullptr, false, false);
            paint::paragraph (g, "No backups yet. BACK UP NOW copies every sound and project; nothing on the EP-40 changes.",
                              juce::Rectangle<float> (c.getRight() + 24.0f, c.getY() + 30.0f, juce::jmin (460.0f, area.getRight() - c.getRight() - 24.0f), 60.0f),
                              HaloFonts::font (Role::body, 14.0f), t.scr.ink, 4.0f);
            return;
        }
        juce::Graphics::ScopedSaveState save (g);
        g.reduceClipRegion (r.withTrimmedTop (28.0f).toNearestInt());
        for (int i = 0; i < (int) v.shelf.size(); ++i)
        {
            const auto c = cassetteBounds (i);
            if (c.getBottom() < r.getY() || c.getY() > r.getBottom())
                continue;
            paintCassette (g, c, &v.shelf[(size_t) i], i == selected, v.failed && i == 0 && ! v.shelf[0].complete);
        }
    }

    void BackupsPage::paint (juce::Graphics& g)
    {
        if (frame.width <= 0)
            return;
        const auto& t = theme();
        const auto v = currentView();
        paintPlate (g, localF (L.plate), v);
        if (v.running)
            paintProgress (g, localF (L.canvas), v);
        else
            paintShelf (g, localF (L.canvas), v);
        const float explainW = juce::jmin (860.0f, frame.body.w * 0.62f);
        paint::paragraph (g, explainer(), localF (L.explainer).withWidth (explainW), HaloFonts::font (Role::body), t.scr.dim, 4.0f);
    }

    void BackupsPage::paintLane (juce::Graphics& g, juce::Rectangle<float> r, bool) const
    {
        const auto& t = theme();
        auto rows = r.reduced (0.0f, 4.0f);
        paint::text (g, "KEYS", rows.removeFromTop (14.0f), HaloFonts::font (Role::label), t.scr.dim);
        rows.removeFromTop (6.0f);
        const auto ui = HaloFonts::font (Role::ui, 11.5f);
        paint::text (g, "arrows pick a cassette" + dot + "enter shows its folder", rows.removeFromTop (16.0f), ui, t.scr.ink);
        paint::text (g, "a backup is read-only: nothing on the ep" + enDash + "40 changes", rows.removeFromTop (16.0f), ui, t.scr.dim);
    }

    //==========================================================================
    std::array<std::optional<EncoderBinding>, 4> BackupsPage::bindings()
    {
        std::array<std::optional<EncoderBinding>, 4> out;
        EncoderBinding shelf;
        shelf.label = "SHELF";
        shelf.getNorm = [this] { const int n = (int) currentView().shelf.size(); return n > 1 ? (double) juce::jmax (0, selected) / (n - 1) : 0.0; };
        shelf.setNorm = [this] (double v) { const int n = (int) currentView().shelf.size(); selected = juce::roundToInt (v * (n - 1)); refresh(); };
        shelf.turn = [this] (int d, encoder::Speed)
        {
            const int n = (int) currentView().shelf.size();
            if (n > 0)
                selected = juce::jlimit (0, n - 1, (selected < 0 ? -1 : selected) + d);
            refresh();
        };
        shelf.format = [this] { return selected < 0 ? enDash : juce::String (selected + 1); };
        shelf.unit = [this] { return "of " + juce::String ((int) currentView().shelf.size()); };
        shelf.caption = [this]
        {
            const auto v = currentView();
            return juce::isPositiveAndBelow (selected, (int) v.shelf.size()) ? Ep40Backup::describe (v.shelf[(size_t) selected]).fromFirstOccurrenceOf ("backup ", false, false)
                                                                             : juce::String ("pick a cassette");
        };
        shelf.pressLabel = "reveal";
        shelf.press = [this]
        {
            const auto v = currentView();
            if (juce::isPositiveAndBelow (selected, (int) v.shelf.size()) && pose < 0)
                v.shelf[(size_t) selected].folder.revealToUser();
        };
        shelf.available = [this] { return ! currentView().shelf.empty(); };
        shelf.reason = [] { return juce::String ("no backups yet"); };
        out[0] = shelf;

        // Read-only cells (the readout grammar): no turn, dead pills.
        auto readout = [this] (const char* label, std::function<juce::String (const View&)> value, std::function<juce::String (const View&)> unit)
        {
            EncoderBinding e;
            e.label = label;
            e.getNorm = [] { return 0.0; };
            e.turn = [] (int, encoder::Speed) {};
            e.format = [this, value] { return value (currentView()); };
            e.unit = [this, unit] { return unit (currentView()); };
            e.caption = [] { return juce::String ("read only"); };
            e.available = [this] { return currentView().plate != Plate::noDevice || ! currentView().shelf.empty(); };
            e.reason = [] { return juce::String ("no ep" + enDash + "40"); };
            return e;
        };
        auto latest = [] (const View& v) -> const Ep40Backup::Entry*
        {
            for (const auto& e : v.shelf)
                if (e.complete)
                    return &e;
            return nullptr;
        };
        out[1] = readout ("SOUNDS", [latest] (const View& v) { const auto* e = latest (v); return e != nullptr ? juce::String (e->sounds) : enDash; },
                          [] (const View&) { return juce::String(); });
        out[2] = readout ("PROJECTS", [latest] (const View& v) { const auto* e = latest (v); return e != nullptr ? juce::String (e->projects) : enDash; },
                          [] (const View&) { return juce::String(); });
        out[3] = readout ("LAST", [latest] (const View& v)
                          {
                              const auto* e = latest (v);
                              return e != nullptr ? e->createdTime().formatted ("%d.%m").trimCharactersAtStart ("0") : enDash;
                          },
                          [] (const View&) { return juce::String(); });
        return out;
    }
}
