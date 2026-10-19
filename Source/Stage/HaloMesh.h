#pragma once

#include <juce_opengl/juce_opengl.h>

#include <string>
#include <vector>

namespace halo
{
    // One named part of the EP-40 model (pad_7, group_a, display_surface ...),
    // loaded from the .hmesh file baked by tools/model/usdz_to_hmesh.py.
    struct MeshPart
    {
        static constexpr int floatsPerVertex = 12; // pos3 normal3 colour3 emissive uv2

        std::string name;
        juce::Matrix3D<float> partToModel;
        juce::Vector3D<float> pivot;
        std::vector<float> vertices;
        std::vector<juce::uint32> indices;

        // GPU objects, created on the GL thread.
        GLuint vao = 0, vbo = 0, ebo = 0;
    };

    struct HaloMesh
    {
        std::vector<MeshPart> parts;

        // Returns an empty mesh (and logs) if the data is malformed.
        static HaloMesh parse (const void* data, size_t size);

        MeshPart* find (std::string_view name);

        void upload();  // GL thread
        void release(); // GL thread
    };
}
