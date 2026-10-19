#pragma once

#include "Core/DisplayState.h"
#include "Core/Ep40MidiMapping.h"
#include "Core/RingState.h"

#include <algorithm>
#include <array>
#include <cctype>
#include <charconv>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

// HALO_STAGE_POSE: a frozen, repeatable stage state for screenshots and visual
// regression (docs/redesign/riddim-3d-plan.md, section 8 "Dev hooks").
//
//   "pad7=1.0,pad5=0.8,group=1,lcd=demo,ring=steady,feed=live"
//
// A pose overrides live and mock pad, display and ring input, and the stage
// freezes its springs and clock while one is set. Every field is optional:
//   pad<label>[=velocity]  label is 7 8 9 4 5 6 1 2 3 dot 0 enter; velocity 0-1 (default 1)
//   group=<0-3|a-d>        active sample group
//   lcd=demo|idle|off      demo shows 120 BPM, playing, with the active group
//   ring=off|discover|steady|monitor|record|transfer|error
//   feed=live|waiting|preview
//   rest                   no pads (the default); accepted for readability
// Unknown or malformed tokens are reported in `errors` and otherwise ignored.
namespace halo
{
    struct StagePose
    {
        enum class Lcd { demo, idle, off };

        std::array<float, ep40::padsPerGroup> pads {}; // grid order, 0 = up
        std::optional<int> group;
        Lcd lcd = Lcd::demo;
        FeedMode feed = FeedMode::live;
        RingState ring { RingState::Kind::connected, 0.0, {} };
        std::vector<std::string> errors;

        bool anyPadDown() const noexcept
        {
            for (auto v : pads)
                if (v > 0.0f)
                    return true;
            return false;
        }

        // The display the stage shows while this pose is active.
        DisplayState display() const
        {
            DisplayState s;
            s.feedMode = feed;
            if (lcd == Lcd::off)
            {
                s.mode = DisplayMode::system;
                return s;
            }
            if (lcd == Lcd::idle)
            {
                s.mode = DisplayMode::main;
                s.activeGroup = group;
                return s;
            }
            s.mode = DisplayMode::main;
            s.value = 120;
            s.activeGroup = group.value_or (1);
            for (int i = 0; i < ep40::padsPerGroup; ++i)
                if (pads[(size_t) i] > 0.0f)
                {
                    s.activePadIndex = i;
                    s.velocity = pads[(size_t) i];
                    break;
                }
            s.leftMeter = 0.62f;
            s.rightMeter = 0.48f;
            s.isPlaying = true;
            s.clockPulse = true;
            return s;
        }

        // nullopt for an empty string (no pose).
        static std::optional<StagePose> parse (std::string_view text)
        {
            if (trim (text).empty())
                return std::nullopt;

            StagePose pose;
            size_t start = 0;
            while (start <= text.size())
            {
                const auto end = std::min (text.find (',', start), text.size());
                const auto token = trim (text.substr (start, end - start));
                start = end + 1;
                if (token.empty())
                    continue;

                const auto eq = token.find ('=');
                const auto key = lower (trim (token.substr (0, eq)));
                const auto value = eq == std::string_view::npos ? std::string() : lower (trim (token.substr (eq + 1)));
                if (! pose.apply (key, value, eq != std::string_view::npos))
                    pose.errors.emplace_back (token);
            }
            return pose;
        }

    private:
        bool apply (const std::string& key, const std::string& value, bool hasValue)
        {
            if (key == "rest")
                return ! hasValue;

            if (key.rfind ("pad", 0) == 0)
            {
                auto label = key.substr (3);
                if (! label.empty() && label[0] == '_')
                    label.erase (0, 1);
                if (label == "dot") label = ".";
                for (size_t i = 0; i < ep40::gridLabels.size(); ++i)
                    if (lower (ep40::gridLabels[i]) == label)
                    {
                        float v = 1.0f;
                        if (hasValue && ! toFloat (value, v))
                            return false;
                        pads[i] = std::clamp (v, 0.0f, 1.0f);
                        return true;
                    }
                return false;
            }
            if (key == "group")
            {
                if (value.size() == 1 && value[0] >= 'a' && value[0] <= 'd') { group = value[0] - 'a'; return true; }
                if (value.size() == 1 && value[0] >= '0' && value[0] <= '3') { group = value[0] - '0'; return true; }
                if (value == "none") { group.reset(); return true; }
                return false;
            }
            if (key == "lcd")
            {
                if (value == "demo") { lcd = Lcd::demo; return true; }
                if (value == "idle") { lcd = Lcd::idle; return true; }
                if (value == "off")  { lcd = Lcd::off; return true; }
                return false;
            }
            if (key == "feed")
            {
                if (value == "live")    { feed = FeedMode::live; return true; }
                if (value == "waiting") { feed = FeedMode::waiting; return true; }
                if (value == "preview") { feed = FeedMode::preview; return true; }
                return false;
            }
            if (key == "ring")
            {
                using K = RingState::Kind;
                struct Entry { const char* name; K kind; };
                static constexpr Entry entries[] {
                    { "off", K::disconnected }, { "discover", K::discovering }, { "steady", K::connected },
                    { "monitor", K::monitoring }, { "record", K::recording }, { "transfer", K::transfer },
                    { "error", K::error } };
                for (const auto& e : entries)
                    if (value == e.name)
                    {
                        ring = {};
                        ring.kind = e.kind;
                        if (e.kind == K::transfer) ring.transferProgress = 0.6;
                        if (e.kind == K::error) ring.errorLabel = "POSE";
                        return true;
                    }
                return false;
            }
            return false;
        }

        static std::string_view trim (std::string_view s)
        {
            while (! s.empty() && std::isspace ((unsigned char) s.front())) s.remove_prefix (1);
            while (! s.empty() && std::isspace ((unsigned char) s.back())) s.remove_suffix (1);
            return s;
        }

        static std::string lower (std::string_view s)
        {
            std::string out (s);
            for (auto& c : out)
                c = (char) std::tolower ((unsigned char) c);
            return out;
        }

        static bool toFloat (const std::string& s, float& out)
        {
            if (s.empty())
                return false;
            const auto* first = s.data();
            const auto* last = s.data() + s.size();
            auto [ptr, ec] = std::from_chars (first, last, out);
            return ec == std::errc() && ptr == last;
        }
    };
}
