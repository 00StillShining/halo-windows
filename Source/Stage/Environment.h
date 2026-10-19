#pragma once

#include "Core/HenvFormat.h"
#include "Stage/StageLog.h"

#include <juce_graphics/juce_graphics.h>
#include <juce_opengl/juce_opengl.h>

#include <array>

namespace halo
{
    // The studio the device is lit by (plan section 3.3 and 3.4), on the GL side:
    //   - the GGX-prefiltered cube map from assets/stage/studio.henv (RGB9E5,
    //     6 mips, seamless filtering), sampled by roughness for reflections;
    //   - a 64x64 RG16F split-sum lookup table, generated once at startup;
    //   - SH9 irradiance: the baked ambient studio plus a floor-bounce lobe
    //     built from the look's floor colour on every setLook (no rebake);
    //   - the key and rim as analytic lights, so the key can be shadowed and the
    //     shadow map lines up with what the reflections show.
    // GL thread only.
    class Environment
    {
    public:
        bool create (StageLog&);
        void release();

        // A lower-hemisphere lobe of linear(floor) x 0.18 added to the irradiance.
        void setFloor (juce::Colour floorToken);

        GLuint cubeMap() const noexcept { return cube; }
        GLuint brdfLut() const noexcept { return lut; }
        float maxMip() const noexcept { return (float) mips - 1.0f; }
        const std::array<std::array<float, 3>, 9>& irradianceSH() const noexcept { return sh; }
        const HenvLight& key() const noexcept { return keyLight; }
        const HenvLight& rim() const noexcept { return rimLight; }
        bool valid() const noexcept { return cube != 0 && lut != 0; }

        static constexpr float floorBounce = 0.18f;

    private:
        GLuint cube = 0, lut = 0;
        int mips = 1;
        std::array<std::array<float, 3>, 9> baseSH {}, sh {};
        HenvLight keyLight, rimLight;
    };
}
