#include "Core/ChunkFile.h"

namespace halo::chunkfile
{
    std::uint32_t crc32 (std::span<const std::byte> data)
    {
        static const auto table = []
        {
            std::array<std::uint32_t, 256> t {};
            for (std::uint32_t i = 0; i < 256; ++i)
            {
                std::uint32_t c = i;
                for (int k = 0; k < 8; ++k)
                    c = (c & 1u) ? 0xEDB88320u ^ (c >> 1) : c >> 1;
                t[i] = c;
            }
            return t;
        }();
        std::uint32_t crc = 0xFFFFFFFFu;
        for (auto b : data)
            crc = table[(crc ^ (std::uint32_t) b) & 0xFFu] ^ (crc >> 8);
        return crc ^ 0xFFFFFFFFu;
    }

    std::optional<File> parse (std::span<const std::byte> data, std::string_view magic, std::uint32_t maxVersion,
                               std::string& error)
    {
        if (data.size() < headerSize)
            return error = "shorter than its header", std::nullopt;
        if (magic.size() != 4 || std::memcmp (data.data(), magic.data(), 4) != 0)
            return error = "bad magic", std::nullopt;

        File file;
        file.version = u32 (data, 4);
        file.flags = u32 (data, 8);
        const auto count = u32 (data, 12);
        const auto crc = u32 (data, 16);
        if (file.version == 0 || file.version > maxVersion)
            return error = "unsupported version " + std::to_string (file.version), std::nullopt;
        if (count > 4096 || headerSize + (std::size_t) count * entrySize > data.size())
            return error = "chunk table overruns the file", std::nullopt;

        const std::size_t tableEnd = headerSize + (std::size_t) count * entrySize;
        if (crc32 (data.subspan (tableEnd)) != crc)
            return error = "CRC mismatch", std::nullopt;

        for (std::uint32_t i = 0; i < count; ++i)
        {
            const std::size_t at = headerSize + (std::size_t) i * entrySize;
            Chunk c;
            std::memcpy (c.id.data(), data.data() + at, 4);
            const std::uint64_t offset = u32 (data, at + 4), size = u32 (data, at + 8);
            if (offset < tableEnd || offset + size > data.size())
                return error = "chunk " + std::string (c.id.data(), 4) + " overruns the file", std::nullopt;
            c.data = data.subspan ((std::size_t) offset, (std::size_t) size);
            file.chunks.push_back (c);
        }
        return file;
    }

    std::vector<std::byte> write (std::string_view magic, std::uint32_t version,
                                  const std::vector<std::pair<std::string, std::vector<std::byte>>>& chunks,
                                  std::uint32_t flags)
    {
        auto put32 = [] (std::vector<std::byte>& out, std::size_t at, std::uint32_t v)
        {
            for (int i = 0; i < 4; ++i)
                out[at + (std::size_t) i] = (std::byte) ((v >> (8 * i)) & 0xFFu);
        };
        const std::size_t tableEnd = headerSize + chunks.size() * entrySize;
        std::vector<std::byte> out (tableEnd);
        std::memcpy (out.data(), magic.data(), 4);
        put32 (out, 4, version);
        put32 (out, 8, flags);
        put32 (out, 12, (std::uint32_t) chunks.size());
        for (std::size_t i = 0; i < chunks.size(); ++i)
        {
            const auto& [id, payload] = chunks[i];
            const std::size_t at = headerSize + i * entrySize;
            std::memcpy (out.data() + at, id.data(), 4);
            put32 (out, at + 4, (std::uint32_t) out.size());
            put32 (out, at + 8, (std::uint32_t) payload.size());
            out.insert (out.end(), payload.begin(), payload.end());
            out.resize ((out.size() + 3) & ~std::size_t (3));
        }
        put32 (out, 16, crc32 (std::span<const std::byte> (out).subspan (tableEnd)));
        return out;
    }
}
