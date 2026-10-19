#include "Stage/StageComponent.h"
#include "Core/Ep40MidiMapping.h"
#include "Stage/FacePrint.h"
#include "Stage/StageShaders.h"
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

        // Device and lighting rig (model space is Z-up metres; world is Y-up).
        constexpr float deviceWidth = 0.176f, deviceDepth = 0.240f;
        constexpr float groundZ = -0.0096f; // just under the halo ring
        constexpr int shadowMapSize = 2048;
        constexpr float shadowExtent = 0.20f; // half-size of the key light's ortho box
        constexpr float heightHalfX = 0.115f, heightHalfY = 0.145f, heightTop = 0.030f;
        const juce::Vector3D<float> keyDir  = juce::Vector3D<float> (-0.45f, 1.0f, 0.55f).normalised();
        const juce::Vector3D<float> topDir  = juce::Vector3D<float> (-0.10f, 1.0f, -0.55f).normalised();
        const juce::Vector3D<float> fillDir = juce::Vector3D<float> (0.85f, 0.35f, 0.40f).normalised();

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

        Mat orthographic (float halfWidth, float halfHeight, float zNear, float zFar)
        {
            float m[16] {};
            m[0] = 1.0f / halfWidth;
            m[5] = 1.0f / halfHeight;
            m[10] = -2.0f / (zFar - zNear);
            m[14] = -(zFar + zNear) / (zFar - zNear);
            m[15] = 1.0f;
            return Mat (m);
        }

        // Looks straight down the model's -Z onto the face; depth 0 is
        // heightTop and depth 1 is the ground.
        Mat heightProjection()
        {
            float m[16] {};
            m[0] = 1.0f / heightHalfX;
            m[5] = 1.0f / heightHalfY;
            m[10] = -2.0f / (heightTop - groundZ);
            m[14] = (heightTop + groundZ) / (heightTop - groundZ);
            m[15] = 1.0f;
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

        Mat keyLightViewProj()
        {
            const auto view = lookAt (keyDir * 0.6f, { 0, 0, 0 }, { 0, 1, 0 });
            return then (view, orthographic (shadowExtent, shadowExtent, 0.05f, 1.2f));
        }

        juce::Vector3D<float> toVec (juce::Colour c) { return { c.getFloatRed(), c.getFloatGreen(), c.getFloatBlue() }; }

        juce::Vector3D<float> linear (juce::Colour c)
        {
            auto f = [] (float v) { return std::pow (v, 2.2f); };
            return { f (c.getFloatRed()), f (c.getFloatGreen()), f (c.getFloatBlue()) };
        }

        std::unique_ptr<juce::OpenGLShaderProgram> buildProgram (juce::OpenGLContext& context, StageLog& log,
                                                                 const char* label, const char* vertex, const char* fragment,
                                                                 std::initializer_list<const char*> attributes)
        {
            auto program = std::make_unique<juce::OpenGLShaderProgram> (context);
            bool ok = program->addVertexShader (vertex) && program->addFragmentShader (fragment);
            if (ok)
            {
                GLuint location = 0;
                for (auto* name : attributes)
                    glBindAttribLocation (program->getProgramID(), location++, name);
                ok = program->link();
            }
            if (! ok)
            {
                // A failed shader leaves the stage blank, and drivers differ; keep the
                // compiler's message in the stage log, in release builds too.
                log.line (juce::String ("shader ") + label + " failed: " + program->getLastError());
                return {};
            }
            return program;
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

        // Dev hooks for repeatable screenshots (riddim-3d-plan.md, section 8).
        // HALO_STAGE_CAMERA=yaw,pitch,radius; HALO_STAGE_PRESET=hero|top|low|keys|edge;
        // HALO_STAGE_DEBUG=1..4 view AO, key shadow, height map or floor terms,
        // 5 the material swatch card, 6 the silhouette mask, 7 unlit albedo,
        // 8 emissive only. HALO_STAGE_POSE freezes input (Core/StagePose.h).
        const auto env = [] (const char* name) { return juce::SystemStats::getEnvironmentVariable (name, {}); };
       #if JUCE_DEBUG
        const char* buildKind = "debug";
       #else
        const char* buildKind = "release";
       #endif
        log.line ("HALO_STAGE start pid=" + juce::String (StageLog::processId()) + " build=" + buildKind);

        statsEnabled = env ("HALO_STAGE_STATS") == "1";
        if (const auto tier = env ("HALO_STAGE_TIER").toLowerCase(); tier.isNotEmpty())
        {
            if (tier != "high")
                log.line ("tier: HALO_STAGE_TIER=" + tier + " requested; only the high tier exists yet");
        }

       #if JUCE_DEBUG
        debugView = env ("HALO_STAGE_DEBUG").getIntValue();

        if (const auto preset = env ("HALO_STAGE_PRESET").toLowerCase(); preset.isNotEmpty())
        {
            struct Preset { const char* name; float yaw, pitch, radius; };
            static constexpr Preset presets[] {
                { "hero", 28.0f, 42.0f, 0.70f },
                { "top",   0.0f, 85.0f, 0.78f },
                { "low",  28.0f, 12.0f, 0.62f },
                { "keys", 18.0f, 52.0f, 0.42f },
                { "edge", 90.0f, 10.0f, 0.62f },
            };
            bool found = false;
            for (const auto& p : presets)
                if (preset == p.name)
                {
                    yawDegrees = p.yaw;
                    pitchDegrees = p.pitch;
                    radius = p.radius;
                    found = true;
                }
            log.line ("preset: " + preset + (found ? "" : " (unknown, ignored)"));
        }

        if (const auto text = env ("HALO_STAGE_POSE"); text.isNotEmpty())
        {
            pose = StagePose::parse (text.toStdString());
            if (pose)
            {
                log.line ("pose: " + text);
                for (const auto& bad : pose->errors)
                    log.line ("pose: ignored token '" + juce::String (bad) + "'");
            }
        }
       #endif

        const auto camera = juce::StringArray::fromTokens (env ("HALO_STAGE_CAMERA"), ",", {});
        if (camera.size() == 3)
        {
            yawDegrees = camera[0].getFloatValue();
            pitchDegrees = camera[1].getFloatValue();
            radius = camera[2].getFloatValue();
        }
        if (debugView != 0)
            log.line ("debug view: " + juce::String (debugView));

        model.addListener (this);
        if (pose)
            applyPose (*pose);
        else
        {
            statusChanged();
            displayChanged (model.display());
        }
    }

    // A pose replaces every live or mock input with a fixed state. The
    // listeners below ignore the model while a pose is active.
    void StageComponent::applyPose (const StagePose& p)
    {
        const auto display = p.display();
        auto image = displayRenderer.render (display);
        const juce::SpinLock::ScopedLockType sl (lock);
        shared.ring = p.ring;
        shared.displayImage = std::move (image);
        shared.displayDirty = true;
        shared.activeGroup = display.activeGroup;
        shared.displayLive = display.feedMode == FeedMode::live;
        for (size_t i = 0; i < shared.pads.size(); ++i)
        {
            shared.pads[i].held = p.pads[i] > 0.0f;
            shared.pads[i].velocity = p.pads[i];
            shared.pads[i].strikeCount = shared.pads[i].held ? 1 : 0;
        }
    }

    StageComponent::~StageComponent()
    {
        model.removeListener (this);
        context.detach();
    }

    //==============================================================================
    void StageComponent::statusChanged()
    {
        if (pose)
            return;
        const juce::SpinLock::ScopedLockType sl (lock);
        shared.ring = model.ring();
    }

    void StageComponent::displayChanged (const DisplayState& state)
    {
        if (pose)
            return;
        auto image = displayRenderer.render (state);
        const juce::SpinLock::ScopedLockType sl (lock);
        shared.displayImage = std::move (image);
        shared.displayDirty = true;
        shared.activeGroup = state.activeGroup;
        shared.displayLive = state.feedMode == FeedMode::live;
    }

    void StageComponent::padTransition (const PadHoldRegistry::Transition& t)
    {
        if (pose)
            return;
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
        if (pose)
            return;
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
        partToModel.assign (mesh.parts.size(), Mat());

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
        platePart = indexOf ("chassis_topPlate");

        // Which GPU and driver the stage landed on: "Let Windows decide" can put
        // Halo on the iGPU, and every timing or driver report needs this.
        {
            auto str = [] (GLenum e) { auto* c = (const char*) glGetString (e); return juce::String (c != nullptr ? c : "?"); };
            GLint samples = 0;
            glGetIntegerv (GL_SAMPLES, &samples);
            log.line ("GL_RENDERER=" + str (GL_RENDERER) + " GL_VERSION=" + str (GL_VERSION)
                      + " GL_VENDOR=" + str (GL_VENDOR) + " GL_SAMPLES=" + juce::String (samples) + " (default framebuffer)");
            log.line ("mesh: " + juce::String ((int) mesh.parts.size()) + " parts");
        }
        stats.create();
        if (statsEnabled)
            log.line (juce::String ("stats: GPU timer ") + (stats.hasTimer() ? "available" : "unavailable"));

        using namespace stage_shaders;
        partShader = buildProgram (context, log, "part", partVertex, partFragment,
                                   { "aPos", "aNormal", "aColour", "aEmissive", "aUV", "aMaterial" });
        depthShader = buildProgram (context, log, "depth", depthVertex, depthFragment, { "aPos" });
        backgroundShader = buildProgram (context, log, "background", backgroundVertex, backgroundFragment, {});
        cardShader = buildProgram (context, log, "card", backgroundVertex, cardFragment, {});

        // The swatch card: each listed part's dominant albedo by surface area.
        swatches.clear();
        {
            struct Entry { const char* name; const char* part; };
            static constexpr Entry entries[] {
                { "deck", "chassis_topPlate" }, { "body", "chassis_base" }, { "shell", "chassis_edgeBand" },
                { "key", "pad_5" }, { "orange", "button_record" }, { "green", "button_play" },
                { "plate", "brand_panel" }, { "frame", "display_bezel" },
            };
            for (const auto& e : entries)
            {
                const auto* part = mesh.find (e.part);
                if (part == nullptr)
                    continue;
                std::map<std::array<float, 3>, double> area;
                const auto& v = part->vertices;
                constexpr size_t n = MeshPart::floatsPerVertex;
                for (size_t t = 0; t + 2 < part->indices.size(); t += 3)
                {
                    const float* a = &v[part->indices[t] * n];
                    const float* b = &v[part->indices[t + 1] * n];
                    const float* c = &v[part->indices[t + 2] * n];
                    const juce::Vector3D<float> ab (b[0] - a[0], b[1] - a[1], b[2] - a[2]);
                    const juce::Vector3D<float> ac (c[0] - a[0], c[1] - a[1], c[2] - a[2]);
                    area[{ a[6], a[7], a[8] }] += 0.5 * (double) (ab ^ ac).length();
                }
                auto best = std::max_element (area.begin(), area.end(),
                                              [] (const auto& x, const auto& y) { return x.second < y.second; });
                if (best == area.end())
                    continue;
                swatches.push_back ({ e.name, { best->first[0], best->first[1], best->first[2] } });
            }
            if (debugView == 5)
            {
                juce::String line ("card:");
                for (const auto& sw : swatches)
                    line << " " << sw.name << "=" << juce::String (sw.linear.x, 4) << "," << juce::String (sw.linear.y, 4)
                         << "," << juce::String (sw.linear.z, 4);
                log.line (line);
            }
        }

        glGenVertexArrays (1, &emptyVao);

        // Ground quad in model space under the chassis; it only ever darkens.
        // Only as large as the shadow fade (see the shader), so no pixel is wasted.
        const float gx = 0.26f, gy = 0.34f;
        const float quad[] { -gx, -gy, groundZ, 0, 0,   gx, -gy, groundZ, 1, 0,
                             -gx,  gy, groundZ, 0, 1,   gx,  gy, groundZ, 1, 1 };
        glGenVertexArrays (1, &groundVao);
        glBindVertexArray (groundVao);
        glGenBuffers (1, &groundVbo);
        glBindBuffer (GL_ARRAY_BUFFER, groundVbo);
        glBufferData (GL_ARRAY_BUFFER, sizeof (quad), quad, GL_STATIC_DRAW);
        glEnableVertexAttribArray (0);
        glVertexAttribPointer (0, 3, GL_FLOAT, GL_FALSE, 5 * sizeof (float), nullptr);
        glEnableVertexAttribArray (4);
        glVertexAttribPointer (4, 2, GL_FLOAT, GL_FALSE, 5 * sizeof (float), (const void*) (3 * sizeof (float)));
        glDisableVertexAttribArray (1); glVertexAttrib3f (1, 0, 0, 1);
        glDisableVertexAttribArray (2); glVertexAttrib3f (2, 0, 0, 0);
        glDisableVertexAttribArray (3); glVertexAttrib1f (3, 0);
        glDisableVertexAttribArray (5); glVertexAttrib2f (5, 1, 0);
        glBindVertexArray (0);

        // The shadow map samples its depth texture with hardware comparison. The
        // height map keeps model z in a half-float colour texture, which (unlike
        // a depth texture) filters smoothly everywhere.
        auto makeTarget = [this] (DepthTarget& target, int width, int height, bool shadow)
        {
            target.width = width;
            target.height = height;
            auto texture = [&] (GLint internalFormat, GLenum format, GLenum type)
            {
                GLuint id = 0;
                glGenTextures (1, &id);
                glBindTexture (GL_TEXTURE_2D, id);
                glTexImage2D (GL_TEXTURE_2D, 0, internalFormat, width, height, 0, format, type, nullptr);
                glTexParameteri (GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
                glTexParameteri (GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
                glTexParameteri (GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
                glTexParameteri (GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
                return id;
            };

            GLint previous = 0;
            glGetIntegerv (GL_FRAMEBUFFER_BINDING, &previous);
            glGenFramebuffers (1, &target.fbo);
            glBindFramebuffer (GL_FRAMEBUFFER, target.fbo);
            if (shadow)
            {
                target.texture = texture (GL_DEPTH_COMPONENT24, GL_DEPTH_COMPONENT, GL_UNSIGNED_INT);
                glTexParameteri (GL_TEXTURE_2D, GL_TEXTURE_COMPARE_MODE, GL_COMPARE_REF_TO_TEXTURE);
                glTexParameteri (GL_TEXTURE_2D, GL_TEXTURE_COMPARE_FUNC, GL_LEQUAL);
                glFramebufferTexture2D (GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_TEXTURE_2D, target.texture, 0);
                glDrawBuffer (GL_NONE);
                glReadBuffer (GL_NONE);
            }
            else
            {
                target.texture = texture (GL_R16F, GL_RED, GL_FLOAT);
                glTexParameteri (GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR);
                glGenerateMipmap (GL_TEXTURE_2D);
                glFramebufferTexture2D (GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, target.texture, 0);
                glGenRenderbuffers (1, &target.depthBuffer);
                glBindRenderbuffer (GL_RENDERBUFFER, target.depthBuffer);
                glRenderbufferStorage (GL_RENDERBUFFER, GL_DEPTH_COMPONENT24, width, height);
                glFramebufferRenderbuffer (GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_RENDERBUFFER, target.depthBuffer);
                glDrawBuffer (GL_COLOR_ATTACHMENT0);
            }
            if (glCheckFramebufferStatus (GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE)
                log.line ("map target incomplete (" + juce::String (width) + "x" + juce::String (height) + ")");
            glBindFramebuffer (GL_FRAMEBUFFER, (GLuint) previous);
        };
        makeTarget (shadowTarget, shadowMapSize, shadowMapSize, true);
        makeTarget (heightTarget, 1024, juce::roundToInt (1024.0f * heightHalfY / heightHalfX), false);

        // The face silkscreen, mipmapped so small print stays clean at a distance.
        printTexture.loadImage (FacePrint::render());
        printTexture.bind();
        glGenerateMipmap (GL_TEXTURE_2D);
        glTexParameteri (GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR);
        glTexParameteri (GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        printTexture.unbind();

        startTime = lastFrameTime = juce::Time::getMillisecondCounterHiRes() * 0.001;
    }

    void StageComponent::openGLContextClosing()
    {
        mesh.release();
        displayTexture.release();
        printTexture.release();
        partShader.reset();
        depthShader.reset();
        backgroundShader.reset();
        cardShader.reset();
        stats.release();
        for (auto* target : { &shadowTarget, &heightTarget })
        {
            if (target->fbo != 0) glDeleteFramebuffers (1, &target->fbo);
            if (target->texture != 0) glDeleteTextures (1, &target->texture);
            if (target->depthBuffer != 0) glDeleteRenderbuffers (1, &target->depthBuffer);
            *target = {};
        }
        if (emptyVao != 0) glDeleteVertexArrays (1, &emptyVao);
        if (groundVao != 0) glDeleteVertexArrays (1, &groundVao);
        if (groundVbo != 0) glDeleteBuffers (1, &groundVbo);
        emptyVao = groundVao = groundVbo = 0;
    }

    //==============================================================================
    void StageComponent::animatePads (const SharedState& state, float dt)
    {
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
            if (pose)
            {
                anim.depth = depth; // frozen: the pose shows the settled state
                continue;
            }
            const float tau = depth > anim.depth ? pressTau : releaseTau;
            anim.depth += (depth - anim.depth) * (1.0f - std::exp (-dt / tau));
            if (! target.held)
                anim.led *= std::exp (-dt / ledDecayTau);
        }

        for (size_t p = 0; p < mesh.parts.size(); ++p)
            partToModel[p] = mesh.parts[p].partToModel;
        for (size_t i = 0; i < 12; ++i)
            if (padParts[i] >= 0)
                partToModel[(size_t) padParts[i]] = then (mesh.parts[(size_t) padParts[i]].partToModel,
                                                          translation (0, 0, -padAnim[i].depth));
    }

    // Depth-only pass of the whole device into `target`. The key light's map works
    // in world space; the top-down height map works directly in model space.
    void StageComponent::drawDepth (const DepthTarget& target, const Mat& viewProj, bool partsInWorld)
    {
        glBindFramebuffer (GL_FRAMEBUFFER, target.fbo);
        glViewport (0, 0, target.width, target.height);
        glDepthMask (GL_TRUE);
        glClearDepth (1.0);
        glClearColor (groundZ, groundZ, groundZ, groundZ); // height map: bare floor
        glClear (GL_DEPTH_BUFFER_BIT | (target.depthBuffer != 0 ? GL_COLOR_BUFFER_BIT : 0));
        glEnable (GL_DEPTH_TEST);
        glDepthFunc (GL_LESS);
        glEnable (GL_POLYGON_OFFSET_FILL);
        glPolygonOffset (1.1f, 2.0f);

        depthShader->use();
        depthShader->setUniformMat4 ("uViewProj", viewProj.mat, 1, false);
        const auto toWorld = modelToWorld();
        for (size_t p = 0; p < mesh.parts.size(); ++p)
        {
            if ((int) p == ringPart) // lies on the floor; never casts
                continue;
            const auto m = partsInWorld ? then (partToModel[p], toWorld) : partToModel[p];
            depthShader->setUniformMat4 ("uModel", m.mat, 1, false);
            glBindVertexArray (mesh.parts[p].vao);
            ++stats.geometryDraws;
            glDrawElements (GL_TRIANGLES, (GLsizei) mesh.parts[p].indices.size(), GL_UNSIGNED_INT, nullptr);
        }
        glBindVertexArray (0);
        glDisable (GL_POLYGON_OFFSET_FILL);
    }

    void StageComponent::drawBackground()
    {
        if (backgroundShader == nullptr || debugView >= 6) // mask, albedo, emissive: black
            return;
        if (debugView == 5)
            return drawCard();

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

    // HALO_STAGE_DEBUG=5: one horizontal band per swatch, top to bottom in the
    // logged order, each the unlit albedo through the active tone curve.
    void StageComponent::drawCard()
    {
        if (cardShader == nullptr || swatches.empty())
            return;
        cardShader->use();
        const auto scale = (float) context.getRenderingScale();
        cardShader->setUniform ("uHeight", (float) getHeight() * scale);
        cardShader->setUniform ("uCount", (int) swatches.size());
        cardShader->setUniform ("uExposure", 1.0f);
        for (size_t i = 0; i < swatches.size() && i < 16; ++i)
        {
            const auto name = "uSwatch[" + juce::String ((int) i) + "]";
            cardShader->setUniform (name.toRawUTF8(), swatches[i].linear.x, swatches[i].linear.y, swatches[i].linear.z);
        }
        glDisable (GL_DEPTH_TEST);
        glBindVertexArray (emptyVao);
        glDrawArrays (GL_TRIANGLES, 0, 3);
    }

    void StageComponent::drawGround (const Mat& viewProj)
    {
        if (debugView >= 5)
            return;
        Mat identity;
        partShader->setUniformMat4 ("uModel", modelToWorld().mat, 1, false);
        partShader->setUniformMat4 ("uPartToModel", identity.mat, 1, false);
        partShader->setUniformMat4 ("uViewProj", viewProj.mat, 1, false);
        partShader->setUniform ("uKind", 3);

        glEnable (GL_BLEND);
        glBlendFunc (GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
        glDepthMask (GL_FALSE);
        glBindVertexArray (groundVao);
        glDrawArrays (GL_TRIANGLE_STRIP, 0, 4);
        glDepthMask (GL_TRUE);
        glDisable (GL_BLEND);
    }

    void StageComponent::drawModel (const Mat& viewProj, const SharedState& state, float time)
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

        for (int p = 0; p < (int) mesh.parts.size(); ++p)
        {
            const auto& part = mesh.parts[(size_t) p];
            const auto& local = partToModel[(size_t) p];
            juce::Vector3D<float> glow;
            int kind = 0;

            for (size_t i = 0; i < 12; ++i)
                if (padParts[i] == p)
                    glow = orange * (padAnim[i].led * 0.9f);

            for (size_t g = 0; g < 4; ++g)
                if (groupParts[g] == p && state.displayLive && state.activeGroup && (size_t) *state.activeGroup == g)
                    glow = green * 0.35f;

            if (p == displayPart && displayTexture.getTextureID() != 0)
            {
                kind = 1;
                glActiveTexture (GL_TEXTURE0);
                displayTexture.bind();
            }
            if (p == ringPart)
                kind = 2;
            if (p == platePart)
                kind = 4;

            partShader->setUniformMat4 ("uModel", then (local, toWorld).mat, 1, false);
            partShader->setUniformMat4 ("uPartToModel", local.mat, 1, false);
            partShader->setUniform ("uKind", kind);
            partShader->setUniform ("uGlow", glow.x, glow.y, glow.z);

            glBindVertexArray (part.vao);
            ++stats.geometryDraws;
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
        const bool ready = meshLoaded && partShader != nullptr && depthShader != nullptr && w > 0 && h > 0;

        stats.beginFrame();
        renderFrame (state, nowSeconds, dt, w, h, ready);
        stats.endFrame ("continuous");
        if (statsEnabled)
            if (const auto line = stats.poll (nowSeconds, tierName); line.isNotEmpty())
                log.line (line);
        if (ready)
            noteFrameForReady (w, h);
    }

    // HALO_STAGE_READY: logged once the stage has drawn 30 frames at one size
    // for half a second (and again after every later resize), so
    // tools/dev/stage_shots.ps1 knows when a screenshot shows a settled frame.
    void StageComponent::noteFrameForReady (int w, int h)
    {
        const double now = juce::Time::getMillisecondCounterHiRes() * 0.001;
        if (w != readyWidth || h != readyHeight)
        {
            readyWidth = w;
            readyHeight = h;
            framesAtSize = 0;
            sizeSince = now;
            readyLogged = false;
        }
        ++framesAtSize;
        if (readyLogged || framesAtSize < 30 || now - sizeSince < 0.5)
            return;
        readyLogged = true;
        juce::MessageManager::callAsync ([safe = juce::Component::SafePointer<StageComponent> (this), w, h]
        {
            if (safe == nullptr)
                return;
            // Physical screen pixels, so a DPI-aware capture can crop the stage.
            const auto bounds = safe->getScreenBounds();
            const auto* display = juce::Desktop::getInstance().getDisplays().getDisplayForRect (bounds);
            const double s = display != nullptr ? display->scale : 1.0;
            safe->log.line ("HALO_STAGE_READY stage=" + juce::String (juce::roundToInt (bounds.getX() * s)) + ","
                            + juce::String (juce::roundToInt (bounds.getY() * s)) + ","
                            + juce::String (w) + "," + juce::String (h) + " scale=" + juce::String (s, 3));
        });
    }

    void StageComponent::renderFrame (const SharedState& state, double nowSeconds, float dt, int w, int h, bool ready)
    {
        GLint mainFramebuffer = 0;
        glGetIntegerv (GL_FRAMEBUFFER_BINDING, &mainFramebuffer);

        // Shadow and height maps first: both follow the pads as they travel.
        const auto lightViewProj = keyLightViewProj();
        if (ready)
        {
            animatePads (state, dt);
            glDisable (GL_BLEND);
            drawDepth (shadowTarget, lightViewProj, true);
            drawDepth (heightTarget, heightProjection(), false);
            glBindTexture (GL_TEXTURE_2D, heightTarget.texture);
            glGenerateMipmap (GL_TEXTURE_2D); // cone-traced AO reads wide taps from coarse mips
            glBindFramebuffer (GL_FRAMEBUFFER, (GLuint) mainFramebuffer);
        }

        glViewport (0, 0, w, h);
        glClearColor (0, 0, 0, 1);
        glClear (GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

        drawBackground();
        if (! ready || debugView == 5)
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

        glActiveTexture (GL_TEXTURE1);
        glBindTexture (GL_TEXTURE_2D, shadowTarget.texture);
        glActiveTexture (GL_TEXTURE2);
        glBindTexture (GL_TEXTURE_2D, heightTarget.texture);
        glActiveTexture (GL_TEXTURE3);
        glBindTexture (GL_TEXTURE_2D, printTexture.getTextureID());
        glActiveTexture (GL_TEXTURE0);

        const auto shadowTint = toVec (HaloColours::graphPaper.ink);
        partShader->use();
        partShader->setUniform ("uTexture", 0);
        partShader->setUniform ("uShadowMap", 1);
        partShader->setUniform ("uHeightMap", 2);
        partShader->setUniform ("uPrint", 3);
        partShader->setUniform ("uEye", eye.x, eye.y, eye.z);
        partShader->setUniformMat4 ("uLightViewProj", lightViewProj.mat, 1, false);
        partShader->setUniform ("uShadowRadius", 0.0014f / (2.0f * shadowExtent));
        partShader->setUniform ("uHeightRect", 0.0f, 0.0f, heightHalfX, heightHalfY);
        partShader->setUniform ("uHeightTexel", 2.0f * heightHalfX / (float) heightTarget.width);
        partShader->setUniform ("uDeviceSize", deviceWidth, deviceDepth);
        partShader->setUniform ("uKeyDir", keyDir.x, keyDir.y, keyDir.z);
        partShader->setUniform ("uTopDir", topDir.x, topDir.y, topDir.z);
        partShader->setUniform ("uFillDir", fillDir.x, fillDir.y, fillDir.z);
        partShader->setUniform ("uShadowTint", shadowTint.x, shadowTint.y, shadowTint.z);
        partShader->setUniform ("uExposure", 1.0f);
        partShader->setUniform ("uDebug", debugView);

        // A pose freezes the clock too, so two captures of one pose match.
        drawModel (viewProj, state, pose ? 1.0f : (float) (nowSeconds - startTime));
        drawGround (viewProj); // after the device: the floor beneath it fails the depth test
    }
}
