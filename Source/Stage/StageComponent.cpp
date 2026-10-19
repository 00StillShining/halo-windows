#include "Stage/StageComponent.h"
#include "Core/Ep40MidiMapping.h"
#include "Stage/FacePrint.h"
#include "Stage/StageShaders.h"

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

        // Halo cue colours on the device itself (pad strike, active group). Device
        // facts, not look tokens: the riddim orange and green (plan section 2.5).
        const juce::Colour cueOrange (0xffFA5A16), cueGreen (0xff0A4A36);

        // Device and lighting rig (model space is Z-up metres; world is Y-up).
        constexpr float deviceWidth = 0.176f, deviceDepth = 0.240f;
        constexpr int shadowMapSize = 2048;
        constexpr float shadowExtent = 0.20f; // half-size of the key light's ortho box
        constexpr float heightHalfX = 0.115f, heightHalfY = 0.145f, heightTop = 0.030f;
        const juce::Vector3D<float> keyDir  = juce::Vector3D<float> (-0.45f, 1.0f, 0.55f).normalised();
        const juce::Vector3D<float> topDir  = juce::Vector3D<float> (-0.10f, 1.0f, -0.55f).normalised();
        const juce::Vector3D<float> fillDir = juce::Vector3D<float> (0.85f, 0.35f, 0.40f).normalised();

        using Mat = juce::Matrix3D<float>;

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
        Mat heightProjection (float groundZ)
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

        // Anisotropic filtering where the driver has it (8x), for decals seen
        // at grazing angles.
        void enableAnisotropy()
        {
            constexpr GLenum maxAnisotropy = 0x84FF, textureMaxAnisotropy = 0x84FE; // EXT/ARB_texture_filter_anisotropic
            if (! juce::OpenGLHelpers::isExtensionSupported ("GL_EXT_texture_filter_anisotropic")
                && ! juce::OpenGLHelpers::isExtensionSupported ("GL_ARB_texture_filter_anisotropic"))
                return;
            GLfloat limit = 1.0f;
            glGetFloatv (maxAnisotropy, &limit);
            glTexParameterf (GL_TEXTURE_2D, textureMaxAnisotropy, std::min (8.0f, limit));
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
            // Zoom multiplies the fitted radius; the offset (world metres) moves
            // the target after fitting, e.g. onto the keys.
            struct Preset { const char* name; float yaw, pitch, zoom; bool ortho; Vec3f offset; };
            static const Preset presets[] {
                { "hero", heroYaw, heroPitch, 1.0f, false, {} },
                { "top",   0.0f, 90.0f, 1.0f, true, {} },
                { "low",  28.0f, 12.0f, 1.0f, false, {} },
                { "keys", 20.0f, 50.0f, 0.58f, false, { 0.0f, 0.0f, 0.045f } },
                { "edge", 90.0f, 8.0f, 1.0f, false, {} },
            };
            bool found = false;
            for (const auto& p : presets)
                if (preset == p.name)
                {
                    yawDegrees = p.yaw;
                    pitchDegrees = p.pitch;
                    zoom = p.zoom;
                    orthoView = p.ortho;
                    presetOffset = p.offset;
                    found = true;
                }
            log.line ("preset: " + preset + (found ? "" : " (unknown, ignored)"));
        }

        if (const auto name = env ("HALO_STAGE_LOOK"); name.isNotEmpty())
        {
            shared.look = StageLook::named (name);
            log.line ("look: " + name);
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
        if (camera.size() == 3) // yaw, pitch, zoom (1 = fitted)
        {
            yawDegrees = camera[0].getFloatValue();
            pitchDegrees = camera[1].getFloatValue();
            zoom = camera[2].getFloatValue();
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

        // Last, so the GL thread never sees a half-configured stage.
        context.attachTo (*this);
    }

    void StageComponent::setLook (const StageLook& newLook)
    {
        const juce::SpinLock::ScopedLockType sl (lock);
        shared.look = newLook;
        shared.lookDirty = true;
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
        pitchDegrees = juce::jlimit (StageCamera::minPitch, StageCamera::maxPitch,
                                     dragStartPitch + (float) e.getDistanceFromDragStartY() * 0.3f);
        orthoView = false;
    }

    void StageComponent::mouseDoubleClick (const juce::MouseEvent&)
    {
        yawDegrees = heroYaw;
        pitchDegrees = heroPitch;
        zoom = 1.0f;
        orthoView = false;
        presetOffsetActive = false;
    }

    void StageComponent::mouseWheelMove (const juce::MouseEvent&, const juce::MouseWheelDetails& wheel)
    {
        zoom = juce::jlimit (StageCamera::minZoom, StageCamera::maxZoom, zoom.load() * (1.0f - wheel.deltaY * 0.25f));
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

        // Bounds of the parts that cast shadows: everything with triangles except
        // the halo ring (an anchor; it used to reach 1.4 mm below the case and
        // made the device float). The floor sits at their lowest point, and the
        // camera fits their box, knobs included.
        {
            juce::Vector3D<float> lo (1e9f, 1e9f, 1e9f), hi (-1e9f, -1e9f, -1e9f);
            for (size_t p = 0; p < mesh.parts.size(); ++p)
            {
                const auto& part = mesh.parts[p];
                if ((int) p == ringPart || part.indices.empty())
                    continue;
                for (size_t v = 0; v < part.vertices.size(); v += MeshPart::floatsPerVertex)
                {
                    const float* m = part.partToModel.mat; // column-major
                    const float x = part.vertices[v], y = part.vertices[v + 1], z = part.vertices[v + 2];
                    const juce::Vector3D<float> q (m[0] * x + m[4] * y + m[8] * z + m[12],
                                                   m[1] * x + m[5] * y + m[9] * z + m[13],
                                                   m[2] * x + m[6] * y + m[10] * z + m[14]);
                    lo = { std::min (lo.x, q.x), std::min (lo.y, q.y), std::min (lo.z, q.z) };
                    hi = { std::max (hi.x, q.x), std::max (hi.y, q.y), std::max (hi.z, q.z) };
                }
            }
            if (lo.x < hi.x)
            {
                groundZ = lo.z;
                // Model (x, y, z) -> world (x, z, -y).
                deviceBox = { { lo.x, lo.z, -hi.y }, { hi.x, hi.z, -lo.y } };
            }
            log.line ("bounds (mm): x " + juce::String (lo.x * 1000.0f, 2) + ".." + juce::String (hi.x * 1000.0f, 2)
                      + " y " + juce::String (lo.y * 1000.0f, 2) + ".." + juce::String (hi.y * 1000.0f, 2)
                      + " z " + juce::String (lo.z * 1000.0f, 2) + ".." + juce::String (hi.z * 1000.0f, 2)
                      + "; floor at z " + juce::String (groundZ * 1000.0f, 2));
        }

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

        // Ground quad in model space under the chassis, at the lowest shadow-casting
        // point: it darkens (contact shadow) and lights (ring pool, dark-look key
        // spot). Only as large as those fade out (see the shader).
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
        const auto print = FacePrint::render();
       #if JUCE_DEBUG
        if (const auto dump = juce::SystemStats::getEnvironmentVariable ("HALO_STAGE_DUMP_PRINT", {}); dump.isNotEmpty())
        {
            juce::File file (dump);
            file.deleteFile();
            if (juce::FileOutputStream out (file); out.openedOk())
                juce::PNGImageFormat().writeImageToStream (print, out);
        }
       #endif
        printTexture.loadImage (print);
        printTexture.bind();
        glGenerateMipmap (GL_TEXTURE_2D);
        glTexParameteri (GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR);
        glTexParameteri (GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        enableAnisotropy();
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
            if ((int) p == ringPart || mesh.parts[p].indices.empty()) // anchors never cast
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

        const auto scale = (float) context.getRenderingScale();
        backgroundShader->use();
        backgroundShader->setUniform ("uScale", scale);
        backgroundShader->setUniform ("uHeight", (float) getHeight() * scale);
        backgroundShader->setUniform ("uCell", look.gridCellPt);
        const auto canvas = toVec (look.floor), minor = toVec (look.gridMinor), major = toVec (look.gridMajor);
        backgroundShader->setUniform ("uCanvas", canvas.x, canvas.y, canvas.z);
        backgroundShader->setUniform ("uMinor", minor.x, minor.y, minor.z, look.gridMinor.getFloatAlpha());
        backgroundShader->setUniform ("uMajor", major.x, major.y, major.z, look.gridMajor.getFloatAlpha());

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

    // The floor around the device, drawn after it (the floor hidden under the
    // device fails the depth test) in up to three passes over the backdrop:
    //   1. key-light spot (dark look only): the floor near the device is lit,
    //   2. contact shadow: blends toward the shadow tint,
    //   3. ring pool: Halo's ring state as light on the floor, screen-blended,
    //      so where no light falls the backdrop is untouched.
    void StageComponent::drawGround (const Mat& viewProj, const SharedState& state, float time)
    {
        if (debugView >= 5)
            return;

        // Ring pool from the honest ring state (HaloRingRig.swift levels).
        int ringMode = 0; // 0 none, 1 discover sweep, 2 steady
        float ringLevel = 0.0f;
        switch (state.ring.kind)
        {
            case RingState::Kind::discovering: ringMode = 1; ringLevel = 0.60f; break;
            case RingState::Kind::connected:   ringMode = 2; ringLevel = 0.20f; break;
            case RingState::Kind::monitoring:  ringMode = 2; ringLevel = 0.40f; break;
            case RingState::Kind::recording:   ringMode = 2; ringLevel = 0.22f + 0.18f * (1.0f + std::sin (time * juce::MathConstants<float>::twoPi / 3.2f)); break;
            case RingState::Kind::transfer:    ringMode = 2; ringLevel = 0.15f + 0.75f * (float) state.ring.transferProgress; break;
            case RingState::Kind::error:       ringMode = 2; ringLevel = 0.40f; break;
            case RingState::Kind::disconnected: break;
        }
        const auto ringColour = toVec (StageLook::ringColour (look, state.ring.kind));
        const auto tint = toVec (look.shadowTint);

        Mat identity;
        partShader->setUniformMat4 ("uModel", modelToWorld().mat, 1, false);
        partShader->setUniformMat4 ("uPartToModel", identity.mat, 1, false);
        partShader->setUniformMat4 ("uViewProj", viewProj.mat, 1, false);
        partShader->setUniform ("uKind", 3);
        partShader->setUniform ("uShadowTint", tint.x, tint.y, tint.z);
        partShader->setUniform ("uRingMode", ringMode);
        partShader->setUniform ("uRingLevel", ringLevel);
        partShader->setUniform ("uRingColour", ringColour.x, ringColour.y, ringColour.z);
        partShader->setUniform ("uRingPeriod", ringDiscoverPeriod);
        partShader->setUniform ("uFloorSpot", look.floorSpot);
        GLint viewport[4] {};
        glGetIntegerv (GL_VIEWPORT, viewport);
        partShader->setUniform ("uViewport", (float) viewport[2], (float) viewport[3]);
        partShader->setUniform ("uTime", time);

        glEnable (GL_BLEND);
        glDepthMask (GL_FALSE);
        glBindVertexArray (groundVao);
        auto pass = [this] (int which, GLenum src, GLenum dst)
        {
            partShader->setUniform ("uGroundPass", which);
            glBlendFunc (src, dst);
            glDrawArrays (GL_TRIANGLE_STRIP, 0, 4);
        };
        if (look.floorSpot > 0.0f)
            pass (1, GL_ONE_MINUS_DST_COLOR, GL_ONE);   // screen
        pass (0, GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA); // shadow
        if (ringMode != 0 && ringLevel > 0.0f)
            pass (2, GL_ONE_MINUS_DST_COLOR, GL_ONE);   // screen
        glDepthMask (GL_TRUE);
        glDisable (GL_BLEND);
    }

    void StageComponent::drawModel (const Mat& viewProj, const SharedState& state, float time)
    {
        juce::ignoreUnused (time);
        const auto toWorld = modelToWorld();
        const auto orange = linear (cueOrange);
        const auto green = linear (cueGreen.brighter (0.6f));
        partShader->setUniformMat4 ("uViewProj", viewProj.mat, 1, false);

        for (int p = 0; p < (int) mesh.parts.size(); ++p)
        {
            const auto& part = mesh.parts[(size_t) p];
            if (p == ringPart || part.indices.empty()) // the ring is a floor pool now; anchors draw nothing
                continue;
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
            if (shared.lookDirty)
            {
                look = shared.look;
                shared.lookDirty = false;
            }
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
            drawDepth (heightTarget, heightProjection (groundZ), false);
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

        // Fit the shadow-casting bounds into the stage at this angle and aspect
        // (6% margin, centred), then apply the user's zoom.
        const auto camera = StageCamera::view (deviceBox, yawDegrees.load(), pitchDegrees.load(), zoom.load(),
                                               (float) w / (float) h, orthoView.load(),
                                               presetOffsetActive.load() ? presetOffset : Vec3f {});
        const Mat viewProj (camera.viewProjection.data());
        const juce::Vector3D<float> eye (camera.eye.x, camera.eye.y, camera.eye.z);

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

        const auto shadowTint = toVec (look.shadowTint);
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
        partShader->setUniform ("uExposure", look.exposure);
        partShader->setUniform ("uDebug", debugView);

        // A pose freezes the clock too, so two captures of one pose match.
        const float time = pose ? 1.0f : (float) (nowSeconds - startTime);
        drawModel (viewProj, state, time);
        drawGround (viewProj, state, time); // after the device: the floor beneath it fails the depth test
    }
}
