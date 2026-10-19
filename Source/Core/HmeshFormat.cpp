#include "Core/HmeshFormat.h"
#include "Core/Ep40MidiMapping.h"

#include <algorithm>
#include <cmath>
#include <map>

namespace halo
{
    namespace
    {
        using chunkfile::f32;
        using chunkfile::u32;

        std::uint16_t u16 (std::span<const std::byte> s, std::size_t at)
        {
            return (std::uint16_t) ((std::uint16_t) s[at] | ((std::uint16_t) s[at + 1] << 8));
        }

        bool finite (std::initializer_list<float> values)
        {
            for (auto v : values)
                if (! std::isfinite (v))
                    return false;
            return true;
        }

        std::uint8_t slotFor (std::string_view name)
        {
            for (std::size_t i = 0; i < ep40::gridPartNames.size(); ++i)
                if (name == ep40::gridPartNames[i])
                    return (std::uint8_t) (1 + i);
            for (std::size_t i = 0; i < ep40::groupPartNames.size(); ++i)
                if (name == ep40::groupPartNames[i])
                    return (std::uint8_t) (1 + ep40::gridPartNames.size() + i);
            return 0;
        }

        std::uint8_t kindFor (std::string_view name)
        {
            auto starts = [&] (std::string_view p) { return name.substr (0, p.size()) == p; };
            if (slotFor (name) != 0)
                return starts ("group_") ? HmeshPart::group : HmeshPart::pad;
            if (starts ("button_") || name == "fader_mode_button") return HmeshPart::button;
            if (starts ("knob_")) return HmeshPart::knob;
            if (starts ("fader_")) return HmeshPart::fader;
            if (starts ("status_lamp_") || starts ("led_")) return HmeshPart::led;
            if (name == "display_surface") return HmeshPart::display;
            if (name == "halo_ring") return HmeshPart::anchor;
            if (name == "chassis_topPlate" || name == "brand_panel") return HmeshPart::plate;
            return HmeshPart::staticPart;
        }

        // v1 and v2: a plain list of parts, each with its own transform and
        // float vertices (v1: 12 floats, v2: 14 with roughness and metallic).
        std::optional<Hmesh> parseLegacy (std::span<const std::byte> data, std::uint32_t version, std::string& error)
        {
            const std::size_t floats = version == 1 ? 12 : 14;
            if (data.size() < 12)
                return error = "truncated header", std::nullopt;
            const auto partCount = u32 (data, 8);
            if (partCount > 4096)
                return error = "absurd part count", std::nullopt;

            Hmesh mesh;
            mesh.version = version;
            std::map<std::array<float, 6>, std::uint8_t> materialOf;
            std::array<float, 3> lo { 1e9f, 1e9f, 1e9f }, hi { -1e9f, -1e9f, -1e9f };
            std::size_t at = 12;
            auto need = [&] (std::size_t n) { return at + n <= data.size(); };

            for (std::uint32_t p = 0; p < partCount; ++p)
            {
                if (! need (2))
                    return error = "truncated part name", std::nullopt;
                const auto nameLength = u16 (data, at);
                at += 2;
                if (! need (nameLength + 64u + 12u + 4u))
                    return error = "truncated part", std::nullopt;
                HmeshPart part;
                part.name.assign (reinterpret_cast<const char*> (data.data() + at), nameLength);
                at += nameLength;
                float m[16];
                for (int i = 0; i < 16; ++i)
                    m[i] = f32 (data, at + (std::size_t) i * 4);
                at += 64 + 12; // matrix, pivot (recomputed below)
                const auto vertexCount = u32 (data, at);
                at += 4;
                if (vertexCount > 1u << 22 || ! need ((std::size_t) vertexCount * floats * 4 + 4))
                    return error = "truncated vertices", std::nullopt;

                part.kind = kindFor (part.name);
                part.slot = slotFor (part.name);
                part.baseVertex = (std::uint32_t) mesh.vertices.size();
                part.vertexCount = vertexCount;
                std::array<float, 3> plo { 1e9f, 1e9f, 1e9f }, phi { -1e9f, -1e9f, -1e9f };
                for (std::uint32_t v = 0; v < vertexCount; ++v)
                {
                    float f[14] { 0, 0, 0, 0, 0, 1, 0.8f, 0.8f, 0.8f, 0, 0, 0, 0.5f, 0.0f };
                    for (std::size_t k = 0; k < floats; ++k)
                        f[k] = f32 (data, at + (v * floats + k) * 4);
                    for (float x : f)
                        if (! std::isfinite (x))
                            return error = "non-finite vertex", std::nullopt;
                    HmeshVertex out {};
                    for (int r = 0; r < 3; ++r)
                    {
                        out.position[r] = m[r] * f[0] + m[4 + r] * f[1] + m[8 + r] * f[2] + m[12 + r];
                        plo[(size_t) r] = std::min (plo[(size_t) r], out.position[r]);
                        phi[(size_t) r] = std::max (phi[(size_t) r], out.position[r]);
                    }
                    float n[3];
                    for (int r = 0; r < 3; ++r)
                        n[r] = m[r] * f[3] + m[4 + r] * f[4] + m[8 + r] * f[5];
                    const float len = std::sqrt (n[0] * n[0] + n[1] * n[1] + n[2] * n[2]);
                    const auto packed = Hmesh::encodeNormal (n[0] / len, n[1] / len, n[2] / len);
                    out.normal[0] = packed[0];
                    out.normal[1] = packed[1];
                    out.uv[0] = (std::uint16_t) std::lround (std::clamp (f[10], 0.0f, 1.0f) * 65535.0f);
                    out.uv[1] = (std::uint16_t) std::lround (std::clamp (f[11], 0.0f, 1.0f) * 65535.0f);
                    const std::array<float, 6> key { f[6], f[7], f[8], f[9], f[12], f[13] };
                    auto it = materialOf.find (key);
                    if (it == materialOf.end())
                    {
                        if (mesh.materials.size() >= 255)
                            return error = "more than 255 materials", std::nullopt;
                        HmeshMaterial mat;
                        mat.name = "legacy" + std::to_string (mesh.materials.size());
                        mat.base = { f[6], f[7], f[8] };
                        mat.emissive = { f[6] * f[9], f[7] * f[9], f[8] * f[9] };
                        mat.roughness = f[12];
                        mat.metallic = f[13];
                        if (part.name == "display_surface") mat.flags |= HmeshMaterial::lcd;
                        it = materialOf.emplace (key, (std::uint8_t) mesh.materials.size()).first;
                        mesh.materials.push_back (mat);
                    }
                    out.material = it->second;
                    out.slot = part.slot;
                    out.mask = 0;
                    out.ao = 255;
                    mesh.vertices.push_back (out);
                }
                at += (std::size_t) vertexCount * floats * 4;

                const auto indexCount = u32 (data, at);
                at += 4;
                if (indexCount % 3 != 0 || ! need ((std::size_t) indexCount * 4))
                    return error = "truncated indices", std::nullopt;
                part.firstIndex = (std::uint32_t) mesh.indices.size();
                part.indexCount = indexCount;
                for (std::uint32_t i = 0; i < indexCount; ++i)
                {
                    const auto index = u32 (data, at + (std::size_t) i * 4);
                    if (index >= vertexCount)
                        return error = "index out of range in " + part.name, std::nullopt;
                    mesh.indices.push_back (part.baseVertex + index);
                }
                at += (std::size_t) indexCount * 4;

                if (vertexCount > 0)
                {
                    part.material = mesh.vertices[part.baseVertex].material;
                    part.bounds = { plo[0], plo[1], plo[2], phi[0], phi[1], phi[2] };
                    for (int r = 0; r < 3; ++r)
                        part.pivot[(size_t) r] = 0.5f * (plo[(size_t) r] + phi[(size_t) r]);
                }
                if (indexCount > 0 && part.name != "halo_ring")
                {
                    part.flags |= HmeshPart::castsShadow;
                    for (int r = 0; r < 3; ++r)
                    {
                        lo[(size_t) r] = std::min (lo[(size_t) r], plo[(size_t) r]);
                        hi[(size_t) r] = std::max (hi[(size_t) r], phi[(size_t) r]);
                    }
                }
                if (part.slot != 0)
                    part.flags |= HmeshPart::moving;
                mesh.parts.push_back (std::move (part));
            }
            if (lo[0] <= hi[0])
            {
                mesh.boundsMin = lo;
                mesh.boundsMax = hi;
            }
            return mesh;
        }
    }

    std::array<std::int16_t, 2> Hmesh::encodeNormal (float x, float y, float z)
    {
        const float s = std::abs (x) + std::abs (y) + std::abs (z);
        if (s <= 0.0f)
            return { 0, 0 };
        x /= s; y /= s; z /= s;
        float ox = x, oy = y;
        if (z < 0.0f)
        {
            ox = (1.0f - std::abs (y)) * (x >= 0.0f ? 1.0f : -1.0f);
            oy = (1.0f - std::abs (x)) * (y >= 0.0f ? 1.0f : -1.0f);
        }
        return { (std::int16_t) std::lround (std::clamp (ox, -1.0f, 1.0f) * 32767.0f),
                 (std::int16_t) std::lround (std::clamp (oy, -1.0f, 1.0f) * 32767.0f) };
    }

    std::array<float, 3> Hmesh::decodeNormal (std::int16_t ex, std::int16_t ey)
    {
        float x = (float) ex / 32767.0f, y = (float) ey / 32767.0f;
        float z = 1.0f - std::abs (x) - std::abs (y);
        if (z < 0.0f)
        {
            const float nx = (1.0f - std::abs (y)) * (x >= 0.0f ? 1.0f : -1.0f);
            const float ny = (1.0f - std::abs (x)) * (y >= 0.0f ? 1.0f : -1.0f);
            x = nx;
            y = ny;
        }
        const float len = std::sqrt (x * x + y * y + z * z);
        return { x / len, y / len, z / len };
    }

    const HmeshPart* Hmesh::find (std::string_view name) const
    {
        for (const auto& p : parts)
            if (p.name == name)
                return &p;
        return nullptr;
    }

    int Hmesh::indexOf (std::string_view name) const
    {
        for (std::size_t i = 0; i < parts.size(); ++i)
            if (parts[i].name == name)
                return (int) i;
        return -1;
    }

    std::optional<Hmesh> Hmesh::parse (std::span<const std::byte> data, std::string& error)
    {
        if (data.size() < 8 || std::memcmp (data.data(), "HMSH", 4) != 0)
            return error = "bad magic", std::nullopt;
        const auto version = u32 (data, 4);
        if (version == 1 || version == 2)
            return parseLegacy (data, version, error);

        const auto file = chunkfile::parse (data, "HMSH", 3, error);
        if (! file)
            return std::nullopt;
        const auto* head = file->find ("HEAD");
        const auto* matl = file->find ("MATL");
        const auto* part = file->find ("PART");
        const auto* vert = file->find ("VERT");
        const auto* indx = file->find ("INDX");
        if (head == nullptr || matl == nullptr || part == nullptr || vert == nullptr || indx == nullptr)
            return error = "missing a required chunk", std::nullopt;
        if (head->data.size() < 40)
            return error = "HEAD too short", std::nullopt;

        Hmesh mesh;
        mesh.version = 3;
        for (int i = 0; i < 3; ++i)
        {
            mesh.boundsMin[(size_t) i] = f32 (head->data, (size_t) i * 4);
            mesh.boundsMax[(size_t) i] = f32 (head->data, 12 + (size_t) i * 4);
            if (! finite ({ mesh.boundsMin[(size_t) i], mesh.boundsMax[(size_t) i] }) || mesh.boundsMin[(size_t) i] > mesh.boundsMax[(size_t) i])
                return error = "bad bounds", std::nullopt;
        }
        const auto partCount = u32 (head->data, 24), materialCount = u32 (head->data, 28);
        const auto vertexCount = u32 (head->data, 32), indexCount = u32 (head->data, 36);
        if (materialCount == 0 || materialCount > 255 || partCount > 4096)
            return error = "bad counts", std::nullopt;

        constexpr std::size_t materialSize = 72;
        if (matl->data.size() != materialCount * materialSize)
            return error = "MATL size does not match its count", std::nullopt;
        for (std::uint32_t i = 0; i < materialCount; ++i)
        {
            const auto s = matl->data.subspan (i * materialSize, materialSize);
            HmeshMaterial m;
            const char* name = reinterpret_cast<const char*> (s.data());
            m.name.assign (name, strnlen (name, 16));
            float v[13];
            for (int k = 0; k < 13; ++k)
            {
                v[k] = f32 (s, 16 + (size_t) k * 4);
                if (! std::isfinite (v[k]))
                    return error = "non-finite material value", std::nullopt;
            }
            m.base = { v[0], v[1], v[2] };
            m.roughness = v[3];
            m.metallic = v[4];
            m.clearcoat = v[5];
            m.clearcoatRoughness = v[6];
            m.transmission = v[7];
            m.thicknessMm = v[8];
            m.emissive = { v[9], v[10], v[11] };
            m.grain = v[12];
            m.flags = u32 (s, 68);
            mesh.materials.push_back (m);
        }

        if (vert->data.size() != (std::size_t) vertexCount * sizeof (HmeshVertex))
            return error = "VERT size does not match its count", std::nullopt;
        if (indx->data.size() != (std::size_t) indexCount * 4)
            return error = "INDX size does not match its count", std::nullopt;
        mesh.vertices.resize (vertexCount);
        if (vertexCount > 0)
            std::memcpy (mesh.vertices.data(), vert->data.data(), vert->data.size()); // little-endian host
        for (const auto& v : mesh.vertices)
        {
            if (! finite ({ v.position[0], v.position[1], v.position[2] }))
                return error = "non-finite vertex", std::nullopt;
            if (v.material >= materialCount)
                return error = "vertex material out of range", std::nullopt;
            if (v.slot >= maxSlots)
                return error = "vertex slot out of range", std::nullopt;
        }
        mesh.indices.resize (indexCount);
        for (std::uint32_t i = 0; i < indexCount; ++i)
            mesh.indices[i] = u32 (indx->data, (std::size_t) i * 4);

        std::size_t at = 0;
        const auto& p = part->data;
        constexpr std::size_t fixed = 76;
        for (std::uint32_t i = 0; i < partCount; ++i)
        {
            if (at + 2 > p.size())
                return error = "PART truncated", std::nullopt;
            const auto nameLength = u16 (p, at);
            at += 2;
            if (at + nameLength > p.size())
                return error = "PART truncated", std::nullopt;
            HmeshPart part;
            part.name.assign (reinterpret_cast<const char*> (p.data() + at), nameLength);
            at += nameLength;
            at = (at + 3) & ~std::size_t (3);
            if (at + fixed > p.size())
                return error = "PART truncated", std::nullopt;
            part.kind = (std::uint8_t) p[at];
            part.flags = (std::uint8_t) p[at + 1];
            part.slot = (std::uint8_t) p[at + 2];
            part.material = (std::uint8_t) p[at + 3];
            float v[13];
            for (int k = 0; k < 13; ++k)
            {
                v[k] = f32 (p, at + 4 + (size_t) k * 4);
                if (! std::isfinite (v[k]))
                    return error = "non-finite part value in " + part.name, std::nullopt;
            }
            part.pivot = { v[0], v[1], v[2] };
            part.axis = { v[3], v[4], v[5] };
            part.travel = v[6];
            for (int k = 0; k < 6; ++k)
                part.bounds[(size_t) k] = v[7 + k];
            part.firstIndex = u32 (p, at + 56);
            part.indexCount = u32 (p, at + 60);
            part.baseVertex = u32 (p, at + 64);
            part.vertexCount = u32 (p, at + 68);
            part.ledFor = u16 (p, at + 72);
            at += fixed;

            if (part.kind > HmeshPart::plate || part.slot >= maxSlots || part.material >= materialCount)
                return error = "part field out of range in " + part.name, std::nullopt;
            if (part.ledFor != 0xFFFF && part.ledFor >= 16)
                return error = "ledFor out of range in " + part.name, std::nullopt;
            if (part.bounds[0] > part.bounds[3] || part.bounds[1] > part.bounds[4] || part.bounds[2] > part.bounds[5])
                return error = "inverted part bounds in " + part.name, std::nullopt;
            if ((std::uint64_t) part.firstIndex + part.indexCount > indexCount
                || (std::uint64_t) part.baseVertex + part.vertexCount > vertexCount)
                return error = "part range overruns its buffers: " + part.name, std::nullopt;
            for (std::uint32_t k = 0; k < part.indexCount; ++k)
            {
                const auto index = mesh.indices[part.firstIndex + k];
                if (index < part.baseVertex || index >= part.baseVertex + part.vertexCount)
                    return error = "index outside its part: " + part.name, std::nullopt;
            }
            mesh.parts.push_back (std::move (part));
        }
        // Every index must belong to some part's range (the single draw uses them all).
        for (auto index : mesh.indices)
            if (index >= vertexCount)
                return error = "index out of range", std::nullopt;
        return mesh;
    }
}
