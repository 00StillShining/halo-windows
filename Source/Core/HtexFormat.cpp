#include "Core/HtexFormat.h"

namespace halo
{
    int Htex::channelCount (Format f)
    {
        switch (f)
        {
            case Format::r8: return 1;
            case Format::rg8: case Format::rg16f: return 2;
            case Format::rgba8: case Format::rgba16f: return 4;
        }
        return 0;
    }

    int Htex::bytesPerTexel (Format f)
    {
        switch (f)
        {
            case Format::r8: return 1;
            case Format::rg8: return 2;
            case Format::rgba8: case Format::rg16f: return 4;
            case Format::rgba16f: return 8;
        }
        return 0;
    }

    std::array<std::uint8_t, 4> Htex::texel8 (std::size_t level, std::uint32_t x, std::uint32_t y) const
    {
        std::array<std::uint8_t, 4> out {};
        const int n = channelCount (format);
        if (level >= mips.size() || bytesPerTexel (format) != n || x >= mipWidth (level) || y >= mipHeight (level))
            return out;
        const std::size_t at = ((std::size_t) y * mipWidth (level) + x) * (std::size_t) n;
        for (int c = 0; c < n; ++c)
            out[(size_t) c] = (std::uint8_t) mips[level][at + (std::size_t) c];
        return out;
    }

    std::optional<Htex> Htex::parse (std::span<const std::byte> data, std::string& error)
    {
        using namespace chunkfile;
        const auto file = chunkfile::parse (data, "HTEX", 1, error);
        if (! file)
            return std::nullopt;
        const auto* head = file->find ("TXHD");
        const auto* body = file->find ("MIPS");
        if (head == nullptr || body == nullptr)
            return error = "missing a required chunk", std::nullopt;
        if (head->data.size() < 24)
            return error = "TXHD too short", std::nullopt;

        Htex t;
        t.width = u32 (head->data, 0);
        t.height = u32 (head->data, 4);
        const auto format = u32 (head->data, 8);
        const auto mipCount = u32 (head->data, 12);
        t.flags = u32 (head->data, 16);
        for (std::size_t i = 0; i < 4; ++i)
            t.channels[i] = (char) head->data[20 + i];

        if (format < 1 || format > 5)
            return error = "unknown format", std::nullopt;
        t.format = (Format) format;
        if (t.width == 0 || t.height == 0 || t.width > 16384 || t.height > 16384)
            return error = "bad size", std::nullopt;
        if (mipCount == 0 || mipCount > 15 || ((t.width | t.height) >> (mipCount - 1)) == 0)
            return error = "bad mip count", std::nullopt;

        std::size_t offset = 0;
        for (std::uint32_t m = 0; m < mipCount; ++m)
        {
            const std::size_t bytes = (std::size_t) t.mipWidth (m) * t.mipHeight (m) * (std::size_t) bytesPerTexel (t.format);
            if (offset + bytes > body->data.size())
                return error = "MIPS shorter than its levels", std::nullopt;
            t.mips.push_back (body->data.subspan (offset, bytes));
            offset += bytes;
        }
        if (offset != body->data.size())
            return error = "MIPS longer than its levels", std::nullopt;
        return t;
    }
}
