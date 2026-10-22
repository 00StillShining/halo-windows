#pragma once

#include "Core/ChunkFile.h"

#include <array>
#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <vector>

// .hmesh: the stage's EP-40 model (docs/redesign/riddim-3d-plan.md section 5;
// the v3 layout is documented in tools/model/hmesh_io.py).
//
// Reads v3 (the chunked format: one model-space vertex and index buffer, a
// material table, per-part kinds, flags and slots) and the legacy v1 and v2
// part lists, which load with neutral defaults: one material per unique
// colour (v1: roughness 0.5), slots for pad_* and group_* by name, bounds
// without halo_ring, AO 255. Every count and index is checked; nothing is
// trusted. No JUCE: HaloTests covers it.
namespace halo
{
    struct HmeshMaterial
    {
        enum Flags : std::uint32_t { glass = 1, lcd = 2, led = 4, apertureEmitter = 8, decalTarget = 16, edgeEmitter = 32 };

        std::string name;
        std::array<float, 3> base {};      // linear
        float roughness = 0.5f, metallic = 0.0f;
        float clearcoat = 0.0f, clearcoatRoughness = 0.0f, transmission = 0.0f, thicknessMm = 0.0f;
        std::array<float, 3> emissive {};  // linear
        float grain = 0.0f;
        std::uint32_t flags = 0;
    };

    struct HmeshPart
    {
        enum Kind : std::uint8_t { staticPart, pad, group, button, knob, fader, led, display, glass, anchor, speakerCone, plate };
        enum Flags : std::uint8_t { castsShadow = 1, moving = 2, emissive = 4, transparent = 8, decal = 16 };

        std::string name;
        std::uint8_t kind = staticPart, flags = 0, slot = 0, material = 0;
        std::array<float, 3> pivot {}, axis { 0.0f, 0.0f, -1.0f };
        float travel = 0.0f;
        std::array<float, 6> bounds {};    // min xyz, max xyz (model)
        std::uint32_t firstIndex = 0, indexCount = 0, baseVertex = 0, vertexCount = 0;
        std::uint16_t ledFor = 0xFFFF;
    };

    // The GPU vertex, 24 bytes, exactly as uploaded.
    struct HmeshVertex
    {
        float position[3];        // model space, rest pose, metres
        std::int16_t normal[2];   // octahedral, snorm16
        std::uint16_t uv[2];      // unorm16
        std::uint8_t material, slot, mask, ao;
    };
    static_assert (sizeof (HmeshVertex) == 24);

    struct Hmesh
    {
        static constexpr int maxSlots = 32;

        std::uint32_t version = 0;
        std::array<float, 3> boundsMin {}, boundsMax {}; // shadow-casting parts; boundsMin[2] = ground contact
        std::vector<HmeshMaterial> materials;
        std::vector<HmeshPart> parts;
        std::vector<HmeshVertex> vertices;
        std::vector<std::uint32_t> indices;  // absolute

        const HmeshPart* find (std::string_view name) const;
        int indexOf (std::string_view name) const;

        static std::optional<Hmesh> parse (std::span<const std::byte>, std::string& error);

        // Octahedral normal packing shared with tools/model/hmesh_io.py.
        static std::array<std::int16_t, 2> encodeNormal (float x, float y, float z);
        static std::array<float, 3> decodeNormal (std::int16_t x, std::int16_t y);
    };
}
