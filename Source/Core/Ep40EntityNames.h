#pragma once

#include <array>
#include <string_view>

// FROZEN entity-name contract (DD-006), ported verbatim from the Mac app's
// Halo/Reality/EP40EntityNames.swift. The 3D stage binds pads, LEDs and the
// display by these names, so a model that renames one breaks the stage.
// Extra parts may be added or renamed freely (DD-006e); these may not.
//
// Order follows the Swift enum (CaseIterable order). Tests/Ep40EntityNamesTests
// checks the list against the Swift file whenever that file is available.
namespace halo::ep40
{
    inline constexpr std::string_view rootEntityName = "ep40_root";

    inline constexpr std::array<std::string_view, 43> entityNames {
        // Chassis & surfaces
        "chassis_base", "chassis_topPlate", "chassis_edgeBand", "display_surface",
        "speaker_grille", "halo_ring", "mic_port",
        // Group pads
        "group_a", "group_b", "group_c", "group_d",
        // Numeric pads
        "pad_dot", "pad_0", "pad_1", "pad_2", "pad_3", "pad_4", "pad_5",
        "pad_6", "pad_7", "pad_8", "pad_9", "pad_enter",
        // Rotaries & fader
        "knob_volume", "knob_x", "knob_y", "fader_track", "fader_cap",
        // Buttons
        "button_sound", "button_main", "button_tempo", "button_sample", "button_keys",
        "button_timing", "button_fx", "button_erase", "button_shift", "button_minus",
        "button_plus", "button_record", "button_play",
        // Ports
        "ports_strip", "ports_usb",
    };

    constexpr bool isContractName (std::string_view name) noexcept
    {
        if (name == rootEntityName)
            return true;
        for (auto n : entityNames)
            if (n == name)
                return true;
        return false;
    }
}
