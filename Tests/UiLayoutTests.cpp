#include "Core/UiLayout.h"

#include <doctest/doctest.h>

using namespace halo::layout;

namespace
{
    bool near (float a, float b, float tol = 0.51f) { return std::abs (a - b) <= tol; }
}

TEST_CASE ("layout: the 1440 x 900 frame matches the plan's table")
{
    const auto f = frame (1440, 900);
    CHECK (f.density == Density::regular);
    CHECK_FALSE (f.narrow);
    CHECK (near (f.glass.x, 8));    CHECK (near (f.glass.right(), 1432));
    CHECK (near (f.glass.y, 8));    CHECK (near (f.glass.bottom(), 780));
    CHECK (near (f.strip.bottom(), 48));
    CHECK (near (f.body.x, 24));    CHECK (near (f.body.right(), 1416));
    CHECK (near (f.body.y, 56));    CHECK (near (f.body.bottom(), 640));
    CHECK (near (f.lane.x, 24));    CHECK (near (f.lane.right(), 520));
    CHECK (near (f.lane.y, 648));   CHECK (near (f.lane.bottom(), 734));
    CHECK (near (f.paramStrip.x, 532));
    CHECK (near (f.cells[0].w, 213.5f));
    CHECK (near (f.softKeys.y, 740)); CHECK (near (f.softKeys.bottom(), 772));
    CHECK (near (f.deck.y, 788));   CHECK (near (f.deck.bottom(), 900));
    const float centres[] { 638.75f, 862.25f, 1085.75f, 1309.25f };
    for (int i = 0; i < 4; ++i)
        CHECK (near (f.encoderX[(size_t) i], centres[i]));
    CHECK (near (f.modeKeys[0].x, 76));
    CHECK (near (f.modeKeys[6].right(), 420));
    CHECK (near (f.vu.x, 1352));
    CHECK (near (f.vu.right(), 1424));
}

TEST_CASE ("layout: compact 1180 x 720 and ultra-compact 1180 x 660")
{
    const auto c = frame (1180, 720);
    CHECK (c.density == Density::compact);
    CHECK (c.narrow);
    CHECK (near (c.glass.bottom(), 628));
    CHECK (near (c.strip.bottom(), 44));
    CHECK (near (c.body.y, 50));   CHECK (near (c.body.bottom(), 512));
    CHECK (near (c.lane.right(), 424));
    CHECK (near (c.lane.y, 518));  CHECK (near (c.lane.bottom(), 590));
    CHECK (near (c.paramStrip.x, 436));
    CHECK (near (c.cells[0].w, 172.5f));
    CHECK (near (c.softKeys.y, 594)); CHECK (near (c.softKeys.bottom(), 622));
    CHECK (near (c.deck.y, 636));
    CHECK (near (c.encoderX[0], 522.25f));
    CHECK (near (c.modeKeys[0].x, 24)); CHECK (near (c.modeKeys[6].right(), 320));
    CHECK (near (c.shiftKey.x, 330));   CHECK (near (c.recKey.x, 374));
    CHECK_FALSE (c.showGrille);
    CHECK_FALSE (c.showVu);

    const auto u = frame (1180, 660);
    CHECK (u.density == Density::ultra);
    CHECK (near (u.glass.bottom(), 580));
    CHECK (near (u.body.y, 50));   CHECK (near (u.body.bottom(), 472));
    CHECK (near (u.lane.y, 480));  CHECK (near (u.lane.bottom(), 544));
    CHECK (near (u.softKeys.y, 548)); CHECK (near (u.softKeys.bottom(), 574));
    CHECK (near (u.deck.y, 588));
    CHECK (near (u.keyCap, 34));
    CHECK (near (u.encoderDiameter, 40));
    CHECK_FALSE (u.encoderNames);
}

TEST_CASE ("layout: encoder, cell and pill centres agree at every size")
{
    for (float w = kMinWidth; w <= 2560.0f; w += 37.0f)
        for (float h = kMinHeight; h <= 1600.0f; h += 53.0f)
        {
            const auto f = frame (w, h);
            INFO ("size " << w << " x " << h);
            for (size_t i = 0; i < 4; ++i)
            {
                CHECK (near (f.encoderX[i], f.cells[i].cx()));
                CHECK (near (f.pills[i].cx(), f.cells[i].cx()));
                CHECK (near (f.encoders[i].cx(), f.cells[i].cx()));
                CHECK (f.glass.contains (f.cells[i]));
                CHECK (f.glass.contains (f.pills[i]));
                if (i > 0)
                    CHECK_FALSE (f.cells[i].overlaps (f.cells[i - 1]));
                CHECK (f.deck.contains (f.encoders[i]));
                for (const auto& k : f.modeKeys)
                    CHECK_FALSE (k.overlaps (f.encoders[i]));
                CHECK_FALSE (f.shiftKey.overlaps (f.encoders[i]));
                CHECK_FALSE (f.recKey.overlaps (f.encoders[i]));
                CHECK_FALSE (f.vu.overlaps (f.encoders[i]));
            }
            CHECK_FALSE (f.lane.overlaps (f.cells[0]));
            CHECK (f.body.h >= 400.0f);              // canvases still have room at the minimum
            CHECK (f.deck.y >= f.glass.bottom());
            CHECK (near (f.deck.bottom(), h));
            CHECK (f.lane.w >= 400.0f);
            CHECK (f.lane.w <= 496.0f);              // wider windows give the cells the room
        }
}

TEST_CASE ("layout: the status strip fits at 1180 with everything at its longest")
{
    const auto f = frame (1180, 660);
    StripContent c;
    c.mock = c.preview = true;
    c.deviceFull = "connected \xc2\xb7 os 2.5.0 \xc2\xb7 477 sounds";
    c.deviceShort = "in use by another app";
    c.feedWord = "preview";
    c.ringWord = "tx 100%";
    c.outName = "speakers (realtek h\xe2\x80\xa6 audio)";
    c.monText = "\xe2\x88\x92" "60 db";
    c.recText = "00:12:34";
    c.writesWord = "unlocked";
    const auto s = layoutStrip (f.strip, c);
    CHECK (s.fits);
    // The never-collapse slots are present and whole.
    for (const auto* slot : { &s.wordmark, &s.honest, &s.mon, &s.rec, &s.writes })
    {
        CHECK (slot->r.w > 0.0f);
        CHECK (f.strip.contains (slot->r));
    }
    CHECK (s.honest.r.w >= StripMetrics::honestSlotWidth() - 0.01f);
    CHECK (s.writes.form != SlotForm::hidden);
    // Nothing overlaps.
    const StripSlot* all[] { &s.wordmark, &s.honest, &s.device, &s.feed, &s.ring, &s.out, &s.mon, &s.rec, &s.writes };
    for (size_t i = 0; i < std::size (all); ++i)
        for (size_t j = i + 1; j < std::size (all); ++j)
        {
            INFO ("slots " << i << " and " << j);
            CHECK_FALSE (all[i]->r.overlaps (all[j]->r));
        }
}

TEST_CASE ("layout: the honest slot's width never moves the other slots")
{
    const auto f = frame (1440, 900);
    StripContent quiet, honest;
    quiet.deviceFull = honest.deviceFull = "connected \xc2\xb7 os 2.5.0 \xc2\xb7 477 sounds";
    quiet.deviceShort = honest.deviceShort = "connected";
    quiet.feedWord = honest.feedWord = "live";
    quiet.ringWord = honest.ringWord = "link";
    quiet.outName = honest.outName = "speakers (realtek)";
    quiet.monText = honest.monText = "off";
    quiet.recText = honest.recText = "\xe2\x80\x94:\xe2\x80\x94";
    quiet.writesWord = honest.writesWord = "locked";
    honest.mock = honest.preview = true;
    const auto a = layoutStrip (f.strip, quiet), b = layoutStrip (f.strip, honest);
    CHECK (a.device.r.x == b.device.r.x);
    CHECK (a.feed.r.x == b.feed.r.x);
    CHECK (a.honest.r.w == b.honest.r.w);
    CHECK (a.fits);
    CHECK (a.out.form == SlotForm::full); // a 1440 window shows every slot in full
}

TEST_CASE ("layout: measuring uses the generated metrics")
{
    // 'MOCK' in the tag face is wider than in the ui face, and tracking adds per glyph.
    CHECK (measure (halo::fontmetrics::kTag, "MOCK") > measure (halo::fontmetrics::kUi, "MOCK"));
    const float plain = measure (halo::fontmetrics::kUi, "abc");
    CHECK (plain > 10.0f);
    CHECK (measure (halo::fontmetrics::kUi, 25.0f, "abc") == doctest::Approx (plain * 2.0f));
}

TEST_CASE ("layout: the PLAY rail's sections fit at every size, with at least 3 event lines")
{
    for (float w : { 1180.0f, 1280.0f, 1440.0f, 1920.0f, 2560.0f })
        for (float h : { 660.0f, 699.0f, 720.0f, 819.0f, 900.0f, 1200.0f })
        {
            INFO ("size " << w << " x " << h);
            const auto f = frame (w, h);
            const auto p = playLayout (f, true);
            CHECK (p.eventLines >= 3);
            CHECK (p.events.bottom() <= f.body.bottom() + 0.01f);
            CHECK (f.body.contains (p.lastPad));
            CHECK_FALSE (p.stage.overlaps (p.rail));
            CHECK (p.stage.w >= 600.0f);
            const auto closed = playLayout (f, false);
            CHECK (closed.stage.w > p.stage.w);
            CHECK (closed.spine.w == doctest::Approx (PlayLayout::spineWidth));
        }
    // The plan's compact sum: 64 + 150 + 90 + 30 + 72 + 4 x 8 = 438 <= 462.
    const auto c = playLayout (frame (1180, 720), true);
    CHECK (c.rackOneLine);
    CHECK (c.lastPad.h + c.monitor.h + c.grab.h + c.rack.h + 4 * PlayLayout::gap + 72.0f <= 462.0f);
    const auto r = playLayout (frame (1440, 900), true);
    CHECK (r.rail.x == doctest::Approx (1010.0f));
    CHECK (r.stage.right() == doctest::Approx (1000.0f));
}
