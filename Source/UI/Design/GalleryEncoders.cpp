// Gallery pages 3 and 4 (R2b): the encoder system, pads and meters, all from
// fixture data under the permanent FIXTURE band.
#include "App/DevPose.h"
#include "UI/Design/EncoderBus.h"
#include "UI/Design/Numerals.h"
#include "UI/Design/Paint.h"
#include "UI/Design/Primitives/DeckControls.h"
#include "UI/Design/Primitives/Gauges.h"
#include "UI/Design/Primitives/PadGrid.h"
#include "UI/Design/Primitives/ParamStrip.h"

namespace halo
{
    namespace
    {
        const juce::String dot = juce::String::fromUTF8 (" \xc2\xb7 ");
        const juce::String plusMinus = juce::String::charToString ((juce::juce_wchar) 0x00B1);

        void band (juce::Graphics& g, juce::Rectangle<float> r)
        {
            g.setColour (theme().scr.panel);
            g.fillRect (r);
            paint::hairlineH (g, r.getX(), r.getRight(), r.getBottom() - 1.0f, theme().scr.frame);
            paint::text (g, "FIXTURE" + dot + "GALLERY DATA, NOT DEVICE STATE", r.reduced (24.0f, 0.0f),
                         HaloFonts::font (Role::tag), theme().scr.ink);
        }

        void title (juce::Graphics& g, float x, float y, float w, const juce::String& s)
        {
            paint::text (g, s, { x, y, w, 16.0f }, HaloFonts::font (Role::label), theme().scr.dim);
            paint::hairlineH (g, x, x + w, y + 20.0f, theme().scr.rule);
        }

        // Fixture values for a CHOP-like page.
        struct Fixture
        {
            double sensitivity = 0.62, slices = 0.0, length = 0.158, slice = 0.6;
            bool autoTrim = true;

            std::array<std::optional<EncoderBinding>, 4> bindings()
            {
                std::array<std::optional<EncoderBinding>, 4> b;
                EncoderBinding s;
                s.label = "SENSITIVITY";
                s.getNorm = [this] { return sensitivity; };
                s.setNorm = [this] (double v) { sensitivity = v; };
                s.reset = [this] { sensitivity = 0.5; };
                s.step = 0.01;
                s.format = [this] { return juce::String (juce::roundToInt (sensitivity * 100.0)); };
                s.unit = [] { return juce::String ("%"); };
                s.caption = [] { return juce::String ("311 cuts seen"); };
                s.pressLabel = "detect";
                s.press = [] {};
                s.shiftPressLabel = "clear cuts";
                s.shiftPress = [] {};
                s.gauge = [this] (juce::Graphics& g, juce::Rectangle<float> r, juce::Colour c, bool) { gauges::tickDial (g, r, sensitivity, c); };
                s.hint = [] { return juce::String ("wheel " + plusMinus + "1%" + dot + "detects when you stop"); };
                b[0] = s;

                EncoderBinding n;
                n.label = "SLICES";
                n.steps = 5;
                n.getNorm = [this] { return slices; };
                n.setNorm = [this] (double v) { slices = v; };
                n.format = [] { return juce::String ("12"); };
                n.caption = [] { return juce::String ("fit" + dot + "4" + dot + "8" + dot + "16" + dot + "all"); };
                n.tag = [] { return juce::String ("FIT"); };
                n.pressLabel = "even";
                n.press = [] {};
                n.gauge = [] (juce::Graphics& g, juce::Rectangle<float> r, juce::Colour c, bool) { gauges::dotRow (g, r, 16, 12, c); };
                b[1] = n;

                EncoderBinding l;
                l.label = "LENGTH";
                l.getNorm = [this] { return length; };
                l.setNorm = [this] (double v) { length = v; };
                l.reset = [this] { length = 1.0; };
                l.step = 0.5 / 19.0;
                l.format = [this] { return juce::String (1.0 + length * 19.0, 1); };
                l.unit = [] { return juce::String ("s"); };
                l.caption = [] { return juce::String ("7 slices cut"); };
                l.tag = [] { return juce::String ("AUTO-TRIM"); };
                l.tagOn = [this] { return autoTrim; };
                l.pressLabel = "auto-trim";
                l.press = [this] { autoTrim = ! autoTrim; };
                l.gauge = [this] (juce::Graphics& g, juce::Rectangle<float> r, juce::Colour c, bool) { gauges::ruler (g, r, length, c, 1.0); };
                l.hint = [] { return juce::String ("wheel " + plusMinus + "0.5 s" + dot + "alt fine"); };
                b[2] = l;

                EncoderBinding m;
                m.label = "SLICE";
                m.getNorm = [this] { return slice; };
                m.setNorm = [this] (double v) { slice = v; };
                m.step = 1.0 / 11.0;
                m.format = [this] { return juce::String (1 + juce::roundToInt (slice * 11.0)); };
                m.unit = [] { return juce::String ("of 12"); };
                m.pressLabel = "audition";
                m.press = [] {};
                m.shiftLabel = "NUDGE";
                m.shiftTurn = [] (int) {};
                m.gauge = [this] (juce::Graphics& g, juce::Rectangle<float> r, juce::Colour c, bool) { gauges::timeline (g, r, slice, 11, c); };
                b[3] = m;
                return b;
            }
        };

        //======================================================================
        class EncoderPage : public juce::Component, private juce::Timer
        {
        public:
            EncoderPage()
            {
                setOpaque (true);
                bus.setBindings (fixture.bindings());

                // A second bus: a dead cell (EVEN), a touched one, a dead slot.
                auto dead = fixture.bindings();
                dead[0]->available = [] { return false; };
                dead[0]->reason = [] { return juce::String ("even " + juce::String::charToString ((juce::juce_wchar) 0x2013) + " no detection"); };
                dead[3].reset();
                busB.setBindings (dead);
                busB.pinCard (2);

                const int pose = DevPose::get ("card");
                if (pose >= 1 && pose <= 4)
                    bus.pinCard (pose - 1);
                if (DevPose::get ("shift") == 1)
                    bus.pinShift (true);

                const char* names[] { "play", "load", "edit", "chop", "capture", "rack", "backups" };
                const Pict icons[] { Pict::play, Pict::load, Pict::edit, Pict::chop, Pict::capture, Pict::rack, Pict::backup };
                const char* keysText[] { "1", "2", "3", "7", "4", "5", "6" };
                for (int i = 0; i < 7; ++i)
                {
                    auto* k = keys.add (new DeckKey (names[i], icons[i], keysText[i]));
                    addAndMakeVisible (k);
                }
                keys[3]->setActive (true);
                keys[3]->setLed (DeckKey::Led::on);
                keys[2]->setLed (DeckKey::Led::dim);
                keys[1]->setLed (DeckKey::Led::blink);
                keys[6]->setLed (DeckKey::Led::hollow);
                keys[4]->setLed (DeckKey::Led::crimson);
                addAndMakeVisible (shiftKey);
                addAndMakeVisible (recKey);
                recKey.setLed (DeckKey::Led::off);

                for (int i = 0; i < 4; ++i)
                {
                    auto* e = encoders.add (new DeckEncoder (bus, i));
                    addAndMakeVisible (e);
                    auto* c = cells.add (new ParamCell (bus, i));
                    addAndMakeVisible (c);
                    auto* c2 = compactCells.add (new ParamCell (busB, i));
                    c2->setSize (CellSize::compact);
                    addAndMakeVisible (c2);
                }
                addAndMakeVisible (pills);
                pills.setHint ("drag: orbit" + dot + "wheel on stage: zoom" + dot + "double-click: reset");
                addAndMakeVisible (pillsB);
                startTimer (500);
                setSize (1440, 900);
            }

            void resized() override
            {
                // Strip geometry exactly as the 1440 frame (plan section 3.1):
                // cells from x 532, 213.5 px with 10 px gutters.
                const float stripX = 532.0f, cellW = 213.5f, gutter = 10.0f;
                std::array<float, 4> centres {};
                for (int i = 0; i < 4; ++i)
                {
                    const float x = stripX + (cellW + gutter) * (float) i;
                    centres[(size_t) i] = x + cellW * 0.5f;
                    cells[i]->setBounds (juce::Rectangle<float> (x, 330.0f, cellW, 86.0f).toNearestInt());
                    compactCells[i]->setBounds (juce::Rectangle<float> (x, 470.0f, cellW, 72.0f).toNearestInt());
                    const int hs = DeckEncoder::hitSize (56);
                    encoders[i]->setBounds ((int) std::round (centres[(size_t) i] - hs * 0.5f), 794, hs, 96);
                }
                pills.setBounds (24, 422, 1392, 32);
                std::array<float, 4> local {};
                for (int i = 0; i < 4; ++i)
                    local[(size_t) i] = centres[(size_t) i] - 24.0f;
                pills.setCentres (local);
                pills.setHintWidth (480.0f);
                pillsB.setBounds (24, 548, 1392, 30);
                pillsB.setCentres (local);
                pillsB.setPillSize (120.0f, 22.0f);

                int x = 76;
                for (auto* k : keys)
                {
                    k->setBounds (x, 798, 44, DeckKey::heightFor (44, true));
                    x += 50;
                }
                shiftKey.setBounds (432, 798, 44, DeckKey::heightFor (44, true));
                recKey.setBounds (480, 798, 44, DeckKey::heightFor (44, true));
            }

            void paint (juce::Graphics& g) override
            {
                const auto& t = theme();
                g.fillAll (t.scr.bg);
                band (g, { 0.0f, 0.0f, (float) getWidth(), 26.0f });

                title (g, 24.0f, 44.0f, 1392.0f, "POPUP CARD" + dot + "ALL FOUR ENCODERS" + dot + "DOCKS IN THE LANE");
                for (int i = 0; i < 4; ++i)
                {
                    const float x = i % 2 == 0 ? 24.0f : 540.0f;
                    const float y = 76.0f + (float) (i / 2) * 100.0f;
                    if (const auto* b = bus.binding (i))
                        popupCard::paint (g, { x, y, 496.0f, 86.0f }, *b, i, false);
                }
                // compact card
                if (const auto* b = bus.binding (2))
                    popupCard::paint (g, { 1056.0f, 76.0f, 360.0f, 72.0f }, *b, 2, true);
                paint::text (g, "compact 400 x 72", { 1056.0f, 152.0f, 200.0f, 14.0f }, HaloFonts::font (Role::print), t.scr.dim);

                title (g, 24.0f, 300.0f, 1392.0f, "PARAM STRIP" + dot + "LIVE" + dot + "VALUE BARS" + dot + "PILLS UNDER EACH ENCODER");
                // The lane, left of the strip.
                if (bus.pinnedCardIndex() >= 0 || bus.touched() >= 0)
                {
                    const int i = bus.pinnedCardIndex() >= 0 ? bus.pinnedCardIndex() : bus.touched();
                    if (const auto* b = bus.binding (i); b != nullptr && bus.isTouched (i))
                        popupCard::paint (g, { 24.0f, 330.0f, 496.0f, 86.0f }, *b, i, false);
                }
                else
                {
                    paint::text (g, "lane: clock, keycap hints (redundant content only)", { 24.0f, 330.0f, 496.0f, 86.0f },
                                 HaloFonts::font (Role::ui), t.scr.dim);
                }
                title (g, 24.0f, 446.0f + 0.0f, 1392.0f, "");
                paint::text (g, "compact cells: dead with its reason" + dot + "touched (pinned)" + dot + "tag off" + dot + "no binding",
                             { 24.0f, 470.0f, 496.0f, 72.0f }, HaloFonts::font (Role::ui), t.scr.dim);

                title (g, 24.0f, 590.0f, 1392.0f, "READOUT CELLS" + dot + "READ-ONLY, RAMP-FILLED, NO TOP BAR");
                const char* labels[] { "pad", "note", "velocity", "channel" };
                const char* values[] { "a5", "c2", "96", "all" };
                for (int i = 0; i < 4; ++i)
                    readout::paint (g, { 24.0f + (float) i * 92.0f, 620.0f, 88.0f, 62.0f }, 1 + i * 2, labels[i], values[i], i == 2, 26.0f);
                // same cells with text values
                readout::paint (g, { 420.0f, 620.0f, 88.0f, 62.0f }, 1, "pad", "a 5", false, 22.0f);
                readout::paint (g, { 512.0f, 620.0f, 88.0f, 62.0f }, 3, "note", "c2", false, 22.0f);

                // The deck: graphite, the only gradient in the app.
                auto deck = juce::Rectangle<float> (0.0f, 780.0f, (float) getWidth(), 120.0f);
                g.setGradientFill (juce::ColourGradient::vertical (t.deck.top, deck.getY(), t.deck.bottom, deck.getBottom()));
                g.fillRect (deck);
                paint::hairlineH (g, 0.0f, (float) getWidth(), deck.getY(), t.deck.edge);
                // speaker grille under the lane's left
                g.setColour (t.deck.grille);
                for (int row = 0; row < 7; ++row)
                    for (int col = 0; col < 6; ++col)
                        g.fillEllipse (24.0f + (float) col * 7.0f + (row % 2 ? 3.5f : 0.0f), 800.0f + (float) row * 7.0f, 2.5f, 2.5f);
                paint::text (g, "mode", { 76.0f, 784.0f, 60.0f, 12.0f }, HaloFonts::font (Role::print, 10.5f), t.deck.print);
                paint::text (g, "encoders" + dot + "wheel turn" + dot + "alt fine" + dot + "ctrl coarse" + dot + "click press",
                             { 900.0f, 784.0f, 516.0f, 12.0f }, HaloFonts::font (Role::print, 10.5f), t.deck.print, juce::Justification::centredRight);
            }

        private:
            void timerCallback() override
            {
                blink = ! blink;
                keys[1]->setBlinkPhase (blink);
            }

            Fixture fixture;
            EncoderBus bus, busB;
            juce::OwnedArray<DeckKey> keys;
            DeckKey shiftKey { "shift", Pict::none, {}, DeckKey::Style::shift };
            DeckKey recKey { "rec ctrl r", Pict::rec, {}, DeckKey::Style::rec };
            juce::OwnedArray<DeckEncoder> encoders;
            juce::OwnedArray<ParamCell> cells, compactCells;
            SoftKeyRow pills { bus }, pillsB { busB };
            bool blink = true;
        };

        //======================================================================
        class MetersPage : public juce::Component, private juce::Timer
        {
        public:
            MetersPage()
            {
                setOpaque (true);
                std::array<PadGrid::Pad, 12> chop {};
                for (int i = 0; i < 12; ++i)
                {
                    chop[(size_t) i].numeral = juce::String (i + 1);
                    chop[(size_t) i].trimmed = i % 3 != 2 && i != 4 && i != 7;
                }
                chop[0].start = true;
                chop[4].selected = true;
                chopPads.setPads (chop);

                std::array<PadGrid::Pad, 12> load {};
                const char* names[] { "BIGKICK", "SNARE 2", "RIM", "HAT CL", "HAT OP", "", "DUB SIREN", "AIRHORN", "", "SUB 808", "CLAP", "VOX RIDDIM" };
                for (int i = 0; i < 12; ++i)
                {
                    load[(size_t) i].name = names[i];
                    load[(size_t) i].empty = juce::String (names[i]).isEmpty();
                    if (! load[(size_t) i].empty)
                        load[(size_t) i].corner = juce::String (100 + i * 7);
                }
                load[6].selected = true;
                loadPads.setPads (load);
                armedPads.setPads (load);
                armedPads.setArmed (true);
                for (auto* p : { &chopPads, &loadPads, &armedPads })
                    addAndMakeVisible (p);
                for (auto* a : { &arcL, &arcR })
                {
                    addAndMakeVisible (a);
                    a->setRunning (true);
                }
                motion = std::make_unique<MotionClock> (*this);
                startTimerHz (30);
                setSize (1440, 900);
            }

            void resized() override
            {
                chopPads.setBounds (24, 76, 330, 4 * 48 + 3 * PadGrid::gap);
                loadPads.setBounds (380, 76, 440, 4 * 56 + 3 * PadGrid::gap);
                armedPads.setBounds (846, 76, 440, 4 * 56 + 3 * PadGrid::gap);
                arcL.setBounds (24, 420, 170, 110);
                arcR.setBounds (210, 420, 170, 110);
            }

            void paint (juce::Graphics& g) override
            {
                const auto& t = theme();
                g.fillAll (t.scr.bg);
                band (g, { 0.0f, 0.0f, (float) getWidth(), 26.0f });
                title (g, 24.0f, 44.0f, 1392.0f, "PAD GRID" + dot + "SLICES (START, SELECTED, TRIMMED)" + dot + "SOUNDS (EMPTY)" + dot + "ASSIGN ARMED");
                title (g, 24.0f, 390.0f, 1392.0f, "METERS" + dot + "LEVEL ARCS IN DBFS (TAP OUT)" + dot + "LED LADDERS" + dot + "TAPE LANE (GRAB)");

                // LEVEL numeral and its 20-segment ladder (encoder 1 owns it).
                const auto blue = t.encoder (0).line;
                paint::lamp (g, { 430.0f, 430.0f }, blue, 7.0f);
                paint::text (g, "LEVEL", { 440.0f, 424.0f, 100.0f, 12.0f }, HaloFonts::font (Role::label), blue);
                numerals::drawWithUnit (g, juce::String::charToString ((juce::juce_wchar) 0x2212) + "12", "db", 424.0f, 490.0f, numerals::L, blue, blue);
                gauges::ladder (g, { 424.0f, 502.0f, 200.0f, 10.0f }, 20, 12, blue);
                // vertical out ladders (the deck's VU window style)
                const int litL = juce::jlimit (0, 12, (int) ((levelL + 40.0f) / 40.0f * 12.0f));
                const int litR = juce::jlimit (0, 12, (int) ((levelR + 40.0f) / 40.0f * 12.0f));
                gauges::ladder (g, { 660.0f, 420.0f, 10.0f, 96.0f }, 12, litL, t.scr.ink, true);
                gauges::ladder (g, { 676.0f, 420.0f, 10.0f, 96.0f }, 12, litR, t.scr.ink, true);
                paint::text (g, "out", { 652.0f, 520.0f, 40.0f, 12.0f }, HaloFonts::font (Role::print, 10.5f), t.scr.dim, juce::Justification::centred);

                // GRAB tape lane: 30 s of history, 8 bars (10.7 s) bracketed in ochre.
                const auto ochre = t.encoder (1).line;
                paint::text (g, "history 30 s", { 760.0f, 450.0f, 100.0f, 12.0f }, HaloFonts::font (Role::print, 10.5f), t.scr.dim);
                gauges::tapeLane (g, { 760.0f, 470.0f, 560.0f, 24.0f }, 30.0, 26.0, 10.7, ochre);
                paint::text (g, "now", { 1290.0f, 500.0f, 30.0f, 12.0f }, HaloFonts::font (Role::print, 10.5f), t.scr.dim, juce::Justification::centredRight);

                // gauges
                title (g, 24.0f, 570.0f, 1392.0f, "GAUGES" + dot + "TICK DIAL" + dot + "DOT ROW" + dot + "RULER WITH THE 20 S LIMIT" + dot + "TIMELINE");
                gauges::tickDial (g, { 24.0f, 600.0f, 90.0f, 60.0f }, 0.62, t.encoder (0).line);
                gauges::dotRow (g, { 140.0f, 620.0f, 160.0f, 16.0f }, 16, 12, t.encoder (1).line);
                gauges::ruler (g, { 330.0f, 600.0f, 260.0f, 50.0f }, 0.158, t.encoder (2).line, 1.0, "1 s", "20 s");
                gauges::timeline (g, { 620.0f, 620.0f, 200.0f, 16.0f }, 0.6, 11, t.encoder (3).line);
            }

        private:
            void timerCallback() override
            {
                phase += 0.05;
                levelL = (float) (-14.0 + 12.0 * std::sin (phase * 2.1) + 3.0 * std::sin (phase * 7.3));
                levelR = (float) (-16.0 + 13.0 * std::sin (phase * 1.7 + 0.6));
                arcL.setLevel (levelL, levelL > -1.0f);
                arcR.setLevel (levelR, levelR > -1.0f);
                repaint (640, 410, 60, 120);
            }

            PadGrid chopPads, loadPads, armedPads;
            LevelArc arcL { "L", "out" }, arcR { "R", "out" };
            std::unique_ptr<MotionClock> motion;
            double phase = 0.0;
            float levelL = -40.0f, levelR = -40.0f;
        };
    }

    std::unique_ptr<juce::Component> createEncoderGalleryPage() { return std::make_unique<EncoderPage>(); }
    std::unique_ptr<juce::Component> createMetersGalleryPage()  { return std::make_unique<MetersPage>(); }
}
