#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

// The chunked container shared by the stage's binary data files (.htex
// textures, .henv environments; tools/model/htex_io.py writes them):
//
//   char[4] magic; u32 version; u32 flags; u32 chunkCount;
//   u32 crc32 of every byte after the chunk table; u32 reserved[3]   (32 bytes)
//   chunkCount x { char[4] id; u32 offset (from file start); u32 size }
//   chunk payloads
//
// Little-endian. The parser never trusts a count or an offset: a bad magic,
// version or CRC, or a chunk that overruns the buffer, is an error. Unknown
// chunks are kept and ignored by the format readers.
namespace halo::chunkfile
{
    struct Chunk
    {
        std::array<char, 4> id {};
        std::span<const std::byte> data;
    };

    struct File
    {
        std::uint32_t version = 0, flags = 0;
        std::vector<Chunk> chunks;

        const Chunk* find (std::string_view id) const
        {
            for (const auto& c : chunks)
                if (id.size() == 4 && std::memcmp (c.id.data(), id.data(), 4) == 0)
                    return &c;
            return nullptr;
        }
    };

    inline constexpr std::size_t headerSize = 32, entrySize = 12;

    std::uint32_t crc32 (std::span<const std::byte>);

    // nullopt and `error` set on any malformation.
    std::optional<File> parse (std::span<const std::byte>, std::string_view magic, std::uint32_t maxVersion, std::string& error);

    // Little-endian reads from a span; the caller checks sizes first.
    inline std::uint32_t u32 (std::span<const std::byte> s, std::size_t at)
    {
        std::uint32_t v = 0;
        for (int i = 3; i >= 0; --i)
            v = (v << 8) | (std::uint32_t) s[at + (std::size_t) i];
        return v;
    }

    inline float f32 (std::span<const std::byte> s, std::size_t at)
    {
        const std::uint32_t bits = u32 (s, at);
        float f;
        std::memcpy (&f, &bits, 4);
        return f;
    }

    // Writer, for tests and tools.
    std::vector<std::byte> write (std::string_view magic, std::uint32_t version,
                                  const std::vector<std::pair<std::string, std::vector<std::byte>>>& chunks,
                                  std::uint32_t flags = 0);
}
