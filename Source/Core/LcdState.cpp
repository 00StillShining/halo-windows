#include "Core/LcdState.h"

namespace halo
{
    namespace
    {
        // Bits in Source/Stage/LcdSegments.h's order.
        enum : std::uint16_t
        {
            T = 1 << 0, B = 1 << 1, L = 1 << 2, R = 1 << 3, UC = 1 << 4, LC = 1 << 5,
            UL = 1 << 6, UR = 1 << 7, LL = 1 << 8, LR = 1 << 9,
        };
    }

    std::uint16_t lcdGlyph (char c)
    {
        // TE's digit has no middle bars: its corner pieces carry the curves
        // (a "2" is top, upper-right, lower-left, bottom, as on the device).
        switch (c)
        {
            case '0': return T | B | L | R;
            case '1': return R;
            case '2': return T | UR | LL | B;
            case '3': return T | UR | LR | B;
            case '4': return UL | R;
            case '5': return T | UL | LR | B;
            case '6': return T | L | LL | LR | B;
            case '7': return T | R;
            case '8': return T | B | UL | UR | LL | LR;
            case '9': return T | UL | UR | R;
            case 'S': return T | UL | LR | B;
            case 'I': return UC | LC;
            case 'M': return L | R | UL | UR;
            case '_': return B;
            default:  return 0;
        }
    }

    LcdState toLcdState (const DisplayState& in)
    {
        DisplayState s = in;
        s.clamp();
        LcdState out;
        out.feed = s.feedMode;

        switch (s.feedMode)
        {
            case FeedMode::waiting:
                // Connected but nothing observed yet: nothing may light.
                out.backlight = LcdState::waitingBacklight;
                out.text = { '_', '_', '_' };
                return out;

            case FeedMode::preview:
                out.backlight = LcdState::previewBacklight;
                out.text = { 'S', 'I', 'M' };
                break;

            case FeedMode::live:
            {
                out.backlight = LcdState::liveBacklight;
                const int v = s.value;
                out.text = { v >= 100 ? (char) ('0' + v / 100) : ' ',
                             v >= 10 ? (char) ('0' + (v / 10) % 10) : ' ',
                             (char) ('0' + v % 10) };
                break;
            }
        }

        // Each icon below is lit by exactly one DisplayState field.
        out.lit[(size_t) s.mode] = true;                                 // mode -> SOUND ... SYSTEM
        out.lit[(size_t) LcdIcon::play] = s.isPlaying;                  // isPlaying -> play triangle
        out.lit[(size_t) LcdIcon::bpm] = s.clockPulse;                  // clockPulse -> BPM
        if (s.activeGroup)
            out.lit[(size_t) LcdIcon::groupA + (size_t) *s.activeGroup] = true; // activeGroup -> its icon
        out.lit[(size_t) LcdIcon::numpad] = s.activePadIndex.has_value(); // activePadIndex -> numpad
        out.meters = { s.leftMeter, s.rightMeter, s.leftMeter * 0.78f, s.rightMeter * 0.78f };
        return out;
    }
}
