#include "Core/Keymap.h"

#include <doctest/doctest.h>

#include <set>
#include <tuple>

using namespace halo;

TEST_CASE ("ui state: Ctrl+1...6 are exactly as before; CHOP is Ctrl+7")
{
    CHECK (keymap::modeForCtrlDigit (1) == AppMode::play);
    CHECK (keymap::modeForCtrlDigit (2) == AppMode::load);
    CHECK (keymap::modeForCtrlDigit (3) == AppMode::edit);
    CHECK (keymap::modeForCtrlDigit (4) == AppMode::capture);
    CHECK (keymap::modeForCtrlDigit (5) == AppMode::rack);
    CHECK (keymap::modeForCtrlDigit (6) == AppMode::backups);
    CHECK (keymap::modeForCtrlDigit (7) == AppMode::chop);
    CHECK_FALSE (keymap::modeForCtrlDigit (8).has_value());
    CHECK_FALSE (keymap::modeForCtrlDigit (0).has_value());
    for (auto m : keymap::deckOrder())
        CHECK (keymap::modeForCtrlDigit (keymap::shortcutDigit (m)) == m);
}

TEST_CASE ("ui state: the deck shows chop between edit and capture")
{
    const auto& order = keymap::deckOrder();
    REQUIRE (order.size() == 7);
    CHECK (order[2] == AppMode::edit);
    CHECK (order[3] == AppMode::chop);
    CHECK (order[4] == AppMode::capture);
}

TEST_CASE ("ui state: CHOP shows only in CHOP mode; the stage only in PLAY")
{
    // Switching CHOP -> RACK shows RACK (the CHOP surface used to linger).
    CHECK (keymap::showsChop (AppMode::chop));
    for (auto m : { AppMode::play, AppMode::load, AppMode::edit, AppMode::capture, AppMode::rack, AppMode::backups })
    {
        CHECK_FALSE (keymap::showsChop (m));
        CHECK (keymap::showsStage (m) == (m == AppMode::play));
    }
    CHECK_FALSE (keymap::showsStage (AppMode::chop));
}

TEST_CASE ("ui state: no input means two things in one context")
{
    std::set<std::tuple<std::string, std::string, std::string>> seen;
    for (const auto& b : keymap::table())
    {
        INFO (b.context << " / " << b.modifiers << " / " << b.input);
        CHECK (seen.insert ({ b.context, b.modifiers, b.input }).second);
    }
    // One grammar (U19): alt is always fine, ctrl coarse, except ctrl+wheel on the chop canvas (zoom).
    for (const auto& b : keymap::table())
    {
        if (b.modifiers == "alt" && b.context.rfind ("encoder", 0) == 0)
            CHECK (b.action.find ("fine") != std::string::npos);
        if (b.modifiers == "ctrl" && b.input == "wheel")
            CHECK ((b.action.find ("coarse") != std::string::npos || b.context == "chop canvas"));
    }
}
