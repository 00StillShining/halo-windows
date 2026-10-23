#pragma once

#include "Core/HmeshFormat.h"

#include <juce_opengl/juce_opengl.h>

#include <string>

namespace halo
{
    // The EP-40 model on the GPU (plan D8): the parsed .hmesh (Core/HmeshFormat)
    // as one vertex buffer, one index buffer and one VAO, so the whole device is
    // a single draw; moving parts follow per-slot transforms in the vertex
    // shader. Materials live in a small RGBA32F texture, four texels per row:
    //   0: base colour (linear), roughness
    //   1: emissive (linear), metallic
    //   2: flags, grain, clearcoat, transmission
    //   3: reserved
    // Vertex attributes (bind in this order): aPos, aNormal (octahedral,
    // snorm16), aUV (unorm16), aBytes (material, slot, mask, ao as floats).
    struct HaloMesh
    {
        Hmesh model;
        GLuint vao = 0, vbo = 0, ebo = 0, materials = 0;

        static constexpr const char* attributes[] { "aPos", "aNormal", "aUV", "aBytes" };

        bool parse (const void* data, size_t size, std::string& error);
        bool empty() const noexcept { return model.indices.empty(); }

        void upload();  // GL thread
        void release(); // GL thread
        // The compiler puts glass parts last (tools/model/build_ep40.py), so the
        // opaque parts are one range and the glass another.
        std::uint32_t opaqueIndexCount = 0;
        void draw() const;      // every opaque part, one call (glass is drawn apart)
        void drawGlass() const; // the glass parts, one call
        bool hasGlass() const noexcept { return opaqueIndexCount < model.indices.size(); }
    };
}
