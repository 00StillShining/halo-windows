#pragma once

#include "Core/ChunkFile.h"

#include <array>
#include <optional>
#include <span>
#include <string>
#include <vector>

// .htex v1: the stage's data textures (SDF atlases, grain, deck AO), stored raw
// with explicit channel semantics so they are uploaded with no premultiply and
// no BGRA swizzle (plan D9; JUCE's PNG loader does both). Written by
// tools/model/htex_io.py.
namespace halo
{
    struct Htex
    {
        enum class Format : std::uint32_t { r8 = 1, rg8 = 2, rgba8 = 3, rg16f = 4, rgba16f = 5 };
        enum Flags : std::uint32_t { srgb = 1, sdf = 2 };

        std::uint32_t width = 0, height = 0, flags = 0;
        Format format = Format::rgba8;
        std::array<char, 4> channels {};             // what each channel holds, e.g. "RGBA"
        std::vector<std::span<const std::byte>> mips; // views into the parsed buffer, level 0 first

        static int channelCount (Format);
        static int bytesPerTexel (Format);

        std::uint32_t mipWidth (std::size_t level) const { return std::max (1u, width >> level); }
        std::uint32_t mipHeight (std::size_t level) const { return std::max (1u, height >> level); }

        // Texel (x, y) of a level, 8-bit formats only, in stored channel order.
        std::array<std::uint8_t, 4> texel8 (std::size_t level, std::uint32_t x, std::uint32_t y) const;

        // The buffer must outlive the result (mips view into it).
        static std::optional<Htex> parse (std::span<const std::byte>, std::string& error);
    };
}
