#pragma once

#include "Stage/ShaderLibrary.h"
#include "Stage/StageLog.h"

#include <juce_opengl/juce_opengl.h>

#include <string>
#include <unordered_map>

namespace halo
{
    // A linked GLSL program with cached uniform locations: after the first
    // frame the stage makes no glGetUniformLocation calls at all (plan M2).
    // GL thread only.
    class GlProgram
    {
    public:
        GlProgram() = default;
        GlProgram (const GlProgram&) = delete;
        GlProgram& operator= (const GlProgram&) = delete;
        ~GlProgram() { jassert (id == 0); } // release() on the GL thread first

        // Attributes are bound to locations 0, 1, 2... in order; fragment
        // outputs likewise. Logs and returns false on any compile or link error.
        bool build (const ShaderLibrary&, StageLog&, const char* label, const char* vertexFile, const char* fragmentFile,
                    bool hdr, std::initializer_list<const char*> attributes, std::initializer_list<const char*> outputs);
        void release();

        bool valid() const noexcept { return id != 0; }
        void use() const;

        GLint location (const char* name);
        void set (const char* name, int v)                     { if (auto l = location (name); l >= 0) juce::gl::glUniform1i (l, v); }
        void set (const char* name, float v)                   { if (auto l = location (name); l >= 0) juce::gl::glUniform1f (l, v); }
        void set (const char* name, float x, float y)          { if (auto l = location (name); l >= 0) juce::gl::glUniform2f (l, x, y); }
        void set (const char* name, float x, float y, float z) { if (auto l = location (name); l >= 0) juce::gl::glUniform3f (l, x, y, z); }
        void set (const char* name, float x, float y, float z, float w) { if (auto l = location (name); l >= 0) juce::gl::glUniform4f (l, x, y, z, w); }
        void setMat4 (const char* name, const float* m)        { if (auto l = location (name); l >= 0) juce::gl::glUniformMatrix4fv (l, 1, juce::gl::GL_FALSE, m); }

        int* lookupCounter = nullptr; // bumped on every real glGetUniformLocation

    private:
        GLuint id = 0;
        std::unordered_map<std::string, GLint> locations;
    };
}
