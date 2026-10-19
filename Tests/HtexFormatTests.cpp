#include <doctest/doctest.h>

#include "Core/HtexFormat.h"

#include <cstring>

using namespace halo;

namespace
{
    using Bytes = std::vector<std::byte>;

    void put32 (Bytes& b, std::uint32_t v)
    {
        for (int i = 0; i < 4; ++i)
            b.push_back ((std::byte) ((v >> (8 * i)) & 0xFFu));
    }

    // An RGBA8 4x2 texture with 3 mips (4x2, 2x1, 1x1). Texel (x, y) of level
    // l is (10 l + x, 100 + y, 30, 40 + l).
    Bytes makeHtex (std::uint32_t mips = 3, int dropBytes = 0)
    {
        Bytes head;
        put32 (head, 4);
        put32 (head, 2);
        put32 (head, 3); // RGBA8
        put32 (head, mips);
        put32 (head, 2); // SDF
        for (char c : { 'G', 'O', 'W', 'C' })
            head.push_back ((std::byte) c);

        Bytes body;
        for (std::uint32_t l = 0; l < mips; ++l)
            for (std::uint32_t y = 0; y < std::max (1u, 2u >> l); ++y)
                for (std::uint32_t x = 0; x < std::max (1u, 4u >> l); ++x)
                    for (std::uint32_t v : { 10 * l + x, 100 + y, 30u, 40 + l })
                        body.push_back ((std::byte) v);
        body.resize (body.size() - (size_t) dropBytes);
        return chunkfile::write ("HTEX", 1, { { "TXHD", head }, { "MIPS", body } });
    }
}

TEST_CASE ("htex: a known RGBA texel decodes unpremultiplied, in RGBA order")
{
    const auto data = makeHtex();
    std::string error;
    const auto t = Htex::parse (data, error);
    REQUIRE_MESSAGE (t.has_value(), error);
    CHECK (t->width == 4);
    CHECK (t->height == 2);
    CHECK (t->format == Htex::Format::rgba8);
    CHECK (t->flags == Htex::sdf);
    CHECK (std::string (t->channels.data(), 4) == "GOWC");
    REQUIRE (t->mips.size() == 3);
    CHECK (t->mips[0].size() == 4 * 2 * 4);
    CHECK (t->mips[1].size() == 2 * 1 * 4);
    CHECK (t->mips[2].size() == 4);

    // Alpha 40 with colour (3, 101, 30): a premultiplying loader would have
    // scaled the colour by 40/255, and a BGRA one swapped R and B.
    const auto texel = t->texel8 (0, 3, 1);
    CHECK (texel[0] == 3);
    CHECK (texel[1] == 101);
    CHECK (texel[2] == 30);
    CHECK (texel[3] == 40);
    CHECK (t->texel8 (2, 0, 0)[3] == 42);
}

TEST_CASE ("htex: malformed files are rejected")
{
    std::string error;
    CHECK_FALSE (Htex::parse (makeHtex (3, 1), error).has_value()); // MIPS short
    CHECK_FALSE (Htex::parse (makeHtex (4), error).has_value());    // a mip past 1x1
    const auto good = makeHtex();
    auto bad = good;
    bad[good.size() - 2] ^= std::byte { 1 };
    CHECK_FALSE (Htex::parse (bad, error).has_value());
    for (size_t n = 0; n < good.size(); ++n)
        CHECK_FALSE (Htex::parse (std::span<const std::byte> (good.data(), n), error).has_value());
}

TEST_CASE ("chunk file: CRC-32 matches the standard check value")
{
    const char* text = "123456789";
    CHECK (chunkfile::crc32 (std::as_bytes (std::span<const char> (text, 9))) == 0xCBF43926u);
}
