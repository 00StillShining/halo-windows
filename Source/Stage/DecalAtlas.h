#pragma once

#include <juce_opengl/juce_opengl.h>

#include <string>

namespace halo
{
    // An ink SDF atlas (.htex RGBA8 or RG8 with compiler-built mips; plan section
    // 4.5) on the GPU: the keys' legends (ep40_keys_sdf.htex, four inks) and the
    // deck's silkscreen (ep40_face_sdf.htex, green and orange).
    // Uploaded raw, in stored channel order, with no premultiply.
    struct DecalAtlas
    {
        GLuint texture = 0;
        int width = 0, height = 0;

        bool upload (const void* data, size_t size, std::string& error); // GL thread
        void release();                                                  // GL thread
        bool valid() const noexcept { return texture != 0; }
    };
}
