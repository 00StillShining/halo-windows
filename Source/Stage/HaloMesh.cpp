#include "Stage/HaloMesh.h"

using namespace juce::gl;

namespace halo
{
    HaloMesh HaloMesh::parse (const void* data, size_t size)
    {
        juce::MemoryInputStream in (data, size, false);
        HaloMesh mesh;

        char magic[4] {};
        if (in.read (magic, 4) != 4 || std::memcmp (magic, "HMSH", 4) != 0 || in.readInt() != 1)
        {
            DBG ("EP40.hmesh: bad header");
            return {};
        }

        const int partCount = in.readInt();
        for (int p = 0; p < partCount && ! in.isExhausted(); ++p)
        {
            MeshPart part;
            const auto nameLength = (int) (juce::uint16) in.readShort();
            juce::MemoryBlock nameBytes;
            in.readIntoMemoryBlock (nameBytes, nameLength);
            part.name = nameBytes.toString().toStdString();

            float m[16];
            for (auto& v : m)
                v = in.readFloat();
            part.partToModel = juce::Matrix3D<float> (m);

            part.pivot.x = in.readFloat();
            part.pivot.y = in.readFloat();
            part.pivot.z = in.readFloat();

            const auto vertexCount = (size_t) in.readInt();
            part.vertices.resize (vertexCount * MeshPart::floatsPerVertex);
            in.read (part.vertices.data(), (int) (part.vertices.size() * sizeof (float)));

            const auto indexCount = (size_t) in.readInt();
            part.indices.resize (indexCount);
            in.read (part.indices.data(), (int) (part.indices.size() * sizeof (juce::uint32)));

            mesh.parts.push_back (std::move (part));
        }

        if ((int) mesh.parts.size() != partCount)
        {
            DBG ("EP40.hmesh: truncated");
            return {};
        }
        return mesh;
    }

    MeshPart* HaloMesh::find (std::string_view name)
    {
        for (auto& part : parts)
            if (part.name == name)
                return &part;
        return nullptr;
    }

    void HaloMesh::upload()
    {
        constexpr auto stride = (GLsizei) (MeshPart::floatsPerVertex * sizeof (float));
        auto offset = [] (int floats) { return (const void*) (size_t) (floats * sizeof (float)); };

        for (auto& part : parts)
        {
            glGenVertexArrays (1, &part.vao);
            glBindVertexArray (part.vao);

            glGenBuffers (1, &part.vbo);
            glBindBuffer (GL_ARRAY_BUFFER, part.vbo);
            glBufferData (GL_ARRAY_BUFFER, (GLsizeiptr) (part.vertices.size() * sizeof (float)),
                          part.vertices.data(), GL_STATIC_DRAW);

            glGenBuffers (1, &part.ebo);
            glBindBuffer (GL_ELEMENT_ARRAY_BUFFER, part.ebo);
            glBufferData (GL_ELEMENT_ARRAY_BUFFER, (GLsizeiptr) (part.indices.size() * sizeof (juce::uint32)),
                          part.indices.data(), GL_STATIC_DRAW);

            glEnableVertexAttribArray (0); glVertexAttribPointer (0, 3, GL_FLOAT, GL_FALSE, stride, offset (0));  // position
            glEnableVertexAttribArray (1); glVertexAttribPointer (1, 3, GL_FLOAT, GL_FALSE, stride, offset (3));  // normal
            glEnableVertexAttribArray (2); glVertexAttribPointer (2, 3, GL_FLOAT, GL_FALSE, stride, offset (6));  // colour
            glEnableVertexAttribArray (3); glVertexAttribPointer (3, 1, GL_FLOAT, GL_FALSE, stride, offset (9));  // emissive
            glEnableVertexAttribArray (4); glVertexAttribPointer (4, 2, GL_FLOAT, GL_FALSE, stride, offset (10)); // uv
        }
        glBindVertexArray (0);
    }

    void HaloMesh::release()
    {
        for (auto& part : parts)
        {
            if (part.ebo != 0) glDeleteBuffers (1, &part.ebo);
            if (part.vbo != 0) glDeleteBuffers (1, &part.vbo);
            if (part.vao != 0) glDeleteVertexArrays (1, &part.vao);
            part.vao = part.vbo = part.ebo = 0;
        }
    }
}
