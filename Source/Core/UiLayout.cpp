#include "Core/UiLayout.h"

#include <algorithm>
#include <cmath>

namespace halo::layout
{
    namespace
    {
        struct Dims
        {
            float strip, gapStrip, cell, gapBodyCells, gapCellsKeys, keys, gapKeysGlass, deck, pillH, encoder, cap;
        };

        constexpr Dims regularDims { 40, 8, 86, 8, 6, 32, 8, 112, 24, 56, 44 };
        constexpr Dims compactDims { 36, 6, 72, 6, 4, 28, 6, 84, 22, 46, 38 };
        constexpr Dims ultraDims   { 36, 6, 64, 8, 4, 26, 6, 72, 22, 40, 34 };

        // Decodes UTF-8 one code point at a time.
        template <typename Fn>
        void forEachCodepoint (std::string_view s, Fn fn)
        {
            for (size_t i = 0; i < s.size();)
            {
                const auto c = (unsigned char) s[i];
                char32_t cp = c;
                int extra = 0;
                if (c >= 0xF0)      { cp = c & 0x07; extra = 3; }
                else if (c >= 0xE0) { cp = c & 0x0F; extra = 2; }
                else if (c >= 0xC0) { cp = c & 0x1F; extra = 1; }
                ++i;
                for (int k = 0; k < extra && i < s.size(); ++k, ++i)
                    cp = (cp << 6) | ((unsigned char) s[i] & 0x3F);
                fn (cp);
            }
        }
    }

    float measure (const fontmetrics::Role& role, float em, std::string_view utf8)
    {
        const float scale = em / role.em;
        float w = 0.0f;
        int n = 0;
        forEachCodepoint (utf8, [&] (char32_t cp) { w += fontmetrics::advanceOf (role, cp) * scale; ++n; });
        return w + role.tracking * em * (float) n;
    }

    float measure (const fontmetrics::Role& role, std::string_view utf8)
    {
        return measure (role, role.em, utf8);
    }

    Frame frame (float width, float height, float scale)
    {
        Frame f;
        f.width = width;
        f.height = height;
        f.scale = scale;
        f.density = height >= 820.0f ? Density::regular : height >= 700.0f ? Density::compact : Density::ultra;
        f.narrow = width < 1280.0f;
        const auto& d = f.density == Density::regular ? regularDims : f.density == Density::compact ? compactDims : ultraDims;

        f.glass = { 8.0f, 8.0f, width - 16.0f, height - d.deck - 16.0f };
        f.strip = { f.glass.x, f.glass.y, f.glass.w, d.strip };
        const float innerLeft = f.glass.x + 16.0f, innerRight = f.glass.right() - 16.0f, innerW = innerRight - innerLeft;
        f.softKeys = { innerLeft, f.glass.bottom() - d.gapKeysGlass - d.keys, innerW, d.keys };
        f.cellHeight = d.cell;
        const float cellsY = f.softKeys.y - d.gapCellsKeys - d.cell;
        const float bodyTop = f.strip.bottom() + d.gapStrip;
        f.body = { innerLeft, bodyTop, innerW, cellsY - d.gapBodyCells - bodyTop };

        const float laneW = f.narrow ? 400.0f : 496.0f;
        f.lane = { innerLeft, cellsY, laneW, d.cell };
        const float stripLeft = innerLeft + laneW + 12.0f;
        f.paramStrip = { stripLeft, cellsY, innerRight - stripLeft, d.cell };
        const float cellW = (f.paramStrip.w - 3.0f * 10.0f) / 4.0f;
        f.pillW = 120.0f;
        f.pillH = d.pillH;
        for (size_t i = 0; i < 4; ++i)
        {
            f.cells[i] = { stripLeft + (float) i * (cellW + 10.0f), cellsY, cellW, d.cell };
            f.encoderX[i] = f.cells[i].cx();
            f.pills[i] = { f.encoderX[i] - f.pillW * 0.5f, f.softKeys.cy() - f.pillH * 0.5f, f.pillW, f.pillH };
        }

        // The deck under the glass.
        f.deck = { 0.0f, f.glass.bottom() + 8.0f, width, height - f.glass.bottom() - 8.0f };
        f.encoderDiameter = d.encoder;
        f.encoderNames = f.density != Density::ultra;
        f.keyLegends = f.density == Density::regular;
        f.keyCap = d.cap;
        f.encoderCentreY = f.density == Density::regular ? f.deck.y + 38.0f
                         : f.density == Density::compact ? f.deck.y + 30.0f
                                                         : f.deck.cy();
        const float hit = std::max (72.0f, d.encoder + 16.0f);
        for (size_t i = 0; i < 4; ++i)
            f.encoders[i] = { f.encoderX[i] - hit * 0.5f, f.encoderCentreY - d.encoder * 0.5f - 4.0f, hit,
                              d.encoder + 8.0f + (f.encoderNames ? 22.0f : 0.0f) };

        f.showGrille = ! f.narrow && f.density == Density::regular;
        f.showVu = ! f.narrow;
        const float keyTop = f.density == Density::regular ? f.deck.y + 16.0f : f.deck.y + (f.deck.h - d.cap) * 0.5f - 2.0f;
        const float pitch = f.density == Density::regular ? 50.0f : 43.0f;
        const float x0 = f.showGrille ? 76.0f : innerLeft;
        const float keyH = d.cap + (f.keyLegends ? 20.0f : 0.0f);
        for (size_t i = 0; i < 7; ++i)
            f.modeKeys[i] = { x0 + (float) i * pitch, keyTop, d.cap, keyH };
        const float side = f.density == Density::regular ? 40.0f : 36.0f;
        const float sideTop = keyTop + (d.cap - side) * 0.5f;
        const float afterKeys = f.modeKeys[6].right() + (f.density == Density::regular ? 12.0f : 10.0f);
        f.shiftKey = { afterKeys, sideTop, side, side + (f.keyLegends ? 20.0f + (d.cap - side) * 0.5f : 0.0f) };
        f.recKey = { f.shiftKey.right() + 8.0f, sideTop, side, f.shiftKey.h };
        f.grille = f.showGrille ? Rect { innerLeft, keyTop, 40.0f, d.cap } : Rect {};
        // The VU window sits right of encoder 4's hit area, at the glass's right
        // edge when there's room (x 1352 at 1440); it hides when there isn't.
        const float vuX = std::max (f.glass.right() - 8.0f - 72.0f, f.encoders[3].right() + 6.0f);
        f.showVu = f.showVu && vuX + 72.0f <= f.glass.right();
        f.vu = f.showVu ? Rect { vuX, f.deck.y + 14.0f, 72.0f, std::max (24.0f, f.deck.h - 46.0f) } : Rect {};
        f.nameplate = f.showVu ? Rect { f.vu.x, f.vu.bottom() + 6.0f, f.vu.w, 14.0f } : Rect {};
        if (f.density == Density::regular)
        {
            f.modeCaption = { x0, f.deck.y + 2.0f, 200.0f, 12.0f };
            f.encoderCaption = { f.cells[0].x, f.deck.y + 2.0f, f.cells[3].right() - f.cells[0].x, 12.0f };
        }
        return f;
    }

    // ---- Status strip -----------------------------------------------------------
    namespace
    {
        float tagWidth (std::string_view text, float extra)
        {
            return std::ceil (measure (fontmetrics::kTag, text)) + 16.0f + extra;
        }
    }

    float StripMetrics::honestSlotWidth()
    {
        return tagWidth ("MOCK", 12.0f) + 8.0f + tagWidth ("PREVIEW", 13.0f);
    }

    float StripMetrics::labelWidth (std::string_view upper) { return std::ceil (measure (fontmetrics::kLabel, upper)); }
    float StripMetrics::valueWidth (std::string_view text)  { return std::ceil (measure (fontmetrics::kUiStatus, text)); }

    StripLayout layoutStrip (const Rect& strip, const StripContent& c)
    {
        using M = StripMetrics;
        const float gap = M::clusterGap (strip.w);
        const float lamp = M::lamp, g = M::gapSmall;
        const char* enDashEp = "EP\xe2\x80\x93" "40";

        auto deviceW = [&] (SlotForm f)
        {
            const float base = lamp + g + M::labelWidth (enDashEp);
            if (f == SlotForm::full)      return base + g + M::valueWidth (c.deviceFull);
            if (f == SlotForm::shortForm) return base + g + M::valueWidth (c.deviceShort);
            return base;
        };
        auto feedW = [&] (SlotForm f)
        {
            if (f == SlotForm::full)      return M::labelWidth ("FEED") + g + lamp + g + M::valueWidth (c.feedWord);
            if (f == SlotForm::shortForm) return lamp + g + M::valueWidth (c.feedWord);
            return lamp;
        };
        auto ringW = [&] (SlotForm f) { return f == SlotForm::hidden ? 0.0f : M::labelWidth ("RING") + g + M::valueWidth (c.ringWord); };
        auto outW = [&] (SlotForm f) { return f == SlotForm::hidden ? 0.0f : M::labelWidth ("OUT") + g + M::valueWidth (c.outName); };
        const float monW = M::labelWidth ("MON") + g + M::valueWidth (c.monText) + 10.0f; // room for the owner dot
        const float recW = lamp + g + M::labelWidth ("REC") + g + M::valueWidth (c.recText);
        auto writesW = [&] (SlotForm f)
        {
            return 14.0f + g + (f == SlotForm::full ? M::labelWidth ("WRITES") + g : 0.0f) + M::valueWidth (c.writesWord);
        };
        const float wordmarkW = 18.0f + 8.0f + std::ceil (measure (fontmetrics::kTitle, "halo"));

        SlotForm device = SlotForm::full, feed = SlotForm::full, ring = SlotForm::full, out = SlotForm::full, writes = SlotForm::full;
        StripLayout L;
        // Collapse in a fixed order until both clusters fit.
        for (int step = 0; step <= 8; ++step)
        {
            switch (step)
            {
                case 0: break;
                case 1: out = SlotForm::hidden; break;          // first to hide (also in the PLAY rail)
                case 2: device = SlotForm::shortForm; break;
                case 3: writes = SlotForm::shortForm; break;     // the word "writes" drops
                case 4: feed = SlotForm::shortForm; break;       // the label drops
                case 5: ring = SlotForm::hidden; break;          // the glyph in slot 1 stays
                case 6: device = SlotForm::minimal; break;
                case 7: feed = SlotForm::minimal; break;         // the lamp stays
                default: break;
            }
            float x = strip.x + M::pad;
            L.wordmark = { { x, strip.y, wordmarkW, strip.h }, SlotForm::full };
            x += wordmarkW + 20.0f;
            L.honest = { { x, strip.y, M::honestSlotWidth(), strip.h }, SlotForm::full };
            x += M::honestSlotWidth() + gap;
            L.device = { { x, strip.y, deviceW (device), strip.h }, device };
            x += deviceW (device) + gap;
            L.feed = { { x, strip.y, feedW (feed), strip.h }, feed };
            x += feedW (feed) + gap;
            L.ring = { { x, strip.y, ringW (ring), strip.h }, ring };
            const float leftEnd = ring == SlotForm::hidden ? x - gap : x + ringW (ring);

            float r = strip.right() - M::pad;
            L.writes = { { r - writesW (writes), strip.y, writesW (writes), strip.h }, writes };
            r -= writesW (writes) + gap;
            L.rec = { { r - recW, strip.y, recW, strip.h }, SlotForm::full };
            r -= recW + gap;
            L.mon = { { r - monW, strip.y, monW, strip.h }, SlotForm::full };
            r -= monW + gap;
            L.out = { { r - outW (out), strip.y, outW (out), strip.h }, out };
            const float rightStart = out == SlotForm::hidden ? r + gap : r - outW (out);

            L.collapseSteps = step;
            L.fits = leftEnd + gap <= rightStart;
            if (L.fits)
                break;
        }
        if (ring == SlotForm::hidden)
            L.ring.r.w = 0.0f;
        if (out == SlotForm::hidden)
            L.out.r.w = 0.0f;
        return L;
    }
}
