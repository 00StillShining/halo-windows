#pragma once

#include <array>
#include <cstdint>
#include <optional>
#include <string_view>

// Port of EP40MIDIMapping.swift. Documented USB-MIDI facts plus clearly labelled
// inferences. Pad order within a group (". 0 ENTER 1-9") was confirmed on a real
// EP-40 on 2026-07-14.
namespace halo::ep40
{
    // Four sample groups A-D over notes 36...83: twelve contiguous notes each.
    inline constexpr std::uint8_t padNoteLow = 36;
    inline constexpr std::uint8_t padNoteHigh = 83;
    inline constexpr int groupCount = 4;
    inline constexpr int padsPerGroup = 12;

    // Per-group MIDI offset (0...11, in ". 0 ENTER 1...9" order) to the physical
    // 3x4 grid index, row-major from top-left: 7 8 9 / 4 5 6 / 1 2 3 / . 0 ENTER.
    inline constexpr std::array<int, padsPerGroup> midiOffsetToGrid { 9, 10, 11, 6, 7, 8, 3, 4, 5, 0, 1, 2 };

    // Grid index -> silkscreen label and model part name (DD-006 entity contract).
    inline constexpr std::array<std::string_view, padsPerGroup> gridLabels {
        "7", "8", "9", "4", "5", "6", "1", "2", "3", ".", "0", "ENTER"
    };
    inline constexpr std::array<std::string_view, padsPerGroup> gridPartNames {
        "pad_7", "pad_8", "pad_9", "pad_4", "pad_5", "pad_6",
        "pad_1", "pad_2", "pad_3", "pad_dot", "pad_0", "pad_enter"
    };
    inline constexpr std::array<std::string_view, groupCount> groupPartNames {
        "group_a", "group_b", "group_c", "group_d"
    };
    inline constexpr std::array<std::string_view, groupCount> groupLabels { "A", "B", "C", "D" };

    constexpr bool isPadNote (std::uint8_t note) noexcept
    {
        return note >= padNoteLow && note <= padNoteHigh;
    }

    // The sample group (0 = A ... 3 = D), or nullopt outside the pad range.
    constexpr std::optional<int> groupForNote (std::uint8_t note) noexcept
    {
        if (! isPadNote (note))
            return std::nullopt;
        return (note - padNoteLow) / padsPerGroup;
    }

    // The physical grid index (0...11), or nullopt outside the pad range.
    constexpr std::optional<int> gridIndexForNote (std::uint8_t note) noexcept
    {
        if (! isPadNote (note))
            return std::nullopt;
        return midiOffsetToGrid[(std::size_t) ((note - padNoteLow) % padsPerGroup)];
    }
}
