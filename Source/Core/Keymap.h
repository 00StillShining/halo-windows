#pragma once

#include <optional>
#include <string>
#include <vector>

namespace halo
{
    // The app's modes. CHOP is a mode of its own (plan U11): Ctrl+7, after the
    // six that were there before, so nothing is renumbered.
    enum class AppMode { play, load, edit, capture, rack, backups, chop };

    namespace keymap
    {
        // Ctrl+1...7 -> mode. 1-6 are exactly as before the redesign.
        inline std::optional<AppMode> modeForCtrlDigit (int digit)
        {
            switch (digit)
            {
                case 1: return AppMode::play;
                case 2: return AppMode::load;
                case 3: return AppMode::edit;
                case 4: return AppMode::capture;
                case 5: return AppMode::rack;
                case 6: return AppMode::backups;
                case 7: return AppMode::chop;
                default: return std::nullopt;
            }
        }

        // The deck's key order (plan section 3.3): play load edit chop capture rack backups.
        inline const std::vector<AppMode>& deckOrder()
        {
            static const std::vector<AppMode> order { AppMode::play, AppMode::load, AppMode::edit, AppMode::chop,
                                                      AppMode::capture, AppMode::rack, AppMode::backups };
            return order;
        }

        inline int shortcutDigit (AppMode m)
        {
            for (int d = 1; d <= 7; ++d)
                if (modeForCtrlDigit (d) == m)
                    return d;
            return 0;
        }

        inline const char* modeWord (AppMode m)
        {
            switch (m)
            {
                case AppMode::play:    return "play";
                case AppMode::load:    return "load";
                case AppMode::edit:    return "edit";
                case AppMode::capture: return "capture";
                case AppMode::rack:    return "rack";
                case AppMode::backups: return "backups";
                case AppMode::chop:    return "chop";
            }
            return "";
        }

        // Which page fills the body. CHOP shows only in CHOP mode (it used to
        // linger over other modes); the 3D stage only in PLAY (plan U12).
        inline bool showsChop (AppMode m)  { return m == AppMode::chop; }
        inline bool showsStage (AppMode m) { return m == AppMode::play; }

        // The input map (plan section 3.15) as data, so it can be checked for
        // ambiguity: no (context, modifiers, input) may mean two things.
        struct Binding
        {
            std::string context;   // "global", "chop canvas", "card", "lists", "stage", "encoder"
            std::string modifiers; // "ctrl", "alt", "shift", "" ...
            std::string input;     // "1", "m", "wheel", "esc" ...
            std::string action;
        };

        inline const std::vector<Binding>& table()
        {
            static const std::vector<Binding> t {
                { "global", "ctrl", "1", "mode play" },
                { "global", "ctrl", "2", "mode load" },
                { "global", "ctrl", "3", "mode edit" },
                { "global", "ctrl", "4", "mode capture" },
                { "global", "ctrl", "5", "mode rack" },
                { "global", "ctrl", "6", "mode backups" },
                { "global", "ctrl", "7", "mode chop" },
                { "global", "ctrl", "m", "monitor on/off" },
                { "global", "ctrl", "r", "record on/off" },
                { "global", "ctrl", "g", "grab" },
                { "global", "ctrl", "e", "export" },
                { "global", "ctrl", "enter", "send (through the write gate)" },
                { "global", "ctrl", "f", "focus capture search" },
                { "global", "", "f1", "focus encoder 1" },
                { "global", "", "f2", "focus encoder 2" },
                { "global", "", "f3", "focus encoder 3" },
                { "global", "", "f4", "focus encoder 4" },
                { "global", "", "esc", "close card > leave chop > cancel assign > unfocus encoder > collapse rail" },
                { "global", "", "space", "audition / stop" },
                { "encoder", "", "wheel", "turn" },
                { "encoder", "alt", "wheel", "turn fine" },
                { "encoder", "ctrl", "wheel", "turn coarse" },
                { "encoder", "shift", "wheel", "turn the shift binding" },
                { "encoder", "", "drag", "turn (180 px = range)" },
                { "encoder", "alt", "drag", "turn fine" },
                { "encoder", "", "click", "press" },
                { "encoder", "shift", "click", "shift press" },
                { "encoder", "", "double-click", "reset" },
                { "encoder focused", "", "up/down", "turn" },
                { "encoder focused", "alt", "up/down", "turn fine" },
                { "encoder focused", "ctrl", "up/down", "turn coarse" },
                { "encoder focused", "shift", "up/down", "turn the shift binding" },
                { "encoder focused", "", "space/enter", "press" },
                { "chop canvas", "", "wheel", "scroll (when zoomed)" },
                { "chop canvas", "ctrl", "wheel", "zoom" },
                { "chop marker", "", "wheel", "nudge the cut 10 ms" },
                { "chop marker", "alt", "wheel", "nudge the cut 1 ms" },
                { "chop canvas", "", "click", "audition" },
                { "chop canvas", "", "double-click", "add a cut" },
                { "chop marker", "", "drag", "move the cut" },
                { "chop marker", "", "right-click", "remove the cut" },
                { "chop", "", "1-9 0", "audition slices 1-10" },
                { "chop", "", "delete/backspace", "remove the selected cut" },
                { "chop", "", "tab", "next marker" },
                { "card", "", "esc", "cancel" },
                { "card", "", "enter", "cancel (fresh card) / confirm (after tab)" },
                { "card", "", "tab", "move focus to confirm" },
                { "lists", "", "up/down", "move" },
                { "lists", "", "enter", "encoder 2 press" },
                { "stage", "", "drag", "orbit" },
                { "stage", "", "wheel", "zoom" },
                { "stage", "", "double-click", "reset view" },
                { "deck", "", "shift (held)", "shift layer" },
                { "deck", "", "shift click", "latch the shift layer for one press" },
            };
            return t;
        }
    }
}
