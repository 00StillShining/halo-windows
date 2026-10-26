#include "Stage/HaloMesh.h"

using namespace juce::gl;

namespace halo
{
    bool HaloMesh::parse (const void* data, size_t size, std::string& error)
    {
        auto parsed = Hmesh::parse (std::span<const std::byte> (static_cast<const std::byte*> (data), size), error);
        if (! parsed)
            return false;
        model = std::move (*parsed);
        // Glass must be the tail of the index buffer; anything else draws opaque.
        opaqueIndexCount = (std::uint32_t) model.indices.size();
        std::uint32_t glassStart = opaqueIndexCount;
        for (const auto& part : model.parts)
            if (part.kind == HmeshPart::glass && part.indexCount > 0)
                glassStart = std::min (glassStart, part.firstIndex);
        bool tail = true;
        for (const auto& part : model.parts)
            if (part.kind != HmeshPart::glass && part.indexCount > 0 && part.firstIndex + part.indexCount > glassStart)
                tail = false;
        if (tail)
            opaqueIndexCount = glassStart;
        return true;
    }

    void HaloMesh::upload (const DeviceColourway& colourway)
    {
        release();
        glGenVertexArrays (1, &vao);
        glBindVertexArray (vao);

        glGenBuffers (1, &vbo);
        glBindBuffer (GL_ARRAY_BUFFER, vbo);
        glBufferData (GL_ARRAY_BUFFER, (GLsizeiptr) (model.vertices.size() * sizeof (HmeshVertex)),
                      model.vertices.data(), GL_STATIC_DRAW);
        glGenBuffers (1, &ebo);
        glBindBuffer (GL_ELEMENT_ARRAY_BUFFER, ebo);
        glBufferData (GL_ELEMENT_ARRAY_BUFFER, (GLsizeiptr) (model.indices.size() * sizeof (std::uint32_t)),
                      model.indices.data(), GL_STATIC_DRAW);

        constexpr auto stride = (GLsizei) sizeof (HmeshVertex);
        auto at = [] (size_t offset) { return (const void*) offset; };
        glEnableVertexAttribArray (0);
        glVertexAttribPointer (0, 3, GL_FLOAT, GL_FALSE, stride, at (offsetof (HmeshVertex, position)));
        glEnableVertexAttribArray (1);
        glVertexAttribPointer (1, 2, GL_SHORT, GL_TRUE, stride, at (offsetof (HmeshVertex, normal)));
        glEnableVertexAttribArray (2);
        glVertexAttribPointer (2, 2, GL_UNSIGNED_SHORT, GL_TRUE, stride, at (offsetof (HmeshVertex, uv)));
        // Bytes as plain floats (no integer attributes; the shader rounds).
        glEnableVertexAttribArray (3);
        glVertexAttribPointer (3, 4, GL_UNSIGNED_BYTE, GL_FALSE, stride, at (offsetof (HmeshVertex, material)));
        glBindVertexArray (0);

        uploadMaterials (colourway);
    }

    void HaloMesh::uploadMaterials (const DeviceColourway& colourway)
    {
        auto decode = [] (float c) { return c <= 0.04045f ? c / 12.92f : std::pow ((c + 0.055f) / 1.055f, 2.4f); };
        std::vector<float> table (model.materials.size() * 16, 0.0f);
        for (size_t i = 0; i < model.materials.size(); ++i)
        {
            const auto& m = model.materials[i];
            float* t = table.data() + i * 16;
            t[0] = m.base[0]; t[1] = m.base[1]; t[2] = m.base[2]; t[3] = m.roughness;
            if (const auto o = colourway.lookup (m.name))
            {
                t[0] = decode (o->colour.getFloatRed());
                t[1] = decode (o->colour.getFloatGreen());
                t[2] = decode (o->colour.getFloatBlue());
                if (o->roughness >= 0.0f)
                    t[3] = o->roughness;
            }
            t[4] = m.emissive[0]; t[5] = m.emissive[1]; t[6] = m.emissive[2]; t[7] = m.metallic;
            t[8] = (float) m.flags; t[9] = m.grain; t[10] = m.clearcoat; t[11] = m.transmission;
        }
        if (materials == 0)
            glGenTextures (1, &materials);
        glBindTexture (GL_TEXTURE_2D, materials);
        glTexImage2D (GL_TEXTURE_2D, 0, GL_RGBA32F, 4, (GLsizei) juce::jmax ((size_t) 1, model.materials.size()), 0,
                      GL_RGBA, GL_FLOAT, table.data());
        glTexParameteri (GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
        glTexParameteri (GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
        glBindTexture (GL_TEXTURE_2D, 0);
    }

    void HaloMesh::release()
    {
        if (ebo != 0) glDeleteBuffers (1, &ebo);
        if (vbo != 0) glDeleteBuffers (1, &vbo);
        if (vao != 0) glDeleteVertexArrays (1, &vao);
        if (materials != 0) glDeleteTextures (1, &materials);
        vao = vbo = ebo = materials = 0;
    }

    void HaloMesh::draw() const
    {
        glBindVertexArray (vao);
        glDrawElements (GL_TRIANGLES, (GLsizei) opaqueIndexCount, GL_UNSIGNED_INT, nullptr);
        glBindVertexArray (0);
    }

    void HaloMesh::drawGlass() const
    {
        if (! hasGlass())
            return;
        glBindVertexArray (vao);
        glDrawElements (GL_TRIANGLES, (GLsizei) (model.indices.size() - opaqueIndexCount), GL_UNSIGNED_INT,
                        reinterpret_cast<const void*> ((std::uintptr_t) opaqueIndexCount * sizeof (std::uint32_t)));
        glBindVertexArray (0);
    }
}
