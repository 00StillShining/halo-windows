#include "Stage/DecalAtlas.h"

#include "Core/HtexFormat.h"

#include <span>

using namespace juce::gl;

namespace halo
{
    bool DecalAtlas::upload (const void* data, size_t size, std::string& error)
    {
        release();
        const auto htex = Htex::parse (std::span<const std::byte> (static_cast<const std::byte*> (data), size), error);
        if (! htex)
            return false;
        const bool rg = htex->format == Htex::Format::rg8;
        if ((htex->format != Htex::Format::rgba8 && ! rg) || (htex->flags & Htex::sdf) == 0)
        {
            error = "not an RGBA8 or RG8 SDF atlas";
            return false;
        }

        glGenTextures (1, &texture);
        glBindTexture (GL_TEXTURE_2D, texture);
        glPixelStorei (GL_UNPACK_ALIGNMENT, 1);
        for (size_t level = 0; level < htex->mips.size(); ++level)
            glTexImage2D (GL_TEXTURE_2D, (GLint) level, rg ? GL_RG8 : GL_RGBA8, (GLsizei) htex->mipWidth (level),
                          (GLsizei) htex->mipHeight (level), 0, rg ? GL_RG : GL_RGBA, GL_UNSIGNED_BYTE, htex->mips[level].data());
        glPixelStorei (GL_UNPACK_ALIGNMENT, 4);
        glTexParameteri (GL_TEXTURE_2D, GL_TEXTURE_MAX_LEVEL, (GLint) htex->mips.size() - 1);
        glTexParameteri (GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, htex->mips.size() > 1 ? GL_LINEAR_MIPMAP_LINEAR : GL_LINEAR);
        glTexParameteri (GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        glTexParameteri (GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glTexParameteri (GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);

        constexpr GLenum maxAnisotropy = 0x84FF, textureMaxAnisotropy = 0x84FE; // EXT/ARB_texture_filter_anisotropic
        if (juce::OpenGLHelpers::isExtensionSupported ("GL_EXT_texture_filter_anisotropic")
            || juce::OpenGLHelpers::isExtensionSupported ("GL_ARB_texture_filter_anisotropic"))
        {
            GLfloat limit = 1.0f;
            glGetFloatv (maxAnisotropy, &limit);
            glTexParameterf (GL_TEXTURE_2D, textureMaxAnisotropy, std::min (8.0f, limit));
        }
        glBindTexture (GL_TEXTURE_2D, 0);
        width = (int) htex->width;
        height = (int) htex->height;
        return true;
    }

    void DecalAtlas::release()
    {
        if (texture != 0)
            glDeleteTextures (1, &texture);
        texture = 0;
        width = height = 0;
    }
}
