#include "Stage/StageComponent.h"
#include "Core/Ep40MidiMapping.h"
#include "UI/HaloColours.h"

#include <HaloAssets.h>

using namespace juce::gl;

namespace halo
{
    namespace
    {
        // Mechanics from HaloMechanics.swift.
        constexpr float travelMin = 0.0011f, travelMax = 0.0013f; // metres
        constexpr float pressTau = 0.020f, releaseTau = 0.035f;    // ~65 ms in, ~110 ms out
        constexpr float ledFloor = 0.16f, ledScale = 0.74f, ledDecayTau = 0.09f;
        constexpr float ringDiscoverPeriod = 4.0f;

        using Mat = juce::Matrix3D<float>;

        Mat perspective (float fovYDegrees, float aspect, float zNear, float zFar)
        {
            const float f = 1.0f / std::tan (juce::degreesToRadians (fovYDegrees) * 0.5f);
            float m[16] {};
            m[0] = f / aspect;
            m[5] = f;
            m[10] = (zFar + zNear) / (zNear - zFar);
            m[11] = -1.0f;
            m[14] = 2.0f * zFar * zNear / (zNear - zFar);
            return Mat (m);
        }

        Mat lookAt (juce::Vector3D<float> eye, juce::Vector3D<float> target, juce::Vector3D<float> up)
        {
            const auto f = (target - eye).normalised();
            const auto s = (f ^ up).normalised();
            const auto u = s ^ f;
            float m[16] { s.x, u.x, -f.x, 0, s.y, u.y, -f.y, 0, s.z, u.z, -f.z, 0,
                          -(s * eye), -(u * eye), f * eye, 1 };
            return Mat (m);
        }

        Mat translation (float x, float y, float z)
        {
            float m[16] { 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, x, y, z, 1 };
            return Mat (m);
        }

        // The model is authored Z-up (Blender); the scene is Y-up, ports far (-Z).
        Mat modelToWorld()
        {
            float m[16] { 1, 0, 0, 0, 0, 0, -1, 0, 0, 1, 0, 0, 0, 0, 0, 1 };
            return Mat (m);
        }

        // Column-major, column-vector product: returns the transform that applies
        // `first`, then `second` (i.e. second * first in maths notation).
        Mat then (const Mat& first, const Mat& second)
        {
            float m[16];
            for (int col = 0; col < 4; ++col)
                for (int row = 0; row < 4; ++row)
                {
                    float sum = 0.0f;
                    for (int k = 0; k < 4; ++k)
                        sum += second.mat[k * 4 + row] * first.mat[col * 4 + k];
                    m[col * 4 + row] = sum;
                }
            return Mat (m);
        }

        const char* partVertexShader = R"(
            #version 150
            in vec3 aPos; in vec3 aNormal; in vec3 aColour; in float aEmissive; in vec2 aUV;
            uniform mat4 uModel;
            uniform mat4 uViewProj;
            out vec3 vNormal; out vec3 vColour; out float vEmissive; out vec2 vUV;
            out vec3 vLocal; out vec3 vWorld;
            void main()
            {
                vec4 world = uModel * vec4 (aPos, 1.0);
                vWorld = world.xyz;
                vLocal = aPos;
                vNormal = normalize (mat3 (uModel) * aNormal);
                vColour = aColour; vEmissive = aEmissive; vUV = aUV;
                gl_Position = uViewProj * world;
            })";

        const char* partFragmentShader = R"(
            #version 150
            in vec3 vNormal; in vec3 vColour; in float vEmissive; in vec2 vUV;
            in vec3 vLocal; in vec3 vWorld;
            uniform int uKind;            // 0 part, 1 display, 2 halo ring, 3 shadow
            uniform vec3 uEye;
            uniform vec3 uGlow;           // linear emissive added to the part
            uniform sampler2D uTexture;
            uniform int uRingMode;        // 0 dark, 1 discover sweep, 2 steady, 3 error
            uniform float uTime;
            uniform vec3 uRingColour;
            uniform float uRingLevel;
            out vec4 fragColour;

            vec3 toSrgb (vec3 c) { return pow (clamp (c, 0.0, 1.0), vec3 (1.0 / 2.2)); }
            vec3 toLinear (vec3 c) { return pow (c, vec3 (2.2)); }

            void main()
            {
                if (uKind == 3)
                {
                    // Soft rounded-box contact shadow; vUV spans -1..1.
                    vec2 q = abs (vUV) - vec2 (0.78, 0.82);
                    float d = length (max (q, 0.0)) + min (max (q.x, q.y), 0.0);
                    float a = 1.0 - smoothstep (-0.05, 0.22, d);
                    fragColour = vec4 (0.10, 0.13, 0.15, a * 0.28);
                    return;
                }

                vec3 n = normalize (vNormal);
                if (! gl_FrontFacing) n = -n;
                vec3 base = vColour;
                vec3 emissive = uGlow;

                if (uKind == 1)
                {
                    // The display is self-lit: show the texture almost unshaded.
                    vec3 tex = toLinear (texture (uTexture, vUV).rgb);
                    fragColour = vec4 (toSrgb (tex * 0.92 + tex * 0.08 * max (n.y, 0.0)), 1.0);
                    return;
                }

                float ringMix = 0.0;
                if (uKind == 2 && uRingMode != 0)
                {
                    float angle = atan (vLocal.y, vLocal.x);
                    float segment = floor ((angle + 3.14159265) / 6.2831853 * 48.0);
                    float level = uRingLevel;
                    if (uRingMode == 1)
                    {
                        float head = floor (fract (uTime / 4.0) * 48.0);
                        float d = mod (head - segment + 48.0, 48.0);
                        float falloff[6] = float[6] (1.0, 0.72, 0.48, 0.28, 0.14, 0.06);
                        level = d < 6.0 ? falloff[int (d)] * 0.9 : 0.0;
                    }
                    // Segment opacity over the dark ring, like the Mac's 48 unlit segments.
                    ringMix = level;
                }

                vec3 keyDir = normalize (vec3 (-0.45, 1.0, 0.55));
                vec3 fillDir = normalize (vec3 (0.8, 0.35, 0.2));
                float key = max (dot (n, keyDir), 0.0);
                float fill = max (dot (n, fillDir), 0.0);
                float hemi = mix (0.32, 0.62, n.y * 0.5 + 0.5);
                vec3 v = normalize (uEye - vWorld);
                float spec = pow (max (dot (n, normalize (keyDir + v)), 0.0), 48.0) * 0.12;

                vec3 lit = base * (hemi + key * 0.78 + fill * 0.18) + spec + base * vEmissive * 0.6 + emissive;
                lit = mix (lit, uRingColour, ringMix);
                fragColour = vec4 (toSrgb (lit), 1.0);
            })";

        const char* backgroundVertexShader = R"(
            #version 150
            void main()
            {
                vec2 p = vec2 ((gl_VertexID << 1) & 2, gl_VertexID & 2);
                gl_Position = vec4 (p * 2.0 - 1.0, 0.0, 1.0);
            })";

        // The graph-paper canvas (GridCanvas.swift): 16 pt cells, a major line every 4.
        const char* backgroundFragmentShader = R"(
            #version 150
            uniform float uScale;
            uniform float uHeight;
            uniform vec3 uCanvas;
            uniform vec4 uMinor;
            uniform vec4 uMajor;
            out vec4 fragColour;
            float line (float coord, float spacing, float width)
            {
                float d = abs (mod (coord + width * 0.5, spacing) - width * 0.5);
                return 1.0 - smoothstep (width * 0.5, width * 0.5 + 1.0, d);
            }
            void main()
            {
                vec2 p = vec2 (gl_FragCoord.x, uHeight - gl_FragCoord.y);
                float cell = 16.0 * uScale;
                float w = max (1.0, uScale);
                float minor = max (line (p.x, cell, w), line (p.y, cell, w));
                float major = max (line (p.x, cell * 4.0, w), line (p.y, cell * 4.0, w));
                vec3 c = uCanvas;
                c = mix (c, uMinor.rgb, minor * uMinor.a);
                c = mix (c, uMajor.rgb, major * uMajor.a);
                fragColour = vec4 (c, 1.0);
            })";

        juce::Vector3D<float> toVec (juce::Colour c) { return { c.getFloatRed(), c.getFloatGreen(), c.getFloatBlue() }; }

        juce::Vector3D<float> linear (juce::Colour c)
        {
            auto f = [] (float v) { return std::pow (v, 2.2f); };
            return { f (c.getFloatRed()), f (c.getFloatGreen()), f (c.getFloatBlue()) };
        }
    }

    //==============================================================================
    StageComponent::StageComponent (AppModel& m) : model (m)
    {
        setOpaque (true);

        juce::OpenGLPixelFormat format;
        format.multisamplingLevel = 4;
        context.setPixelFormat (format);
        context.setMultisamplingEnabled (true);
        context.setOpenGLVersionRequired (juce::OpenGLContext::openGL3_2);
        context.setRenderer (this);
        context.setComponentPaintingEnabled (false);
        context.setContinuousRepainting (true);
        context.attachTo (*this);

        model.addListener (this);
        statusChanged();
        displayChanged (model.display());
    }

    StageComponent::~StageComponent()
    {
        model.removeListener (this);
        context.detach();
    }

    //==============================================================================
    void StageComponent::statusChanged()
    {
        const juce::SpinLock::ScopedLockType sl (lock);
        shared.ring = model.ring();
    }

    void StageComponent::displayChanged (const DisplayState& state)
    {
        auto image = displayRenderer.render (state);
        const juce::SpinLock::ScopedLockType sl (lock);
        shared.displayImage = std::move (image);
        shared.displayDirty = true;
        shared.activeGroup = state.activeGroup;
        shared.displayLive = state.feedMode == FeedMode::live;
    }

    void StageComponent::padTransition (const PadHoldRegistry::Transition& t)
    {
        const juce::SpinLock::ScopedLockType sl (lock);
        auto& pad = shared.pads[(std::size_t) t.pad];
        switch (t.kind)
        {
            case PadHoldRegistry::Kind::press:
            case PadHoldRegistry::Kind::restrike:
                pad.held = true;
                pad.velocity = t.velocity;
                ++pad.strikeCount;
                break;
            case PadHoldRegistry::Kind::release:
                pad.held = false;
                break;
            case PadHoldRegistry::Kind::sustain:
                break;
        }
    }

    void StageComponent::allPadsReleased()
    {
        const juce::SpinLock::ScopedLockType sl (lock);
        for (auto& pad : shared.pads)
            pad.held = false;
    }

    //==============================================================================
    void StageComponent::mouseDown (const juce::MouseEvent&)
    {
        dragStartYaw = yawDegrees.load();
        dragStartPitch = pitchDegrees.load();
    }

    void StageComponent::mouseDrag (const juce::MouseEvent& e)
    {
        yawDegrees = juce::jlimit (-60.0f, 110.0f, dragStartYaw - (float) e.getDistanceFromDragStartX() * 0.3f);
        pitchDegrees = juce::jlimit (12.0f, 85.0f, dragStartPitch + (float) e.getDistanceFromDragStartY() * 0.3f);
    }

    void StageComponent::mouseDoubleClick (const juce::MouseEvent&)
    {
        yawDegrees = 28.0f;
        pitchDegrees = 42.0f;
        radius = 0.70f;
    }

    void StageComponent::mouseWheelMove (const juce::MouseEvent&, const juce::MouseWheelDetails& wheel)
    {
        radius = juce::jlimit (0.40f, 1.20f, radius.load() * (1.0f - wheel.deltaY * 0.25f));
    }

    //==============================================================================
    void StageComponent::newOpenGLContextCreated()
    {
        mesh = HaloMesh::parse (HaloAssets::EP40_hmesh, (size_t) HaloAssets::EP40_hmeshSize);
        mesh.upload();
        meshLoaded = ! mesh.parts.empty();

        auto indexOf = [this] (std::string_view name)
        {
            for (size_t i = 0; i < mesh.parts.size(); ++i)
                if (mesh.parts[i].name == name)
                    return (int) i;
            return -1;
        };
        for (size_t i = 0; i < 12; ++i) padParts[i] = indexOf (ep40::gridPartNames[i]);
        for (size_t i = 0; i < 4; ++i) groupParts[i] = indexOf (ep40::groupPartNames[i]);
        displayPart = indexOf ("display_surface");
        ringPart = indexOf ("halo_ring");

        partShader = std::make_unique<juce::OpenGLShaderProgram> (context);
        const bool partOk = partShader->addVertexShader (partVertexShader)
                         && partShader->addFragmentShader (partFragmentShader);
        if (partOk)
        {
            const auto id = partShader->getProgramID();
            glBindAttribLocation (id, 0, "aPos");
            glBindAttribLocation (id, 1, "aNormal");
            glBindAttribLocation (id, 2, "aColour");
            glBindAttribLocation (id, 3, "aEmissive");
            glBindAttribLocation (id, 4, "aUV");
        }
        if (! partOk || ! partShader->link())
        {
            DBG ("part shader: " << partShader->getLastError());
            partShader.reset();
        }

        backgroundShader = std::make_unique<juce::OpenGLShaderProgram> (context);
        if (! backgroundShader->addVertexShader (backgroundVertexShader)
            || ! backgroundShader->addFragmentShader (backgroundFragmentShader)
            || ! backgroundShader->link())
        {
            DBG ("background shader: " << backgroundShader->getLastError());
            backgroundShader.reset();
        }

        glGenVertexArrays (1, &emptyVao);

        // Shadow quad in model space, under the chassis, nudged away from the key light.
        const float w = 0.105f, h = 0.138f, z = -0.0105f, dx = 0.010f, dy = -0.012f;
        const float quad[] {
            -w + dx, -h + dy, z, -1, -1,   w + dx, -h + dy, z, 1, -1,
            -w + dx,  h + dy, z, -1,  1,   w + dx,  h + dy, z, 1,  1,
        };
        glGenVertexArrays (1, &shadowVao);
        glBindVertexArray (shadowVao);
        glGenBuffers (1, &shadowVbo);
        glBindBuffer (GL_ARRAY_BUFFER, shadowVbo);
        glBufferData (GL_ARRAY_BUFFER, sizeof (quad), quad, GL_STATIC_DRAW);
        glEnableVertexAttribArray (0);
        glVertexAttribPointer (0, 3, GL_FLOAT, GL_FALSE, 5 * sizeof (float), nullptr);
        glEnableVertexAttribArray (4);
        glVertexAttribPointer (4, 2, GL_FLOAT, GL_FALSE, 5 * sizeof (float), (const void*) (3 * sizeof (float)));
        glDisableVertexAttribArray (1); glVertexAttrib3f (1, 0, 0, 1);
        glDisableVertexAttribArray (2); glVertexAttrib3f (2, 0, 0, 0);
        glDisableVertexAttribArray (3); glVertexAttrib1f (3, 0);
        glBindVertexArray (0);

        startTime = lastFrameTime = juce::Time::getMillisecondCounterHiRes() * 0.001;
    }

    void StageComponent::openGLContextClosing()
    {
        mesh.release();
        displayTexture.release();
        partShader.reset();
        backgroundShader.reset();
        if (emptyVao != 0) glDeleteVertexArrays (1, &emptyVao);
        if (shadowVao != 0) glDeleteVertexArrays (1, &shadowVao);
        if (shadowVbo != 0) glDeleteBuffers (1, &shadowVbo);
        emptyVao = shadowVao = shadowVbo = 0;
    }

    void StageComponent::drawBackground()
    {
        if (backgroundShader == nullptr)
            return;

        const auto& c = HaloColours::graphPaper;
        const auto scale = (float) context.getRenderingScale();
        backgroundShader->use();
        backgroundShader->setUniform ("uScale", scale);
        backgroundShader->setUniform ("uHeight", (float) getHeight() * scale);
        const auto canvas = toVec (c.canvas), minor = toVec (c.gridMinor), major = toVec (c.gridMajor);
        backgroundShader->setUniform ("uCanvas", canvas.x, canvas.y, canvas.z);
        backgroundShader->setUniform ("uMinor", minor.x, minor.y, minor.z, c.gridMinor.getFloatAlpha());
        backgroundShader->setUniform ("uMajor", major.x, major.y, major.z, c.gridMajor.getFloatAlpha());

        glDisable (GL_DEPTH_TEST);
        glBindVertexArray (emptyVao);
        glDrawArrays (GL_TRIANGLES, 0, 3);
    }

    void StageComponent::drawShadow (const Mat& viewProj)
    {
        partShader->setUniformMat4 ("uModel", modelToWorld().mat, 1, false);
        partShader->setUniformMat4 ("uViewProj", viewProj.mat, 1, false);
        partShader->setUniform ("uKind", 3);

        glEnable (GL_BLEND);
        glBlendFunc (GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
        glDepthMask (GL_FALSE);
        glBindVertexArray (shadowVao);
        glDrawArrays (GL_TRIANGLE_STRIP, 0, 4);
        glDepthMask (GL_TRUE);
        glDisable (GL_BLEND);
    }

    void StageComponent::drawModel (const Mat& viewProj, const SharedState& state, float dt, float time)
    {
        const auto toWorld = modelToWorld();
        const auto orange = linear (HaloColours::graphPaper.orange);
        const auto green = linear (HaloColours::graphPaper.riddimGreen.brighter (0.6f));

        // Ring appearance from the honest ring state (HaloRingRig.swift).
        int ringMode = 0;
        float ringLevel = 0.0f;
        auto ringColour = orange;
        switch (state.ring.kind)
        {
            case RingState::Kind::discovering: ringMode = 1; break;
            case RingState::Kind::connected:   ringMode = 2; ringLevel = 0.20f; break;
            case RingState::Kind::monitoring:  ringMode = 2; ringLevel = 0.40f; break;
            case RingState::Kind::recording:   ringMode = 2; ringLevel = 0.22f + 0.165f * (1.0f + std::sin (time * juce::MathConstants<float>::twoPi / 3.2f)); break;
            case RingState::Kind::transfer:    ringMode = 2; ringLevel = 0.85f * (float) state.ring.transferProgress; break;
            case RingState::Kind::error:       ringMode = 3; ringLevel = 0.30f; ringColour = linear (HaloColours::graphPaper.warning); break;
            case RingState::Kind::disconnected: break;
        }
        partShader->setUniform ("uRingMode", ringMode);
        partShader->setUniform ("uRingLevel", ringLevel);
        partShader->setUniform ("uRingColour", ringColour.x, ringColour.y, ringColour.z);
        partShader->setUniform ("uTime", time);
        partShader->setUniformMat4 ("uViewProj", viewProj.mat, 1, false);

        // Animate pads toward their targets.
        for (size_t i = 0; i < 12; ++i)
        {
            auto& anim = padAnim[i];
            const auto& target = state.pads[i];
            if (target.strikeCount != anim.seenStrikes)
            {
                anim.seenStrikes = target.strikeCount;
                anim.led = ledFloor + ledScale * target.velocity * target.velocity;
            }
            const float depth = target.held ? travelMin + (travelMax - travelMin) * target.velocity : 0.0f;
            const float tau = depth > anim.depth ? pressTau : releaseTau;
            anim.depth += (depth - anim.depth) * (1.0f - std::exp (-dt / tau));
            if (! target.held)
                anim.led *= std::exp (-dt / ledDecayTau);
        }

        for (int p = 0; p < (int) mesh.parts.size(); ++p)
        {
            const auto& part = mesh.parts[(size_t) p];
            Mat local = part.partToModel;
            juce::Vector3D<float> glow;
            int kind = 0;

            for (size_t i = 0; i < 12; ++i)
                if (padParts[i] == p)
                {
                    local = then (local, translation (0, 0, -padAnim[i].depth));
                    glow = orange * (padAnim[i].led * 0.9f);
                }

            for (size_t g = 0; g < 4; ++g)
                if (groupParts[g] == p && state.displayLive && state.activeGroup && (size_t) *state.activeGroup == g)
                    glow = green * 0.35f;

            if (p == displayPart && displayTexture.getTextureID() != 0)
            {
                kind = 1;
                displayTexture.bind();
                partShader->setUniform ("uTexture", 0);
            }
            if (p == ringPart)
                kind = 2;

            partShader->setUniformMat4 ("uModel", then (local, toWorld).mat, 1, false);
            partShader->setUniform ("uKind", kind);
            partShader->setUniform ("uGlow", glow.x, glow.y, glow.z);

            glBindVertexArray (part.vao);
            glDrawElements (GL_TRIANGLES, (GLsizei) part.indices.size(), GL_UNSIGNED_INT, nullptr);
        }
        glBindVertexArray (0);
    }

    void StageComponent::renderOpenGL()
    {
        const double nowSeconds = juce::Time::getMillisecondCounterHiRes() * 0.001;
        const float dt = (float) juce::jlimit (0.0, 0.1, nowSeconds - lastFrameTime);
        lastFrameTime = nowSeconds;

        SharedState state;
        {
            const juce::SpinLock::ScopedLockType sl (lock);
            state = shared;
            shared.displayDirty = false;
        }
        if (state.displayDirty && state.displayImage.isValid())
            displayTexture.loadImage (state.displayImage);

        const auto scale = (float) context.getRenderingScale();
        const int w = juce::roundToInt (scale * (float) getWidth());
        const int h = juce::roundToInt (scale * (float) getHeight());
        glViewport (0, 0, w, h);
        glClearColor (0, 0, 0, 1);
        glClear (GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

        drawBackground();
        if (! meshLoaded || partShader == nullptr || w == 0 || h == 0)
            return;

        const float yaw = juce::degreesToRadians (yawDegrees.load());
        const float pitch = juce::degreesToRadians (pitchDegrees.load());
        const float r = radius.load();
        const juce::Vector3D<float> eye (r * std::cos (pitch) * std::sin (yaw), r * std::sin (pitch),
                                         r * std::cos (pitch) * std::cos (yaw));
        const auto view = lookAt (eye, { 0, 0, 0 }, { 0, 1, 0 });
        const auto proj = perspective (24.0f, (float) w / (float) h, 0.05f, 5.0f);
        const auto viewProj = then (view, proj);

        glEnable (GL_DEPTH_TEST);
        glDepthFunc (GL_LEQUAL);
        glEnable (GL_MULTISAMPLE);
        glActiveTexture (GL_TEXTURE0);

        partShader->use();
        partShader->setUniform ("uEye", eye.x, eye.y, eye.z);
        drawShadow (viewProj);
        drawModel (viewProj, state, dt, (float) (nowSeconds - startTime));
    }
}
