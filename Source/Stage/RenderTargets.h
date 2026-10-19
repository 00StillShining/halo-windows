#pragma once

#include "Stage/StageLog.h"

#include <juce_opengl/juce_opengl.h>

namespace halo
{
    // The stage's offscreen targets (plan section 3.2).
    //
    // High tier:
    //   ms*       4x MSAA scene: RGBA16F device radiance (premultiplied, a =
    //             coverage), RGBA16F floor terms (a = contact shadow), depth
    //   resolved  single-sample RGBA16F device + floor, after the custom resolve
    //   post      RGBA8 display image, only when FXAA follows the composite
    // Low tier:
    //   scene     RGBA8 + depth, the forward-rendered frame
    //   post      FXAA reads the scene directly; post is unused
    //
    // Targets only ever grow. A larger stage is reallocated once its size has
    // been steady for 200 ms; until then the frame renders at reduced scale into
    // the targets it has (and the composite scales it up), so dragging a window
    // edge costs one reallocation, not one per frame.
    class RenderTargets
    {
    public:
        struct Frame
        {
            int width = 0, height = 0;          // render size this frame
            float uvScaleX = 1.0f, uvScaleY = 1.0f;
        };

        // Allocates small targets once to prove the formats work. Returns false
        // (and logs why) if the framebuffer is incomplete.
        bool create (bool hdr, int wantedSamples, StageLog&);
        void release();

        Frame prepare (int outputWidth, int outputHeight, double nowSeconds, StageLog&);

        bool isHdr() const noexcept { return hdr; }
        int samples() const noexcept { return grantedSamples; }
        int allocatedWidth() const noexcept { return width; }
        int allocatedHeight() const noexcept { return height; }
        int reallocations() const noexcept { return allocations; }

        GLuint msFbo = 0, msDevice = 0, msFloor = 0, msDepth = 0;
        GLuint resolveFbo = 0, device = 0, floor = 0;
        GLuint sceneFbo = 0, scene = 0, sceneDepth = 0;
        GLuint postFbo = 0, post = 0;

    private:
        bool allocate (int w, int h, StageLog&);
        void destroy();

        bool hdr = true;
        int wanted = 4, grantedSamples = 0;
        int width = 0, height = 0, allocations = 0;
        int pendingW = 0, pendingH = 0;
        double pendingSince = 0.0;
    };
}
