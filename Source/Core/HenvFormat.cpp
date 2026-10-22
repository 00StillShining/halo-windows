#include "Core/HenvFormat.h"

#include <cmath>

namespace halo
{
    std::array<float, 3> decodeRgb9e5 (std::uint32_t v)
    {
        const int exponent = (int) (v >> 27) - 15 - 9;
        const float scale = std::ldexp (1.0f, exponent);
        return { (float) (v & 511u) * scale, (float) ((v >> 9) & 511u) * scale, (float) ((v >> 18) & 511u) * scale };
    }

    std::size_t Henv::mipOffset (std::uint32_t mip) const
    {
        std::size_t offset = 0;
        for (std::uint32_t m = 0; m < mip; ++m)
            offset += (std::size_t) faceCount * mipSize (m) * mipSize (m);
        return offset;
    }

    const HenvLight* Henv::light (std::string_view name) const
    {
        for (const auto& l : lights)
            if (l.name == name)
                return &l;
        return nullptr;
    }

    std::optional<Henv> Henv::parse (std::span<const std::byte> data, std::string& error)
    {
        using namespace chunkfile;
        const auto file = chunkfile::parse (data, "HENV", 1, error);
        if (! file)
            return std::nullopt;

        const auto* head = file->find ("ENVH");
        const auto* cube = file->find ("CUBE");
        const auto* sh = file->find ("SH9 ");
        const auto* lite = file->find ("LITE");
        if (head == nullptr || cube == nullptr || sh == nullptr || lite == nullptr)
            return error = "missing a required chunk", std::nullopt;
        if (head->data.size() < 12)
            return error = "ENVH too short", std::nullopt;

        Henv env;
        env.faceSize = u32 (head->data, 0);
        env.mipCount = u32 (head->data, 4);
        const auto format = u32 (head->data, 8);
        if (format != 1)
            return error = "unknown cube format", std::nullopt;
        if (env.faceSize == 0 || env.faceSize > 4096 || (env.faceSize & (env.faceSize - 1)) != 0)
            return error = "face size must be a power of two up to 4096", std::nullopt;
        if (env.mipCount == 0 || (env.faceSize >> (env.mipCount - 1)) == 0)
            return error = "bad mip count", std::nullopt;

        const auto texels = env.mipOffset (env.mipCount);
        if (cube->data.size() != texels * 4)
            return error = "CUBE size does not match its mips", std::nullopt;
        env.cube.resize (texels);
        for (std::size_t i = 0; i < texels; ++i)
            env.cube[i] = u32 (cube->data, i * 4);

        if (sh->data.size() != 9 * 3 * 4)
            return error = "SH9 must hold 27 floats", std::nullopt;
        for (std::size_t i = 0; i < 9; ++i)
            for (std::size_t c = 0; c < 3; ++c)
            {
                const float v = f32 (sh->data, (i * 3 + c) * 4);
                if (! std::isfinite (v))
                    return error = "non-finite SH9 coefficient", std::nullopt;
                env.sh9[i][c] = v;
            }

        if (lite->data.size() < 4)
            return error = "LITE too short", std::nullopt;
        const auto count = u32 (lite->data, 0);
        constexpr std::size_t record = 48;
        if (count > 16 || lite->data.size() < 4 + count * record)
            return error = "LITE overruns its chunk", std::nullopt;
        for (std::uint32_t i = 0; i < count; ++i)
        {
            const std::size_t at = 4 + i * record;
            HenvLight l;
            const char* name = reinterpret_cast<const char*> (lite->data.data() + at);
            l.name.assign (name, strnlen (name, 8));
            float values[10];
            for (int k = 0; k < 10; ++k)
            {
                values[k] = f32 (lite->data, at + 8 + (std::size_t) k * 4);
                if (! std::isfinite (values[k]))
                    return error = "non-finite light value", std::nullopt;
            }
            l.direction = { values[0], values[1], values[2] };
            l.irradiance = { values[3], values[4], values[5] };
            l.angularRadius = values[6];
            l.width = values[7];
            l.height = values[8];
            l.distance = values[9];
            env.lights.push_back (l);
        }
        return env;
    }
}
