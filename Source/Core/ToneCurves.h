#pragma once

#include <algorithm>
#include <array>
#include <cmath>
#include <string_view>

// The stage's tone curves and sRGB transfer functions in C++ (plan section 3.3).
// The GLSL side expands the same ToneCurveConstants.inc, so both use one set of
// numbers; Tests/ToneCurvesTests.cpp checks these against the reference
// formulas, and a Debug self-test in the stage checks the GLSL against these.
namespace halo
{
    enum class ToneMapper { neutral, agx, aces };

    namespace tone
    {
        #define HALO_TC(name, value) inline constexpr float name = (float) (value);
        #include "Core/ToneCurveConstants.inc"
        #undef HALO_TC

        using Rgb = std::array<float, 3>;

        inline constexpr std::string_view name (ToneMapper t)
        {
            return t == ToneMapper::agx ? "agx" : t == ToneMapper::aces ? "aces" : "neutral";
        }

        inline ToneMapper parse (std::string_view s, ToneMapper fallback = ToneMapper::neutral)
        {
            if (s == "neutral") return ToneMapper::neutral;
            if (s == "agx") return ToneMapper::agx;
            if (s == "aces") return ToneMapper::aces;
            return fallback;
        }

        // Exact piecewise sRGB encode (OETF) and decode.
        inline float srgbEncode (float x)
        {
            x = std::clamp (x, 0.0f, 1.0f);
            return x <= srgbLinearKnee ? x * srgbLinearSlope : srgbScale * std::pow (x, 1.0f / srgbGamma) - srgbOffset;
        }

        inline float srgbDecode (float v)
        {
            v = std::clamp (v, 0.0f, 1.0f);
            return v <= srgbEncodedKnee ? v / srgbLinearSlope : std::pow ((v + srgbOffset) / srgbScale, srgbGamma);
        }

        // Khronos PBR Neutral: scene-linear in, display-linear out.
        inline Rgb neutral (Rgb c)
        {
            const float x = std::min ({ c[0], c[1], c[2] });
            const float offset = x < neutralToeKnee ? x - neutralToeScale * x * x : neutralOffset;
            for (auto& v : c) v -= offset;
            const float peak = std::max ({ c[0], c[1], c[2] });
            if (peak < neutralStartCompression)
                return c;
            const float d = 1.0f - neutralStartCompression;
            const float newPeak = 1.0f - d * d / (peak + d - neutralStartCompression);
            for (auto& v : c) v *= newPeak / peak;
            const float g = 1.0f - 1.0f / (neutralDesaturation * (peak - newPeak) + 1.0f);
            for (auto& v : c) v = v * (1.0f - g) + newPeak * g;
            return c;
        }

        // AgX base: scene-linear (Rec.709) in, display-linear out.
        inline Rgb agx (Rgb c)
        {
            const float in[9] { agxIn00, agxIn01, agxIn02, agxIn10, agxIn11, agxIn12, agxIn20, agxIn21, agxIn22 };
            const float out[9] { agxOut00, agxOut01, agxOut02, agxOut10, agxOut11, agxOut12, agxOut20, agxOut21, agxOut22 };
            auto mul = [] (const float* m, Rgb v) // column-major
            {
                return Rgb { m[0] * v[0] + m[3] * v[1] + m[6] * v[2],
                             m[1] * v[0] + m[4] * v[1] + m[7] * v[2],
                             m[2] * v[0] + m[5] * v[1] + m[8] * v[2] };
            };
            c = mul (in, c);
            for (auto& v : c)
            {
                v = std::clamp (std::log2 (std::max (v, 1e-10f)), agxMinEv, agxMaxEv);
                v = (v - agxMinEv) / (agxMaxEv - agxMinEv);
                const float x2 = v * v, x4 = x2 * x2;
                v = agxC6 * x4 * x2 + agxC5 * x4 * v + agxC4 * x4 + agxC3 * x2 * v + agxC2 * x2 + agxC1 * v + agxC0;
            }
            c = mul (out, c);
            for (auto& v : c)
                v = std::pow (std::clamp (v, 0.0f, 1.0f), agxDisplayGamma);
            return c;
        }

        // ACES, Hill's fit: scene-linear in, display-linear out (comparison only).
        inline Rgb aces (Rgb c)
        {
            const float in[9] { acesIn00, acesIn01, acesIn02, acesIn10, acesIn11, acesIn12, acesIn20, acesIn21, acesIn22 };
            const float out[9] { acesOut00, acesOut01, acesOut02, acesOut10, acesOut11, acesOut12, acesOut20, acesOut21, acesOut22 };
            auto mul = [] (const float* m, Rgb v) // row-major
            {
                return Rgb { m[0] * v[0] + m[1] * v[1] + m[2] * v[2],
                             m[3] * v[0] + m[4] * v[1] + m[5] * v[2],
                             m[6] * v[0] + m[7] * v[1] + m[8] * v[2] };
            };
            c = mul (in, c);
            for (auto& v : c)
                v = (v * (v + acesA) - acesB) / (v * (acesC * v + acesD) + acesE);
            c = mul (out, c);
            for (auto& v : c)
                v = std::clamp (v, 0.0f, 1.0f);
            return c;
        }

        inline Rgb apply (ToneMapper t, Rgb c)
        {
            switch (t)
            {
                case ToneMapper::agx:  return agx (c);
                case ToneMapper::aces: return aces (c);
                case ToneMapper::neutral: break;
            }
            return neutral (c);
        }
    }
}
