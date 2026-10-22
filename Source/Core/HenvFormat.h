#pragma once

#include "Core/ChunkFile.h"

#include <array>
#include <optional>
#include <span>
#include <string>
#include <vector>

// .henv v1: the stage's studio environment (tools/model/ep40/env_bake.py;
// format notes in tools/model/htex_io.py). A GGX-prefiltered RGB9E5 cube map,
// SH9 irradiance of the ambient studio, and the analytic key and rim lights.
namespace halo
{
    struct HenvLight
    {
        std::string name;
        std::array<float, 3> direction {};   // towards the light, world Y-up, unit
        std::array<float, 3> irradiance {};  // at normal incidence
        float angularRadius = 0.0f, width = 0.0f, height = 0.0f, distance = 0.0f;
    };

    struct Henv
    {
        static constexpr int faceCount = 6; // +X -X +Y -Y +Z -Z (OpenGL order)

        std::uint32_t faceSize = 0, mipCount = 0;
        std::vector<std::uint32_t> cube;            // RGB9E5, mip-major then face-major
        std::array<std::array<float, 3>, 9> sh9 {}; // irradiance SH (cosine-convolved)
        std::vector<HenvLight> lights;

        std::size_t mipOffset (std::uint32_t mip) const; // index into `cube`
        std::uint32_t mipSize (std::uint32_t mip) const { return std::max (1u, faceSize >> mip); }
        const HenvLight* light (std::string_view name) const;

        static std::optional<Henv> parse (std::span<const std::byte>, std::string& error);
    };

    // GL_RGB9_E5 (EXT_texture_shared_exponent) to linear float RGB.
    std::array<float, 3> decodeRgb9e5 (std::uint32_t);
}
