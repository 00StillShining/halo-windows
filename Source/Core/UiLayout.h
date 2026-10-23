#pragma once

#include "Core/UiFontMetrics.h"

#include <array>
#include <string>
#include <string_view>
#include <vector>

// The window's geometry (docs/redesign/plan.md section 3.1), computed in pure
// C++ from (width, height, physical scale) and the generated font metrics, so
// it can be tested at every size without JUCE. All values are logical px in
// window coordinates.
namespace halo::layout
{
    struct Rect
    {
        float x = 0, y = 0, w = 0, h = 0;
        float right() const { return x + w; }
        float bottom() const { return y + h; }
        float cx() const { return x + w * 0.5f; }
        float cy() const { return y + h * 0.5f; }
        bool empty() const { return w <= 0.0f || h <= 0.0f; }
        bool overlaps (const Rect& o) const
        {
            return ! empty() && ! o.empty() && x < o.right() && o.x < right() && y < o.bottom() && o.y < bottom();
        }
        bool contains (const Rect& o) const
        {
            return o.x >= x - 0.01f && o.y >= y - 0.01f && o.right() <= right() + 0.01f && o.bottom() <= bottom() + 0.01f;
        }
    };

    enum class Density { regular, compact, ultra }; // >= 820, >= 700, below (client height)

    inline constexpr float kMinWidth = 1180.0f, kMinHeight = 660.0f;

    struct Frame
    {
        float width = 0, height = 0, scale = 1;
        Density density = Density::regular;
        bool narrow = false; // below 1280 wide: grille, VU and nameplate drop; the lane is 400

        Rect glass, strip, body, lane, paramStrip, softKeys, deck;
        std::array<Rect, 4> cells {}, pills {}, encoders {};
        std::array<float, 4> encoderX {};
        float encoderCentreY = 0, encoderDiameter = 56, cellHeight = 86, pillW = 120, pillH = 24;

        std::array<Rect, 7> modeKeys {};
        Rect shiftKey, recKey, grille, vu, nameplate, modeCaption, encoderCaption;
        float keyCap = 44;
        bool keyLegends = true, encoderNames = true, showGrille = true, showVu = true;
    };

    Frame frame (float width, float height, float physicalScale = 1.0f);

    // ---- PLAY (plan section 3.5) ---------------------------------------------
    struct PlayLayout
    {
        Rect stage, rail, spine;
        bool railOpen = true;
        // Rail sections, top to bottom, with 8 px between them.
        Rect lastPad, monitor, grab, rack, events;
        bool rackOneLine = false; // compact: "rack engaged · echo spring · mix 35"
        int eventLines = 3;
        static constexpr float gap = 8.0f, spineWidth = 28.0f;
    };
    PlayLayout playLayout (const Frame&, bool railOpen);

    // ---- CHOP (plan section 3.7) ---------------------------------------------
    struct ChopLayout
    {
        Rect header, overview, ruler, canvas, bracket, status;
        Rect pads, output, send;   // regular: three zones; compact: output and send stack in one column
        Rect notice;               // inside `send`, sized for the given notice text
        bool stacked = false, showOverview = true;
        float padRow = 48.0f;
        static constexpr float minCanvas = 120.0f;
    };
    // noticeText is the longest notice the send zone may show right now.
    ChopLayout chopLayout (const Frame&, std::string_view noticeText, bool noticeBoxed);

    // A greedy word wrap with the generated metrics: the height a paragraph
    // needs at this width (lineHeight per line).
    float wrappedHeight (const fontmetrics::Role&, std::string_view utf8, float width, float lineHeight);
    // Height of a Notice (plan 2.5) for the text at this outer width.
    float noticeHeight (std::string_view utf8, float width, bool boxed);

    // ---- Text measurement from the generated metrics -------------------------
    float measure (const fontmetrics::Role&, std::string_view utf8);
    float measure (const fontmetrics::Role&, float em, std::string_view utf8);

    // ---- Status strip (plan section 3.2) --------------------------------------
    struct StripContent
    {
        bool mock = false, preview = false;
        std::string deviceFull;   // "connected · os 2.5.0 · 477 sounds"
        std::string deviceShort;  // "connected"
        std::string feedWord;     // "live" / "wait" / "preview"
        std::string ringWord;     // "link", "tx 42%", ...
        std::string outName;      // "speakers (realtek)" (already middle-ellipsised to 22 chars)
        std::string monText;      // "−12 db" / "off"
        std::string recText;      // "—:—" / "00:12"
        std::string writesWord;   // "locked" / "unlocked" / "n/a"
    };

    enum class SlotForm { hidden, minimal, shortForm, full };

    struct StripSlot
    {
        Rect r;
        SlotForm form = SlotForm::full;
    };

    struct StripLayout
    {
        StripSlot wordmark, honest, device, feed, ring, out, mon, rec, writes;
        bool fits = true;
        int collapseSteps = 0;
    };

    // Widths of the parts every slot is made of (shared with the painter).
    struct StripMetrics
    {
        static constexpr float lamp = 7.0f, gapSmall = 6.0f, pad = 16.0f;
        static float clusterGap (float stripWidth) { return stripWidth < 1300.0f ? 16.0f : 24.0f; }
        static float honestSlotWidth();     // reserved for MOCK + PREVIEW, always
        static float labelWidth (std::string_view upper);
        static float valueWidth (std::string_view text);
    };

    StripLayout layoutStrip (const Rect& strip, const StripContent&);
}
