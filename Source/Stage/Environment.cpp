#include "Stage/Environment.h"
#include "Core/ToneCurves.h"

#include <HaloStageAssets.h>

#include <cmath>

using namespace juce::gl;

namespace halo
{
    namespace
    {
        constexpr float pi = 3.14159265358979f;

        // The same real SH9 basis as env_bake.py and part.frag.
        std::array<float, 9> shBasis (float x, float y, float z)
        {
            return { 0.282095f, 0.488603f * y, 0.488603f * z, 0.488603f * x, 1.092548f * x * y, 1.092548f * y * z,
                     0.315392f * (3.0f * z * z - 1.0f), 1.092548f * x * z, 0.546274f * (x * x - y * y) };
        }

        float radicalInverse (std::uint32_t bits)
        {
            bits = (bits << 16u) | (bits >> 16u);
            bits = ((bits & 0x55555555u) << 1u) | ((bits & 0xAAAAAAAAu) >> 1u);
            bits = ((bits & 0x33333333u) << 2u) | ((bits & 0xCCCCCCCCu) >> 2u);
            bits = ((bits & 0x0F0F0F0Fu) << 4u) | ((bits & 0xF0F0F0F0u) >> 4u);
            bits = ((bits & 0x00FF00FFu) << 8u) | ((bits & 0xFF00FF00u) >> 8u);
            return (float) bits * 2.3283064365386963e-10f;
        }

        // Karis' split-sum DFG term with height-correlated Smith visibility:
        // (scale, bias) to F0 for (N.V, perceptual roughness).
        std::vector<float> integrateBrdf (int size, int samples)
        {
            std::vector<float> out ((size_t) (size * size * 2));
            for (int j = 0; j < size; ++j)
            {
                const float roughness = ((float) j + 0.5f) / (float) size;
                const float a = roughness * roughness, a2 = a * a;
                for (int i = 0; i < size; ++i)
                {
                    const float NoV = ((float) i + 0.5f) / (float) size;
                    const float vx = std::sqrt (1.0f - NoV * NoV), vz = NoV; // V in tangent space
                    float A = 0.0f, B = 0.0f;
                    for (int s = 0; s < samples; ++s)
                    {
                        const float u1 = ((float) s + 0.5f) / (float) samples, u2 = radicalInverse ((std::uint32_t) s);
                        const float phi = 2.0f * pi * u1;
                        const float cosT = std::sqrt ((1.0f - u2) / (1.0f + (a2 - 1.0f) * u2));
                        const float sinT = std::sqrt (1.0f - cosT * cosT);
                        const float hx = sinT * std::cos (phi), hz = cosT;
                        const float VoH = vx * hx + vz * hz;
                        const float lz = 2.0f * VoH * hz - vz; // N.L
                        if (lz <= 0.0f || VoH <= 0.0f)
                            continue;
                        const float NoL = lz, NoH = hz;
                        const float vis = 0.5f / (NoL * std::sqrt (NoV * NoV * (1.0f - a2) + a2)
                                                  + NoV * std::sqrt (NoL * NoL * (1.0f - a2) + a2));
                        const float g = vis * 4.0f * NoL * VoH / NoH;
                        const float fc = std::pow (1.0f - VoH, 5.0f);
                        A += (1.0f - fc) * g;
                        B += fc * g;
                    }
                    out[(size_t) ((j * size + i) * 2)] = A / (float) samples;
                    out[(size_t) ((j * size + i) * 2 + 1)] = B / (float) samples;
                }
            }
            return out;
        }
    }

    bool Environment::create (StageLog& log)
    {
        release();
        const auto bytes = std::span<const std::byte> (reinterpret_cast<const std::byte*> (HaloStageAssets::studio_henv),
                                                       (size_t) HaloStageAssets::studio_henvSize);
        std::string error;
        const auto env = Henv::parse (bytes, error);
        if (! env || env->light ("key") == nullptr || env->light ("rim") == nullptr)
        {
            log.line ("environment: studio.henv rejected: " + juce::String (error.empty() ? "no key or rim light" : error));
            return false;
        }

        keyLight = *env->light ("key");
        rimLight = *env->light ("rim");
        baseSH = sh = env->sh9;
        mips = (int) env->mipCount;

        glGenTextures (1, &cube);
        glBindTexture (GL_TEXTURE_CUBE_MAP, cube);
        for (std::uint32_t m = 0; m < env->mipCount; ++m)
        {
            const auto n = (GLsizei) env->mipSize (m);
            const auto* level = env->cube.data() + env->mipOffset (m);
            for (int f = 0; f < Henv::faceCount; ++f)
                glTexImage2D ((GLenum) (GL_TEXTURE_CUBE_MAP_POSITIVE_X + (GLenum) f), (GLint) m, GL_RGB9_E5, n, n, 0,
                              GL_RGB, GL_UNSIGNED_INT_5_9_9_9_REV, level + (size_t) f * (size_t) n * (size_t) n);
        }
        glTexParameteri (GL_TEXTURE_CUBE_MAP, GL_TEXTURE_BASE_LEVEL, 0);
        glTexParameteri (GL_TEXTURE_CUBE_MAP, GL_TEXTURE_MAX_LEVEL, (GLint) env->mipCount - 1);
        glTexParameteri (GL_TEXTURE_CUBE_MAP, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR);
        glTexParameteri (GL_TEXTURE_CUBE_MAP, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        for (GLenum wrap : { (GLenum) GL_TEXTURE_WRAP_S, (GLenum) GL_TEXTURE_WRAP_T, (GLenum) GL_TEXTURE_WRAP_R })
            glTexParameteri (GL_TEXTURE_CUBE_MAP, wrap, GL_CLAMP_TO_EDGE);
        glEnable (GL_TEXTURE_CUBE_MAP_SEAMLESS);
        glBindTexture (GL_TEXTURE_CUBE_MAP, 0);

        constexpr int lutSize = 64;
        const auto table = integrateBrdf (lutSize, 512);
        glGenTextures (1, &lut);
        glBindTexture (GL_TEXTURE_2D, lut);
        glTexImage2D (GL_TEXTURE_2D, 0, GL_RG16F, lutSize, lutSize, 0, GL_RG, GL_FLOAT, table.data());
        glTexParameteri (GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        glTexParameteri (GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        glTexParameteri (GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glTexParameteri (GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
        glBindTexture (GL_TEXTURE_2D, 0);

        auto fmt = [] (const std::array<float, 3>& v) { return juce::String (v[0], 3) + "," + juce::String (v[1], 3) + "," + juce::String (v[2], 3); };
        log.line ("environment: " + juce::String ((int) env->faceSize) + "^2 x " + juce::String (mips) + " mips; key dir "
                  + fmt (keyLight.direction) + " E " + fmt (keyLight.irradiance) + "; rim dir " + fmt (rimLight.direction)
                  + " E " + fmt (rimLight.irradiance));
        return true;
    }

    void Environment::release()
    {
        if (cube != 0) glDeleteTextures (1, &cube);
        if (lut != 0) glDeleteTextures (1, &lut);
        cube = lut = 0;
    }

    void Environment::setFloor (juce::Colour floorToken)
    {
        // Radiance SH of a constant lower hemisphere, by quadrature over a
        // Fibonacci sphere, then convolved with the cosine lobe like the bake.
        const std::array<float, 3> radiance { tone::srgbDecode (floorToken.getFloatRed()) * floorBounce,
                                              tone::srgbDecode (floorToken.getFloatGreen()) * floorBounce,
                                              tone::srgbDecode (floorToken.getFloatBlue()) * floorBounce };
        constexpr int n = 4096;
        std::array<float, 9> lobe {};
        for (int i = 0; i < n; ++i)
        {
            const float y = 1.0f - 2.0f * ((float) i + 0.5f) / (float) n;
            if (y >= 0.0f)
                continue;
            const float r = std::sqrt (1.0f - y * y), phi = (float) i * pi * (3.0f - std::sqrt (5.0f));
            const auto b = shBasis (r * std::cos (phi), y, r * std::sin (phi));
            for (int k = 0; k < 9; ++k)
                lobe[(size_t) k] += b[(size_t) k];
        }
        constexpr float cosine[9] { pi, 2.0f * pi / 3.0f, 2.0f * pi / 3.0f, 2.0f * pi / 3.0f,
                                    pi / 4.0f, pi / 4.0f, pi / 4.0f, pi / 4.0f, pi / 4.0f };
        for (int k = 0; k < 9; ++k)
            for (int c = 0; c < 3; ++c)
                sh[(size_t) k][(size_t) c] = baseSH[(size_t) k][(size_t) c]
                                             + lobe[(size_t) k] * (4.0f * pi / (float) n) * cosine[k] * radiance[(size_t) c];
    }
}
