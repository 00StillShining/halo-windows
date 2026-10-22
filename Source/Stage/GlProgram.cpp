#include "Stage/GlProgram.h"

using namespace juce::gl;

namespace halo
{
    namespace
    {
        GLuint compile (GLenum type, const juce::String& source, StageLog& log, const juce::String& what)
        {
            const GLuint shader = glCreateShader (type);
            const auto utf8 = source.toStdString();
            const char* text = utf8.c_str();
            glShaderSource (shader, 1, &text, nullptr);
            glCompileShader (shader);
            GLint ok = 0;
            glGetShaderiv (shader, GL_COMPILE_STATUS, &ok);
            if (ok == GL_FALSE)
            {
                GLint length = 0;
                glGetShaderiv (shader, GL_INFO_LOG_LENGTH, &length);
                std::string message ((size_t) juce::jmax (1, length), '\0');
                glGetShaderInfoLog (shader, length, nullptr, message.data());
                log.line ("shader " + what + " failed to compile: " + juce::String (message));
                glDeleteShader (shader);
                return 0;
            }
            return shader;
        }
    }

    bool GlProgram::build (const ShaderLibrary& library, StageLog& log, const char* label, const char* vertexFile,
                           const char* fragmentFile, bool hdr, std::initializer_list<const char*> attributes,
                           std::initializer_list<const char*> outputs)
    {
        release();
        const juce::String name (label);
        const auto vs = compile (GL_VERTEX_SHADER, library.source (vertexFile, hdr), log, name + " (" + vertexFile + ")");
        const auto fs = compile (GL_FRAGMENT_SHADER, library.source (fragmentFile, hdr), log, name + " (" + fragmentFile + ")");
        if (vs == 0 || fs == 0)
        {
            if (vs != 0) glDeleteShader (vs);
            if (fs != 0) glDeleteShader (fs);
            return false;
        }

        const GLuint program = glCreateProgram();
        glAttachShader (program, vs);
        glAttachShader (program, fs);
        GLuint index = 0;
        for (auto* a : attributes)
            glBindAttribLocation (program, index++, a);
        index = 0;
        for (auto* o : outputs)
            glBindFragDataLocation (program, index++, o);
        glLinkProgram (program);
        glDeleteShader (vs);
        glDeleteShader (fs);

        GLint ok = 0;
        glGetProgramiv (program, GL_LINK_STATUS, &ok);
        if (ok == GL_FALSE)
        {
            GLint length = 0;
            glGetProgramiv (program, GL_INFO_LOG_LENGTH, &length);
            std::string message ((size_t) juce::jmax (1, length), '\0');
            glGetProgramInfoLog (program, length, nullptr, message.data());
            log.line ("shader " + name + " failed to link: " + juce::String (message));
            glDeleteProgram (program);
            return false;
        }
        id = program;
        locations.clear();
        return true;
    }

    void GlProgram::release()
    {
        if (id != 0)
            glDeleteProgram (id);
        id = 0;
        locations.clear();
    }

    void GlProgram::use() const
    {
        glUseProgram (id);
    }

    GLint GlProgram::location (const char* name)
    {
        if (auto it = locations.find (name); it != locations.end())
            return it->second;
        const GLint l = glGetUniformLocation (id, name);
        if (lookupCounter != nullptr)
            ++*lookupCounter;
        locations.emplace (name, l);
        return l;
    }
}
