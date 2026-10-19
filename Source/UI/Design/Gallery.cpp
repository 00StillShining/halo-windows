#include "UI/Design/Gallery.h"
#include "Core/UiTokens.h"
#include "UI/Design/Numerals.h"
#include "UI/Design/Paint.h"
#include "UI/Design/Pictograms.h"
#include "UI/Design/Primitives/ActionKey.h"
#include "UI/Design/Primitives/NameCells.h"
#include "UI/Design/Primitives/Notice.h"
#include "UI/Design/Primitives/StepKeyRow.h"

namespace halo
{
    namespace
    {
        const juce::String dot = juce::String::fromUTF8 (" \xc2\xb7 ");
        const juce::String minus = juce::String::charToString ((juce::juce_wchar) 0x2212);
        const juce::String enDash = juce::String::charToString ((juce::juce_wchar) 0x2013);

        juce::String hex (juce::Colour c) { return "#" + c.toDisplayString (false); }

        void section (juce::Graphics& g, float x, float y, float w, const juce::String& title)
        {
            const auto& t = theme();
            paint::text (g, title, { x, y, w, 16.0f }, HaloFonts::font (Role::label), t.scr.dim);
            paint::hairlineH (g, x, x + w, y + 20.0f, t.scr.rule);
        }

        // The permanent fixture band: no gallery state can pass for device state.
        void fixtureBand (juce::Graphics& g, juce::Rectangle<float> r)
        {
            paint::hazard (g, r.removeFromLeft (36.0f), 10.0f);
            g.setColour (theme().honest.mock);
            g.fillRect (r);
            paint::text (g, "FIXTURE" + dot + "GALLERY DATA, NOT DEVICE STATE", r.reduced (12.0f, 0.0f),
                         HaloFonts::font (Role::tag), juce::Colour (0xff000005));
        }

        //==========================================================================
        // Page 1: palette, type, pictograms, numerals, tags, lamps.
        // Page 2: notices, action keys, step keys, dot timelines, name cells.
        class PageOne : public juce::Component
        {
        public:
            explicit PageOne (int pageNumber) : page (pageNumber)
            {
                setOpaque (true);
                keys.add (new ActionKey ("export slices", Pict::folder, ActionKey::Variant::neutral));
                keys.add (new ActionKey ("show", Pict::none, ActionKey::Variant::neutral));
                keys.add (new ActionKey ("back up now", Pict::backup, ActionKey::Variant::primary));
                keys.add (new ActionKey ("send to pads", Pict::upload, ActionKey::Variant::deviceWrite));
                keys.add (new ActionKey ("send to pads", Pict::upload, ActionKey::Variant::lockedWrite));
                keys.add (new ActionKey ("delete", Pict::none, ActionKey::Variant::destructive));
                keys.add (new ActionKey ("send to ep" + enDash + "40", Pict::upload, ActionKey::Variant::busy));
                auto* disabled = keys.add (new ActionKey ("chop", Pict::chop, ActionKey::Variant::neutral));
                disabled->setEnabled (false);
                for (auto* k : keys)
                    addAndMakeVisible (k);

                quality.setItems ({ "original", "high", "balanced", "lo 22k", "lo 11k" });
                quality.setSelected (2);
                channels.setItems ({ "keep", "mono", "stereo" });
                channels.setSelected (1);
                groups.setItems ({ "a", "b", "c", "d" });
                groups.setSelected (0);
                dead.setItems ({ "fit", "4", "8", "16", "all" });
                dead.setRowEnabled (false);
                for (auto* r : { &quality, &channels, &groups, &dead })
                    addAndMakeVisible (r);

                name.setText ("LONGSONG");
                addAndMakeVisible (name);

                blocker.set (NoticeData::blocker ("16 slices are over 20 s (longest 31.5 s), the EP-40's limit per sound. "
                                                  "Turn on AUTO-TRIM, use more slices, or CLOSE and trim in EDIT."));
                info.set (NoticeData::info ("AUTO-TRIM: over 20 s, so SEND sends the first 20 s of the selection, with a fade-out. "
                                            "Drag the trim handles to pick another part, or CHOP it."));
                plain.set (NoticeData::plain ("Back up the EP-40 in BACKUPS (Ctrl+6) to unlock SEND TO PADS. EXPORT works now."));
                for (auto* n : { &blocker, &info, &plain })
                    addAndMakeVisible (n);
                if (page != 2)
                    for (auto* c : getChildren())
                        c->setVisible (false);
                setSize (1440, 900);
            }

            void resized() override
            {
                // Page 2, left column: notices, dot timelines, name cells.
                const float lx = 24.0f, lw = 680.0f;
                float y = 76.0f;
                for (auto* n : { &blocker, &info, &plain })
                {
                    n->setBounds ((int) lx, (int) y, (int) lw, n->heightForWidth ((int) lw));
                    y += (float) n->getHeight() + 12.0f;
                }
                noticesBottom = (int) y;
                name.setBounds ((int) lx, noticesBottom + 190, NameCells::widthForHeight (36), 36);

                // Right column: action keys, then step keys.
                const int rx = 744, rw = 672;
                int x = rx, ky = 76;
                for (auto* k : keys)
                {
                    if (x + k->preferredWidth() > rx + rw)
                    {
                        x = rx;
                        ky += ActionKey::height + 34;
                    }
                    k->setBounds (x, ky, k->preferredWidth(), ActionKey::height);
                    x += k->preferredWidth() + 16;
                }
                keysBottom = ky + ActionKey::height + 30;
                quality.setBounds (rx, keysBottom + 36, rw, StepKeyRow::height);
                channels.setBounds (rx, keysBottom + 36 + 42, 300, StepKeyRow::height);
                groups.setBounds (rx + 320, keysBottom + 36 + 42, 180, StepKeyRow::height);
                dead.setBounds (rx, keysBottom + 36 + 84, 300, StepKeyRow::height);
            }

            void paint (juce::Graphics& g) override
            {
                const auto& t = theme();
                g.fillAll (t.scr.bg);
                fixtureBand (g, { 0.0f, 0.0f, (float) getWidth(), 26.0f });

                if (page != 2)
                {
                    paintPalette (g, 24.0f, 44.0f);
                    paintType (g, 24.0f, 500.0f);
                    paintPictograms (g, 744.0f, 44.0f);
                    paintNumerals (g, 744.0f, 312.0f);
                    paintTagsAndLamps (g, 744.0f, 590.0f);
                    return;
                }

                section (g, 24.0f, 44.0f, 680.0f, "NOTICES" + dot + "BLOCKER" + dot + "INFO" + dot + "PLAIN (LONGEST VERBATIM COPY)");
                section (g, 744.0f, 44.0f, 672.0f, "ACTION KEYS" + dot + "NEUTRAL" + dot + "PRIMARY" + dot + "DEVICE WRITE" + dot
                                                       + "LOCKED" + dot + "DESTRUCTIVE" + dot + "BUSY");
                for (auto* k : keys)
                {
                    const auto caption = ActionKey::captionFor (k->getVariant());
                    if (caption.isNotEmpty())
                        paint::text (g, caption, { (float) k->getX(), (float) k->getBottom() + 4.0f, 320.0f, 16.0f },
                                     HaloFonts::font (Role::print), t.scr.dim);
                }
                section (g, 744.0f, (float) keysBottom + 8.0f, 672.0f, "STEP KEYS" + dot + "SELECTED" + dot + "PAIRED SHADING" + dot + "DISABLED");
                paint::text (g, "even " + enDash + " no detection", { 1060.0f, (float) keysBottom + 36.0f + 84.0f, 300.0f, 30.0f },
                             HaloFonts::font (Role::ui), t.scr.dim);

                const float dy = (float) noticesBottom + 20.0f;
                section (g, 24.0f, dy, 680.0f, "DOT TIMELINES");
                paint::dotTimeline (g, { 24.0f, dy + 36.0f, 470.0f, 10.0f }, 61, 50, 3);
                paint::text (g, "99.7 used" + dot + "+5.2 these" + dot + "17.0 free", { 510.0f, dy + 30.0f, 200.0f, 22.0f },
                             HaloFonts::font (Role::print), t.scr.dim);
                paint::dotTimeline (g, { 24.0f, dy + 64.0f, 470.0f, 10.0f }, 61, 56, 1, true);
                paint::text (g, "above 90%: free dots crimson", { 510.0f, dy + 58.0f, 200.0f, 22.0f }, HaloFonts::font (Role::print), t.scr.dim);

                section (g, 24.0f, (float) noticesBottom + 140.0f, 680.0f, "NAME CELLS" + dot + "PIXEL FACE ON THE DEVICE GRID");
                paint::text (g, "a" + enDash + "z 0" + enDash + "9" + dot + "8 max",
                             { 24.0f + (float) NameCells::widthForHeight (36) + 16.0f, (float) name.getY(), 200.0f, 36.0f },
                             HaloFonts::font (Role::ui), t.scr.dim);
            }

        private:
            void swatch (juce::Graphics& g, float x, float y, juce::Colour c, const juce::String& label, bool showContrast)
            {
                const auto& t = theme();
                g.setColour (c);
                g.fillRoundedRectangle (x, y, 58.0f, 30.0f, 3.0f);
                g.setColour (t.scr.rule);
                g.drawRoundedRectangle (x + 0.5f, y + 0.5f, 57.0f, 29.0f, 3.0f, 1.0f);
                paint::text (g, label, { x, y + 33.0f, 66.0f, 13.0f }, HaloFonts::font (Role::mono), t.scr.ink);
                paint::text (g, hex (c).toLowerCase(), { x, y + 46.0f, 66.0f, 13.0f }, HaloFonts::font (Role::mono), t.scr.dim);
                if (showContrast)
                    paint::text (g, juce::String (tokens::contrast (c.getARGB() & 0xFFFFFF, t.scr.bg.getARGB() & 0xFFFFFF), 2) + ":1",
                                 { x, y + 59.0f, 66.0f, 13.0f }, HaloFonts::font (Role::mono), t.scr.ramp[5]);
            }

            void paintPalette (juce::Graphics& g, float x, float y)
            {
                const auto& t = theme();
                section (g, x, y, 680.0f, "PALETTE" + dot + t.name.toUpperCase() + dot + "CONTRAST ON SCR.BG");
                y += 30.0f;
                const std::pair<juce::Colour, const char*> screen[] {
                    { t.scr.bg, "bg" }, { t.scr.panel, "panel" }, { t.scr.wave, "wave" }, { t.scr.waveSel, "waveSel" },
                    { t.scr.rule, "rule" }, { t.scr.frame, "frame" }, { t.scr.mute, "mute" }, { t.scr.dim, "dim" },
                    { t.scr.ink, "ink" }, { t.scr.hot, "hot" } };
                float sx = x;
                for (const auto& [c, n] : screen) { swatch (g, sx, y, c, n, true); sx += 68.0f; }
                y += 84.0f;
                sx = x;
                for (int i = 0; i < 8; ++i) { swatch (g, sx, y, t.scr.ramp[(size_t) i], "ramp" + juce::String (i), true); sx += 68.0f; }
                swatch (g, sx, y, t.scr.ledOff, "led.off", false);
                swatch (g, sx + 68.0f, y, t.scr.hatch, "hatch", false);
                y += 84.0f;
                sx = x;
                const char* encNames[] { "enc1", "enc2", "enc3", "enc4" };
                for (int i = 0; i < 4; ++i)
                {
                    const auto& e = t.encoder (i);
                    swatch (g, sx, y, e.line, encNames[i], true);
                    // fill with its text-on-fill sample
                    g.setColour (e.fill);
                    g.fillRoundedRectangle (sx + 68.0f, y, 58.0f, 30.0f, 3.0f);
                    paint::text (g, "fill", { sx + 68.0f, y, 58.0f, 30.0f }, HaloFonts::font (Role::tag), e.onFill, juce::Justification::centred);
                    paint::text (g, juce::String (tokens::contrast (e.onFill.getARGB() & 0xFFFFFF, e.fill.getARGB() & 0xFFFFFF), 2),
                                 { sx + 68.0f, y + 33.0f, 70.0f, 13.0f }, HaloFonts::font (Role::mono), t.scr.dim);
                    sx += 68.0f * 2.0f + 6.0f;
                }
                y += 84.0f;
                sx = x;
                const std::pair<juce::Colour, const char*> honestHues[] {
                    { t.honest.mock, "mock" }, { t.honest.preview, "preview" }, { t.honest.danger, "danger" }, { t.honest.info, "info" } };
                for (const auto& [c, n] : honestHues) { swatch (g, sx, y, c, n, true); sx += 68.0f; }
                const std::pair<juce::Colour, const char*> paper[] {
                    { t.scr.paper, "paper" }, { t.scr.paperHi, "paperHi" }, { t.scr.paperShade, "paperSh" }, { t.scr.tapeEmpty, "tape" }, { t.scr.tapeWound, "wound" } };
                sx += 16.0f;
                for (const auto& [c, n] : paper) { swatch (g, sx, y, c, n, false); sx += 68.0f; }
                y += 84.0f;
                sx = x;
                const std::pair<juce::Colour, const char*> deck[] {
                    { t.deck.top, "deck" }, { t.deck.bottom, "deck.b" }, { t.deck.key, "key" }, { t.deck.keyDown, "key.dn" },
                    { t.deck.keyShift, "shift" }, { t.deck.print, "print" }, { t.deck.printCap, "print.cap" }, { t.deck.grille, "grille" } };
                for (const auto& [c, n] : deck) { swatch (g, sx, y, c, n, false); sx += 68.0f; }
            }

            void paintType (juce::Graphics& g, float x, float y)
            {
                const auto& t = theme();
                section (g, x, y, 680.0f, "TYPE" + dot + "EM SIZES" + dot + "NEVER SCALES WITH THE WINDOW");
                y += 32.0f;
                struct Row { Role role; juce::String sample; const char* spec; };
                const Row rows[] {
                    { Role::title, "LONGSONG.01  halo", "title  lexend giga 300  19 / +0.06" },
                    { Role::label, "SENSITIVITY  LENGTH  EP" + enDash + "40 QUALITY", "label  lexend exa 500  10.5 / +0.12" },
                    { Role::tag, "MOCK  PREVIEW  AUTO-TRIM  WRITES LOCKED", "tag  lexend exa 600  10.5 / +0.10" },
                    { Role::ui, "transient  even  cancel  done  speakers (realtek)", "ui  inter tight 400  12.5" },
                    { Role::uiLight, "balanced  4.0 s  mono", "ui.light  inter tight 300  15" },
                    { Role::body, "Turn on AUTO-TRIM, use more slices, or CLOSE and trim in EDIT.", "body  inter tight 400  13" },
                    { Role::print, "play  load  edit  chop  capture  rack  backups", "print  inter tight 400  11" },
                    { Role::mono, "17:31:04  note on c2 vel 96  ~40 kb/s", "mono  jetbrains mono 400  10.5" },
                    { Role::pixel, "LONGSONG", "pixel  departure mono  2 x 11 px" },
                };
                for (const auto& r : rows)
                {
                    const auto font = HaloFonts::font (r.role);
                    const float h = juce::jmax (22.0f, font.getHeight() + 6.0f);
                    paint::text (g, r.sample, { x, y, 430.0f, h }, font, t.scr.ink);
                    paint::text (g, r.spec, { x + 440.0f, y, 240.0f, h }, HaloFonts::font (Role::mono), t.scr.dim);
                    y += h + 8.0f;
                }
            }

            void paintNumerals (juce::Graphics& g, float x, float y)
            {
                const auto& t = theme();
                section (g, x, y, 680.0f, "HALO NUMERALS" + dot + "XL 46" + dot + "L 36" + dot + "M 26" + dot + "S 20" + dot + "XS 16");
                y += 32.0f;
                numerals::draw (g, "0123456789", x, y + 46.0f, numerals::XL, t.scr.ink);
                numerals::drawWithUnit (g, "12", "slices", x + 400.0f, y + 46.0f, numerals::XL, t.scr.ink, t.scr.dim);
                y += 64.0f;
                numerals::drawWithUnit (g, "28.60", "s", x, y + 36.0f, numerals::L, t.encoder (3).line, t.encoder (3).line);
                numerals::drawWithUnit (g, minus + "12", "db", x + 190.0f, y + 36.0f, numerals::L, t.encoder (0).line, t.encoder (0).line);
                numerals::drawWithUnit (g, "4.0", "s", x + 360.0f, y + 36.0f, numerals::L, t.encoder (2).line, t.encoder (2).line);
                numerals::drawWithUnit (g, "62", "%", x + 500.0f, y + 36.0f, numerals::L, t.encoder (0).line, t.encoder (0).line);
                y += 54.0f;
                numerals::drawWithUnit (g, "5.21", "mb", x, y + 26.0f, numerals::M, t.scr.ink, t.scr.dim);
                numerals::drawWithUnit (g, "~2:10", "min to send", x + 150.0f, y + 26.0f, numerals::M, t.scr.ink, t.scr.dim);
                numerals::drawWithUnit (g, "180.0", "bpm", x + 380.0f, y + 26.0f, numerals::M, t.scr.ink, t.scr.dim);
                numerals::draw (g, enDash, x + 560.0f, y + 26.0f, numerals::M, t.scr.dim);
                y += 40.0f;
                numerals::draw (g, "1 2 3 10 11 12", x, y + 20.0f, numerals::S, t.scr.ink);
                numerals::draw (g, "42" + juce::String::charToString ((juce::juce_wchar) 0x00B0) + " 99.7% 58/477", x + 260.0f, y + 20.0f, numerals::S, t.scr.ink);
                numerals::draw (g, "00:12:40", x + 520.0f, y + 16.0f, numerals::XS, t.scr.ink);
            }

            void paintPictograms (juce::Graphics& g, float x, float y)
            {
                const auto& t = theme();
                section (g, x, y, 672.0f, "PICTOGRAMS" + dot + "16 PX GRID" + dot + "1.5 PX STROKE");
                y += 32.0f;
                const int perRow = 10;
                for (int i = 1; i < (int) Pict::count; ++i)
                {
                    const int idx = i - 1;
                    const float px = x + (float) (idx % perRow) * 67.0f;
                    const float py = y + (float) (idx / perRow) * 54.0f;
                    const bool fill = (Pict) i == Pict::rec;
                    pictograms::draw (g, (Pict) i, { px + 18.0f, py, 22.0f, 22.0f }, (Pict) i == Pict::rec ? t.honest.danger : t.scr.ink, 1.5f, fill);
                    paint::text (g, pictograms::name ((Pict) i), { px - 4.0f, py + 26.0f, 66.0f, 14.0f }, HaloFonts::font (Role::print, 10.5f),
                                 t.scr.dim, juce::Justification::centred);
                }
            }

            void paintTagsAndLamps (juce::Graphics& g, float x, float y)
            {
                const auto& t = theme();
                section (g, x, y, 672.0f, "TAGS" + dot + "LAMPS");
                y += 32.0f;
                float tx = x;
                auto put = [&] (const juce::String& s, paint::TagStyle st, juce::Colour fill = {}, juce::Colour text = {})
                {
                    const auto r = paint::tag (g, { tx, y }, s, st, fill, text);
                    tx = r.getRight() + 10.0f;
                };
                put ("MOCK", paint::TagStyle::mock);
                put ("PREVIEW", paint::TagStyle::preview);
                put ("REC 00:12", paint::TagStyle::danger);
                put ("ENGAGED", paint::TagStyle::filled);
                put ("BYPASSED", paint::TagStyle::outline);
                put ("INCOMPLETE", paint::TagStyle::hatched);
                y += 30.0f;
                tx = x;
                put ("AUTO-TRIM", paint::TagStyle::filled, t.encoder (2).fill, t.encoder (2).onFill);
                put ("FIT", paint::TagStyle::filled, t.encoder (1).fill, t.encoder (1).onFill);
                put ("GRAB", paint::TagStyle::outline, t.encoder (1).line, t.encoder (1).line);
                put ("TAKE", paint::TagStyle::filled);
                put ("FILE", paint::TagStyle::outline);
                put ("LIMITS 7", paint::TagStyle::outline, t.encoder (3).line, t.encoder (3).line);
                y += 30.0f;
                tx = x;
                put ("WRITES LOCKED", paint::TagStyle::outline);
                put ("WRITES UNLOCKED", paint::TagStyle::filled);
                put ("ASSIGN ARMED", paint::TagStyle::filled, t.scr.hot, juce::Colour (0xff000005));
                y += 40.0f;

                struct L { const char* what; int kind; juce::Colour c; };
                const L lamps[] { { "live", 0, t.scr.hot }, { "off / locked", 1, t.scr.dim }, { "wait (blinks 1 hz)", 1, t.scr.dim },
                                  { "mock", 0, t.honest.mock }, { "preview", 1, t.honest.preview }, { "error / rec", 0, t.honest.danger } };
                float lx = x;
                for (const auto& l : lamps)
                {
                    if (l.kind == 0) paint::lamp (g, { lx + 4.0f, y + 8.0f }, l.c);
                    else             paint::hollowLamp (g, { lx + 4.0f, y + 8.0f }, l.c);
                    paint::text (g, l.what, { lx + 14.0f, y, 100.0f, 16.0f }, HaloFonts::font (Role::ui), t.scr.dim);
                    lx += 112.0f;
                }
                y += 30.0f;
                paint::text (g, "deck leds", { x, y, 80.0f, 16.0f }, HaloFonts::font (Role::print), t.scr.dim);
                g.setColour (t.deck.top);
                g.fillRoundedRectangle (x + 80.0f, y - 6.0f, 160.0f, 28.0f, 4.0f);
                paint::deckLed (g, { x + 100.0f, y + 8.0f }, t.deck.ledOn);
                paint::deckLed (g, { x + 130.0f, y + 8.0f }, t.honest.danger);
                paint::lamp (g, { x + 160.0f, y + 8.0f }, t.deck.ledOffDeck, 5.0f);
                paint::hollowLamp (g, { x + 190.0f, y + 8.0f }, t.deck.print, 6.0f, 1.2f);
                paint::text (g, "hatch", { x + 280.0f, y, 60.0f, 16.0f }, HaloFonts::font (Role::print), t.scr.dim);
                g.setColour (t.scr.droppedTail);
                g.fillRect (x + 330.0f, y - 6.0f, 90.0f, 28.0f);
                paint::hatch (g, { x + 330.0f, y - 6.0f, 90.0f, 28.0f }, t.scr.hatch);
                paint::text (g, "hazard", { x + 440.0f, y, 60.0f, 16.0f }, HaloFonts::font (Role::print), t.scr.dim);
                paint::hazard (g, { x + 496.0f, y - 6.0f, 90.0f, 28.0f });
                paint::dashedRect (g, { x + 600.0f, y - 6.0f, 70.0f, 28.0f }, t.honest.preview);
            }

            juce::OwnedArray<ActionKey> keys;
            StepKeyRow quality, channels, groups, dead;
            NameCells name;
            Notice blocker, info, plain;
            int page = 1;
            int keysBottom = 400, noticesBottom = 300;
        };

        //==========================================================================
        class GalleryShell : public juce::Component
        {
        public:
            explicit GalleryShell (std::unique_ptr<juce::Component> page) : content (std::move (page))
            {
                setOpaque (true);
                viewport.setViewedComponent (content.get(), false);
                viewport.setScrollBarsShown (true, true);
                addAndMakeVisible (viewport);
                setSize (1440, 900);
            }
            void paint (juce::Graphics& g) override { g.fillAll (theme().scr.bg); }
            void resized() override { viewport.setBounds (getLocalBounds()); }

        private:
            std::unique_ptr<juce::Component> content;
            juce::Viewport viewport;
        };
    }

    std::unique_ptr<juce::Component> createGallery (int page)
    {
        return std::make_unique<GalleryShell> (std::make_unique<PageOne> (page));
    }
}
