#include "Stage/RenderTargets.h"

using namespace juce::gl;

namespace halo
{
    namespace
    {
        GLuint texture2D (GLint internalFormat, GLenum format, GLenum type, int w, int h)
        {
            GLuint id = 0;
            glGenTextures (1, &id);
            glBindTexture (GL_TEXTURE_2D, id);
            glTexImage2D (GL_TEXTURE_2D, 0, internalFormat, w, h, 0, format, type, nullptr);
            glTexParameteri (GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
            glTexParameteri (GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
            glTexParameteri (GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
            glTexParameteri (GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
            return id;
        }

        GLuint textureMS (GLint internalFormat, int samples, int w, int h)
        {
            GLuint id = 0;
            glGenTextures (1, &id);
            glBindTexture (GL_TEXTURE_2D_MULTISAMPLE, id);
            glTexImage2DMultisample (GL_TEXTURE_2D_MULTISAMPLE, samples, (GLenum) internalFormat, w, h, GL_TRUE);
            return id;
        }

        bool complete (const char* what, StageLog& log)
        {
            const auto status = glCheckFramebufferStatus (GL_FRAMEBUFFER);
            if (status == GL_FRAMEBUFFER_COMPLETE)
                return true;
            log.line (juce::String ("targets: ") + what + " incomplete (0x" + juce::String::toHexString ((int) status) + ")");
            return false;
        }
    }

    bool RenderTargets::create (bool wantHdr, int wantedSamples, StageLog& log)
    {
        hdr = wantHdr;
        wanted = wantedSamples;
        if (hdr)
        {
            GLint maxColour = 1, maxDepth = 1;
            glGetIntegerv (GL_MAX_COLOR_TEXTURE_SAMPLES, &maxColour);
            glGetIntegerv (GL_MAX_SAMPLES, &maxDepth);
            wanted = juce::jlimit (1, juce::jmin ((int) maxColour, (int) maxDepth), wantedSamples);
        }
        return allocate (64, 64, log);
    }

    void RenderTargets::destroy()
    {
        GLuint fbos[] { msFbo, resolveFbo, sceneFbo, postFbo };
        GLuint textures[] { msDevice, msFloor, device, floor, scene, post };
        GLuint renderbuffers[] { msDepth, sceneDepth };
        for (auto f : fbos) if (f != 0) glDeleteFramebuffers (1, &f);
        for (auto t : textures) if (t != 0) glDeleteTextures (1, &t);
        for (auto r : renderbuffers) if (r != 0) glDeleteRenderbuffers (1, &r);
        msFbo = msDevice = msFloor = msDepth = resolveFbo = device = floor = 0;
        sceneFbo = scene = sceneDepth = postFbo = post = 0;
    }

    void RenderTargets::release()
    {
        destroy();
        width = height = 0;
    }

    bool RenderTargets::allocate (int w, int h, StageLog& log)
    {
        GLint previous = 0;
        glGetIntegerv (GL_FRAMEBUFFER_BINDING, &previous);
        destroy();
        bool ok = true;

        if (hdr)
        {
            msDevice = textureMS (GL_RGBA16F, wanted, w, h);
            msFloor = textureMS (GL_RGBA16F, wanted, w, h);
            glGenRenderbuffers (1, &msDepth);
            glBindRenderbuffer (GL_RENDERBUFFER, msDepth);
            glRenderbufferStorageMultisample (GL_RENDERBUFFER, wanted, GL_DEPTH_COMPONENT24, w, h);
            glGenFramebuffers (1, &msFbo);
            glBindFramebuffer (GL_FRAMEBUFFER, msFbo);
            glFramebufferTexture2D (GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D_MULTISAMPLE, msDevice, 0);
            glFramebufferTexture2D (GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT1, GL_TEXTURE_2D_MULTISAMPLE, msFloor, 0);
            glFramebufferRenderbuffer (GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_RENDERBUFFER, msDepth);
            const GLenum both[] { GL_COLOR_ATTACHMENT0, GL_COLOR_ATTACHMENT1 };
            glDrawBuffers (2, both);
            ok = complete ("HDR MSAA scene", log) && ok;
            GLint granted = 0;
            glGetIntegerv (GL_SAMPLES, &granted);
            grantedSamples = (int) granted;

            device = texture2D (GL_RGBA16F, GL_RGBA, GL_FLOAT, w, h);
            floor = texture2D (GL_RGBA16F, GL_RGBA, GL_FLOAT, w, h);
            glGenFramebuffers (1, &resolveFbo);
            glBindFramebuffer (GL_FRAMEBUFFER, resolveFbo);
            glFramebufferTexture2D (GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, device, 0);
            glFramebufferTexture2D (GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT1, GL_TEXTURE_2D, floor, 0);
            glDrawBuffers (2, both);
            ok = complete ("HDR resolve", log) && ok;
        }
        else
        {
            scene = texture2D (GL_RGBA8, GL_RGBA, GL_UNSIGNED_BYTE, w, h);
            glGenRenderbuffers (1, &sceneDepth);
            glBindRenderbuffer (GL_RENDERBUFFER, sceneDepth);
            glRenderbufferStorage (GL_RENDERBUFFER, GL_DEPTH_COMPONENT24, w, h);
            glGenFramebuffers (1, &sceneFbo);
            glBindFramebuffer (GL_FRAMEBUFFER, sceneFbo);
            glFramebufferTexture2D (GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, scene, 0);
            glFramebufferRenderbuffer (GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_RENDERBUFFER, sceneDepth);
            ok = complete ("LDR scene", log) && ok;
            grantedSamples = 1;
        }

        post = texture2D (GL_RGBA8, GL_RGBA, GL_UNSIGNED_BYTE, w, h);
        glGenFramebuffers (1, &postFbo);
        glBindFramebuffer (GL_FRAMEBUFFER, postFbo);
        glFramebufferTexture2D (GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, post, 0);
        ok = complete ("post", log) && ok;

        glBindFramebuffer (GL_FRAMEBUFFER, (GLuint) previous);
        glBindTexture (GL_TEXTURE_2D, 0);
        width = w;
        height = h;
        ++allocations;
        log.line ("targets: " + juce::String (hdr ? "HDR " : "LDR ") + juce::String (w) + "x" + juce::String (h)
                  + " GL_SAMPLES=" + juce::String (grantedSamples) + (ok ? "" : " INCOMPLETE")
                  + " (allocation " + juce::String (allocations) + ")");
        return ok;
    }

    RenderTargets::Frame RenderTargets::prepare (int w, int h, double now, StageLog& log)
    {
        Frame f;
        if (w <= 0 || h <= 0)
            return f;

        if (w > width || h > height)
        {
            // The first real size is allocated at once; later growth waits for
            // the size to settle.
            const bool first = allocations <= 1;
            if (w != pendingW || h != pendingH)
            {
                pendingW = w;
                pendingH = h;
                pendingSince = now;
            }
            if (first || now - pendingSince >= 0.2)
                allocate (juce::jmax (w, first ? 0 : width), juce::jmax (h, first ? 0 : height), log);
        }
        else
        {
            pendingW = pendingH = 0;
        }

        // Render at full size if it fits, otherwise scaled down to fit.
        const float s = juce::jmin (1.0f, (float) width / (float) w, (float) height / (float) h);
        f.width = juce::jmax (1, (int) std::floor ((float) w * s));
        f.height = juce::jmax (1, (int) std::floor ((float) h * s));
        f.uvScaleX = (float) f.width / (float) width;
        f.uvScaleY = (float) f.height / (float) height;
        return f;
    }
}
