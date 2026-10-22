#include <doctest/doctest.h>

#include "Core/ToneCurves.h"

#include <cmath>

using namespace halo;

namespace
{
    struct Case { tone::Rgb in, out; };

    // Reference values computed independently (numpy, double precision) from the
    // published formulas: Khronos PBR Neutral, Wrensch's minimal AgX base, and
    // Hill's ACES fit.
    const Case neutralCases[] {
        { { 0.0f, 0.0f, 0.0f }, { 0.0000000f, 0.0000000f, 0.0000000f } },
        { { 0.18f, 0.18f, 0.18f }, { 0.1400000f, 0.1400000f, 0.1400000f } },
        { { 0.5f, 0.5f, 0.5f }, { 0.4600000f, 0.4600000f, 0.4600000f } },
        { { 1.0f, 1.0f, 1.0f }, { 0.8690909f, 0.8690909f, 0.8690909f } },
        { { 4.0f, 4.0f, 4.0f }, { 0.9832558f, 0.9832558f, 0.9832558f } },
        { { 0.955f, 0.102f, 0.008f }, { 0.8652316f, 0.0956974f, 0.0108952f } },
        { { 0.716f, 0.644f, 0.521f }, { 0.6760000f, 0.6040000f, 0.4810000f } },
        { { 0.003f, 0.069f, 0.037f }, { 0.0000562f, 0.0660563f, 0.0340562f } },
        { { 2.0f, 0.5f, 0.1f }, { 0.9600000f, 0.3211358f, 0.1507720f } },
    };
    const Case agxCases[] {
        { { 0.0f, 0.0f, 0.0f }, { 0.0000000f, 0.0000000f, 0.0000000f } },
        { { 0.18f, 0.18f, 0.18f }, { 0.2144674f, 0.2145327f, 0.2145367f } },
        { { 0.5f, 0.5f, 0.5f }, { 0.4250225f, 0.4251746f, 0.4251841f } },
        { { 1.0f, 1.0f, 1.0f }, { 0.5899770f, 0.5902072f, 0.5902215f } },
        { { 4.0f, 4.0f, 4.0f }, { 0.8606224f, 0.8610029f, 0.8610266f } },
        { { 0.955f, 0.102f, 0.008f }, { 0.6684364f, 0.1617858f, 0.0511059f } },
        { { 0.716f, 0.644f, 0.521f }, { 0.5113502f, 0.4854973f, 0.4358766f } },
        { { 0.003f, 0.069f, 0.037f }, { 0.0028419f, 0.0916060f, 0.0439860f } },
        { { 2.0f, 0.5f, 0.1f }, { 0.8072893f, 0.4496241f, 0.2106905f } },
    };
    const Case acesCases[] {
        { { 0.0f, 0.0f, 0.0f }, { 0.0000000f, 0.0000000f, 0.0000000f } },
        { { 0.18f, 0.18f, 0.18f }, { 0.1055912f, 0.1055912f, 0.1055902f } },
        { { 0.5f, 0.5f, 0.5f }, { 0.3743083f, 0.3743083f, 0.3743046f } },
        { { 1.0f, 1.0f, 1.0f }, { 0.6191154f, 0.6191154f, 0.6191092f } },
        { { 4.0f, 4.0f, 4.0f }, { 0.9090138f, 0.9090138f, 0.9090047f } },
        { { 0.955f, 0.102f, 0.008f }, { 0.6619476f, 0.0577188f, 0.0055156f } },
        { { 0.716f, 0.644f, 0.521f }, { 0.5014786f, 0.4659444f, 0.3987774f } },
        { { 0.003f, 0.069f, 0.037f }, { 0.0000000f, 0.0219890f, 0.0090671f } },
        { { 2.0f, 0.5f, 0.1f }, { 0.9014993f, 0.4186894f, 0.1050008f } },
    };

    void check (ToneMapper t, const Case& c, float tolerance)
    {
        const auto got = tone::apply (t, c.in);
        for (int i = 0; i < 3; ++i)
        {
            INFO (tone::name (t) << " in " << c.in[0] << "," << c.in[1] << "," << c.in[2] << " channel " << i);
            CHECK (got[(size_t) i] == doctest::Approx (c.out[(size_t) i]).scale (1.0).epsilon (tolerance));
        }
    }
}

TEST_CASE ("tone curves: match the reference formulas")
{
    for (const auto& c : neutralCases) check (ToneMapper::neutral, c, 1e-5f);
    for (const auto& c : agxCases) check (ToneMapper::agx, c, 2e-4f);
    for (const auto& c : acesCases) check (ToneMapper::aces, c, 1e-5f);
}

TEST_CASE ("tone curves: monotonic on grey and bounded to display range")
{
    for (auto t : { ToneMapper::neutral, ToneMapper::agx, ToneMapper::aces })
    {
        float previous = -1.0f;
        for (int i = 0; i <= 400; ++i)
        {
            const float x = std::pow (2.0f, -12.0f + 18.0f * (float) i / 400.0f);
            const float y = tone::apply (t, { x, x, x })[1];
            INFO (tone::name (t) << " x " << x);
            CHECK (y >= previous - 1e-6f);
            CHECK (y >= 0.0f);
            CHECK (y <= 1.0f);
            previous = y;
        }
    }
}

TEST_CASE ("tone curves: Neutral leaves mid-range albedo hue alone")
{
    // Below the compression start the curve is a pure offset, so the riddim
    // orange and green keep their hue (the reason Neutral is the default, D2).
    const auto out = tone::neutral ({ 0.30f, 0.20f, 0.10f });
    CHECK (out[0] - out[1] == doctest::Approx (0.10f));
    CHECK (out[1] - out[2] == doctest::Approx (0.10f));
}

TEST_CASE ("sRGB: the OETF matches the piecewise definition and inverts")
{
    CHECK (tone::srgbEncode (0.0f) == 0.0f);
    CHECK (tone::srgbEncode (1.0f) == doctest::Approx (1.0f));
    // Linear segment below the knee, power segment above, continuous at it.
    CHECK (tone::srgbEncode (0.002f) == doctest::Approx (0.002f * 12.92f));
    CHECK (tone::srgbEncode (0.5f) == doctest::Approx (1.055f * std::pow (0.5f, 1.0f / 2.4f) - 0.055f));
    const float below = tone::srgbEncode (0.0031308f - 1e-7f), above = tone::srgbEncode (0.0031308f + 1e-7f);
    CHECK (std::abs (above - below) < 1e-5f);
    // Every 8-bit code survives decode -> encode.
    for (int i = 0; i <= 255; ++i)
    {
        const float v = (float) i / 255.0f;
        CHECK (std::lround (tone::srgbEncode (tone::srgbDecode (v)) * 255.0f) == i);
    }
    CHECK (tone::srgbDecode (0.5f) == doctest::Approx (0.2140411f));
}

TEST_CASE ("tone curves: names round trip")
{
    for (auto t : { ToneMapper::neutral, ToneMapper::agx, ToneMapper::aces })
        CHECK (tone::parse (tone::name (t)) == t);
    CHECK (tone::parse ("filmic", ToneMapper::agx) == ToneMapper::agx);
}
