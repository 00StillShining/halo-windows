#include <doctest/doctest.h>

#include "Core/HenvFormat.h"

#include <cmath>
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

    void putF (Bytes& b, float f)
    {
        std::uint32_t v;
        std::memcpy (&v, &f, 4);
        put32 (b, v);
    }

    // A small but complete environment: 4x4 faces, 3 mips, two lights.
    Bytes makeHenv (std::uint32_t faceSize = 4, std::uint32_t mips = 3, bool dropCubeTexel = false)
    {
        Bytes head;
        put32 (head, faceSize);
        put32 (head, mips);
        put32 (head, 1);

        Bytes cube;
        std::uint32_t n = 0;
        for (std::uint32_t m = 0; m < mips; ++m)
            for (int f = 0; f < 6; ++f)
                for (std::uint32_t i = 0; i < std::max (1u, faceSize >> m) * std::max (1u, faceSize >> m); ++i)
                    put32 (cube, 0x84020100u + (n++ & 0xFFu)); // around (1, 1, 1)
        if (dropCubeTexel)
            cube.resize (cube.size() - 4);

        Bytes sh;
        for (int i = 0; i < 27; ++i)
            putF (sh, 0.1f * (float) i);

        Bytes lite;
        put32 (lite, 2);
        for (const char* name : { "key", "rim" })
        {
            char padded[8] {};
            std::memcpy (padded, name, std::strlen (name));
            for (char c : padded)
                lite.push_back ((std::byte) c);
            for (float v : { 0.0f, 1.0f, 0.0f, 1.8f, 1.7f, 1.6f, 0.3f, 0.9f, 0.6f, 1.2f })
                putF (lite, v);
        }

        return chunkfile::write ("HENV", 1, { { "ENVH", head }, { "CUBE", cube }, { "SH9 ", sh }, { "LITE", lite },
                                              { "XTRA", Bytes (7, std::byte { 0x5A }) } });
    }
}

TEST_CASE ("henv: RGB9E5 decodes known vectors (GL_EXT_texture_shared_exponent)")
{
    auto near = [] (std::array<float, 3> a, std::array<float, 3> b)
    {
        for (int i = 0; i < 3; ++i)
            CHECK (a[(size_t) i] == doctest::Approx (b[(size_t) i]).epsilon (1e-6));
    };
    near (decodeRgb9e5 (0x0u), { 0.0f, 0.0f, 0.0f });
    near (decodeRgb9e5 (0x84020100u), { 1.0f, 1.0f, 1.0f });
    near (decodeRgb9e5 (0x8c004040u), { 0.5f, 0.25f, 2.0f });
    near (decodeRgb9e5 (0x9586e980u), { 6.0f, 5.8125f, 5.515625f });
    near (decodeRgb9e5 (0x56c7a7ecu), { 0.030029296875f, 0.02850341796875f, 0.02642822265625f });
    near (decodeRgb9e5 (0xf80001ffu), { 65408.0f, 0.0f, 0.0f });
}

TEST_CASE ("henv: a well-formed file parses, unknown chunks are skipped")
{
    const auto data = makeHenv();
    std::string error;
    const auto env = Henv::parse (data, error);
    REQUIRE_MESSAGE (env.has_value(), error);
    CHECK (env->faceSize == 4);
    CHECK (env->mipCount == 3);
    CHECK (env->mipSize (0) == 4);
    CHECK (env->mipSize (2) == 1);
    CHECK (env->cube.size() == 6u * (16 + 4 + 1));
    CHECK (env->mipOffset (1) == 6u * 16);
    CHECK (env->mipOffset (2) == 6u * (16 + 4));
    CHECK (env->sh9.size() == 9);
    CHECK (env->sh9[8][2] == doctest::Approx (2.6f));
    REQUIRE (env->lights.size() == 2);
    REQUIRE (env->light ("rim") != nullptr);
    CHECK (env->light ("key")->irradiance[0] == doctest::Approx (1.8f));
    CHECK (env->light ("rim")->distance == doctest::Approx (1.2f));
}

TEST_CASE ("henv: the shipped studio environment parses")
{
#ifdef HALO_ASSET_DIR
    std::FILE* f = std::fopen (HALO_ASSET_DIR "/stage/studio.henv", "rb");
    REQUIRE (f != nullptr);
    std::vector<std::byte> data;
    std::byte buffer[65536];
    for (size_t n; (n = std::fread (buffer, 1, sizeof buffer, f)) > 0;)
        data.insert (data.end(), buffer, buffer + n);
    std::fclose (f);
    std::string error;
    const auto env = Henv::parse (data, error);
    REQUIRE_MESSAGE (env.has_value(), error);
    CHECK (env->faceSize == 256);
    CHECK (env->mipCount == 7);
    REQUIRE (env->light ("key") != nullptr);
    REQUIRE (env->light ("rim") != nullptr);
    // The key is above the device and in front; the rim behind it.
    CHECK (env->light ("key")->direction[1] > 0.5f);
    CHECK (env->light ("rim")->direction[2] < -0.5f);
    for (const auto& l : env->lights)
        CHECK (std::abs (std::hypot (l.direction[0], l.direction[1], l.direction[2]) - 1.0f) < 1e-4f);
#else
    MESSAGE ("HALO_ASSET_DIR not set");
#endif
}

TEST_CASE ("henv: malformed files are rejected, never trusted")
{
    std::string error;
    const auto good = makeHenv();

    auto bad = good;
    bad[0] = std::byte { 'X' };
    CHECK_FALSE (Henv::parse (bad, error).has_value());

    bad = good;
    bad[4] = std::byte { 9 }; // version 9
    CHECK_FALSE (Henv::parse (bad, error).has_value());

    bad = good;
    bad.back() ^= std::byte { 0xFF }; // payload flip -> CRC
    CHECK_FALSE (Henv::parse (bad, error).has_value());
    CHECK (error == "CRC mismatch");

    CHECK_FALSE (Henv::parse (makeHenv (4, 3, true), error).has_value()); // CUBE short
    CHECK_FALSE (Henv::parse (makeHenv (6, 2), error).has_value());       // not a power of two
    CHECK_FALSE (Henv::parse (makeHenv (4, 4), error).has_value());       // mips past 1x1

    // Every truncation fails cleanly.
    for (size_t n = 0; n < good.size(); n += 7)
        CHECK_FALSE (Henv::parse (std::span<const std::byte> (good.data(), n), error).has_value());
}
