#include "Stage/StageComponent.h"
#include "Core/Ep40MidiMapping.h"
#include "Core/ToneCurves.h"
#include "Stage/FacePrint.h"

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

        // Attribute and output names, bound by index (HaloMesh's vertex layout).
        const std::initializer_list<const char*> partAttributes { "aPos", "aNormal", "aColour", "aEmissive", "aUV", "aMaterial" };

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
            return { tone::srgbDecode (c.getFloatRed()), tone::srgbDecode (c.getFloatGreen()), tone::srgbDecode (c.getFloatBlue()) };
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
    }

    //==============================================================================
    StageComponent::StageComponent (AppModel& m) : model (m)
    {
        setOpaque (true);

        // The stage owns its anti-aliasing (MSAA in its own HDR targets, or FXAA),
        // so JUCE's window framebuffer asks for none.
        juce::OpenGLPixelFormat format;
        format.multisamplingLevel = 0;
        context.setPixelFormat (format);
        context.setMultisamplingEnabled (false);
        context.setOpenGLVersionRequired (juce::OpenGLContext::openGL3_2);
        context.setRenderer (this);
        context.setComponentPaintingEnabled (false);
        context.setContinuousRepainting (true);

        // Dev hooks for repeatable screenshots (riddim-3d-plan.md, section 8).
        // HALO_STAGE_CAMERA=yaw,pitch,zoom; HALO_STAGE_PRESET=hero|top|low|keys|edge;
        // HALO_STAGE_DEBUG=1..4 view AO, key shadow, height map or floor terms,
        // 5 the material swatch card, 6 the silhouette mask, 7 unlit albedo,
        // 8 emissive only. HALO_STAGE_POSE freezes input (Core/StagePose.h).
        // HALO_STAGE_TIER=high|low, HALO_STAGE_TONEMAP=neutral|agx|aces,
        // HALO_STAGE_FXAA=1 forces FXAA on the High tier.
        const auto env = [] (const char* name) { return juce::SystemStats::getEnvironmentVariable (name, {}); };
       #if JUCE_DEBUG
        const char* buildKind = "debug";
       #else
        const char* buildKind = "release";
       #endif
        log.line ("HALO_STAGE start pid=" + juce::String (StageLog::processId()) + " build=" + buildKind);

        statsEnabled = env ("HALO_STAGE_STATS") == "1";
        tierRequest = env ("HALO_STAGE_TIER").toLowerCase();
        if (tierRequest.isNotEmpty())
            log.line ("tier: HALO_STAGE_TIER=" + tierRequest);

       #if JUCE_DEBUG
        debugView = env ("HALO_STAGE_DEBUG").getIntValue();
        forceFxaa = env ("HALO_STAGE_FXAA") == "1";

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
        if (const auto name = env ("HALO_STAGE_TONEMAP").toLowerCase(); name.isNotEmpty())
        {
            shared.look.toneMapper = tone::parse (name.toStdString(), shared.look.toneMapper);
            log.line ("tone mapper: " + juce::String (std::string (tone::name (shared.look.toneMapper))));
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
    // Builds every program for the requested tier. High (HDR) needs float MSAA
    // targets and every HDR program; any failure falls back to Low (today's
    // forward path into an LDR target, then FXAA), with the reason logged.
    bool StageComponent::buildPrograms()
    {
        releasePrograms();
        targets.release();

        auto attach = [this] (GlProgram& p) { p.lookupCounter = &stats.uniformLookups; };
        for (auto* p : { &partProgram, &depthProgram, &backgroundProgram, &cardProgram, &resolveProgram,
                         &compositeProgram, &fxaaProgram, &selfTestProgram })
            attach (*p);

        bool high = tierRequest != "low";
        juce::String why;
        if (high)
        {
            const bool ok = targets.create (true, 4, log)
                && partProgram.build (shaders, log, "part (HDR)", "part.vert", "part.frag", true, partAttributes, { "fragColour", "floorOut" })
                && depthProgram.build (shaders, log, "depth", "depth.vert", "depth.frag", true, { "aPos" }, { "fragColour" })
                && cardProgram.build (shaders, log, "card (HDR)", "fullscreen.vert", "card.frag", true, {}, { "fragColour" })
                && resolveProgram.build (shaders, log, "resolve", "fullscreen.vert", "resolve.frag", true, {}, { "deviceOut", "floorResolved" })
                && compositeProgram.build (shaders, log, "composite", "fullscreen.vert", "composite.frag", true, {}, { "fragColour" })
                && fxaaProgram.build (shaders, log, "fxaa", "fullscreen.vert", "fxaa.frag", true, {}, { "fragColour" });
            if (! ok)
            {
                high = false;
                why = "an HDR target or program failed (see above)";
            }
        }
        if (! high)
        {
            releasePrograms();
            targets.release();
            const bool ok = targets.create (false, 1, log)
                && partProgram.build (shaders, log, "part (LDR)", "part.vert", "part.frag", false, partAttributes, { "fragColour" })
                && depthProgram.build (shaders, log, "depth", "depth.vert", "depth.frag", false, { "aPos" }, { "fragColour" })
                && backgroundProgram.build (shaders, log, "background", "fullscreen.vert", "background.frag", false, {}, { "fragColour" })
                && cardProgram.build (shaders, log, "card (LDR)", "fullscreen.vert", "card.frag", false, {}, { "fragColour" })
                && fxaaProgram.build (shaders, log, "fxaa", "fullscreen.vert", "fxaa.frag", false, {}, { "fragColour" });
            if (! ok)
            {
                log.line ("tier: low tier failed too; the stage cannot draw");
                tierName = "none";
                return false;
            }
        }
       #if JUCE_DEBUG
        selfTestProgram.build (shaders, log, "selftest", "fullscreen.vert", "selftest.frag", true, {}, { "fragColour" });
       #endif

        tierName = high ? "high" : "low";
        fxaaEngaged = ! high || targets.samples() < 4 || forceFxaa;
        log.line ("tier: " + tierName + (why.isNotEmpty() ? " (demoted: " + why + ")" : juce::String())
                  + (tierRequest == "low" ? " (requested)" : juce::String())
                  + "; AA: " + (high ? juce::String (targets.samples()) + "x MSAA" : juce::String ("none"))
                  + (fxaaEngaged ? " + FXAA" : ""));
        return true;
    }

    void StageComponent::releasePrograms()
    {
        for (auto* p : { &partProgram, &depthProgram, &backgroundProgram, &cardProgram, &resolveProgram,
                         &compositeProgram, &fxaaProgram, &selfTestProgram })
            p->release();
    }

    // Debug: a 64-pixel ramp through each GLSL tone curve, read back and
    // compared with Core/ToneCurves.h (which Tests/ToneCurvesTests checks).
    void StageComponent::runToneSelfTest()
    {
        if (! selfTestProgram.valid())
            return;
        GLint previous = 0;
        glGetIntegerv (GL_FRAMEBUFFER_BINDING, &previous);
        GLuint texture = 0, fbo = 0;
        glGenTextures (1, &texture);
        glBindTexture (GL_TEXTURE_2D, texture);
        glTexImage2D (GL_TEXTURE_2D, 0, GL_RGBA32F, 64, 1, 0, GL_RGBA, GL_FLOAT, nullptr);
        glGenFramebuffers (1, &fbo);
        glBindFramebuffer (GL_FRAMEBUFFER, fbo);
        glFramebufferTexture2D (GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, texture, 0);
        glViewport (0, 0, 64, 1);
        glDisable (GL_BLEND);
        glDisable (GL_DEPTH_TEST);
        selfTestProgram.use();
        glBindVertexArray (emptyVao);

        for (auto mapper : { ToneMapper::neutral, ToneMapper::agx, ToneMapper::aces })
        {
            selfTestProgram.set ("uToneMapper", (int) mapper);
            glDrawArrays (GL_TRIANGLES, 0, 3);
            std::vector<float> pixels (64 * 4);
            glReadPixels (0, 0, 64, 1, GL_RGBA, GL_FLOAT, pixels.data());
            float worst = 0.0f;
            for (int i = 0; i < 64; ++i)
            {
                const float x = 4.0f * (float) i / 63.0f;
                const auto expected = tone::apply (mapper, { x, 0.5f * x, 0.1f * x });
                for (int c = 0; c < 3; ++c)
                    worst = std::max (worst, std::abs (pixels[(size_t) (i * 4 + c)] - expected[(size_t) c]));
            }
            log.line ("tone self-test " + juce::String (std::string (tone::name (mapper))) + ": max |GLSL - C++| = "
                      + juce::String (worst, 7) + (worst <= 1e-3f ? " PASS" : " FAIL"));
        }
        glBindVertexArray (0);
        glBindFramebuffer (GL_FRAMEBUFFER, (GLuint) previous);
        glDeleteFramebuffers (1, &fbo);
        glDeleteTextures (1, &texture);
    }

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
                      + " GL_VENDOR=" + str (GL_VENDOR) + " GL_SAMPLES=" + juce::String (samples) + " (window framebuffer)");
            log.line ("mesh: " + juce::String ((int) mesh.parts.size()) + " parts");
        }
        stats.create();
        stats.tracksLookups = true;
        if (statsEnabled)
            log.line (juce::String ("stats: GPU timer ") + (stats.hasTimer() ? "available" : "unavailable"));

        glGenVertexArrays (1, &emptyVao);
        buildPrograms();

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

        // Ground quad in model space under the chassis, at the lowest shadow-casting
        // point: it carries the contact shadow (and, on the Low tier, the ring pool
        // and key spot). Only as large as those fade out (see part.frag).
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

       #if JUCE_DEBUG
        runToneSelfTest();
       #endif

        startTime = lastFrameTime = juce::Time::getMillisecondCounterHiRes() * 0.001;
    }

    void StageComponent::openGLContextClosing()
    {
        mesh.release();
        displayTexture.release();
        printTexture.release();
        releasePrograms();
        targets.release();
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

        depthProgram.use();
        depthProgram.setMat4 ("uViewProj", viewProj.mat);
        const auto toWorld = modelToWorld();
        for (size_t p = 0; p < mesh.parts.size(); ++p)
        {
            if ((int) p == ringPart || mesh.parts[p].indices.empty()) // anchors never cast
                continue;
            const auto m = partsInWorld ? then (partToModel[p], toWorld) : partToModel[p];
            depthProgram.setMat4 ("uModel", m.mat);
            glBindVertexArray (mesh.parts[p].vao);
            ++stats.geometryDraws;
            glDrawElements (GL_TRIANGLES, (GLsizei) mesh.parts[p].indices.size(), GL_UNSIGNED_INT, nullptr);
        }
        glBindVertexArray (0);
        glDisable (GL_POLYGON_OFFSET_FILL);
    }

    // Low tier: the backdrop, drawn first in display values.
    void StageComponent::drawBackground (int height)
    {
        if (debugView >= 6) // mask, albedo, emissive: black
            return;
        const auto scale = (float) context.getRenderingScale();
        backgroundProgram.use();
        backgroundProgram.set ("uScale", scale);
        backgroundProgram.set ("uHeight", (float) height);
        backgroundProgram.set ("uCell", look.gridCellPt);
        const auto canvas = toVec (look.floor), minor = toVec (look.gridMinor), major = toVec (look.gridMajor);
        backgroundProgram.set ("uCanvas", canvas.x, canvas.y, canvas.z);
        backgroundProgram.set ("uMinor", minor.x, minor.y, minor.z, look.gridMinor.getFloatAlpha());
        backgroundProgram.set ("uMajor", major.x, major.y, major.z, look.gridMajor.getFloatAlpha());
        glDisable (GL_DEPTH_TEST);
        glBindVertexArray (emptyVao);
        glDrawArrays (GL_TRIANGLES, 0, 3);
    }

    // HALO_STAGE_DEBUG=5: one horizontal band per swatch, top to bottom in the
    // logged order, each the unlit albedo through the active tone curve.
    void StageComponent::drawCard (int height)
    {
        if (swatches.empty())
            return;
        static const char* names[16] { "uSwatch[0]", "uSwatch[1]", "uSwatch[2]", "uSwatch[3]", "uSwatch[4]", "uSwatch[5]",
                                       "uSwatch[6]", "uSwatch[7]", "uSwatch[8]", "uSwatch[9]", "uSwatch[10]", "uSwatch[11]",
                                       "uSwatch[12]", "uSwatch[13]", "uSwatch[14]", "uSwatch[15]" };
        cardProgram.use();
        cardProgram.set ("uHeight", (float) height);
        cardProgram.set ("uCount", (int) std::min<size_t> (swatches.size(), 16));
        cardProgram.set ("uExposure", look.exposure);
        cardProgram.set ("uToneMapper", (int) look.toneMapper);
        for (size_t i = 0; i < swatches.size() && i < 16; ++i)
            cardProgram.set (names[i], swatches[i].linear.x, swatches[i].linear.y, swatches[i].linear.z);
        glDisable (GL_DEPTH_TEST);
        glBindVertexArray (emptyVao);
        glDrawArrays (GL_TRIANGLES, 0, 3);
    }

    StageComponent::RingLight StageComponent::ringLight (const SharedState& state, float time) const
    {
        // Levels and cadence from the honest ring state (HaloRingRig.swift).
        RingLight r;
        switch (state.ring.kind)
        {
            case RingState::Kind::discovering: r = { 1, 0.60f }; break;
            case RingState::Kind::connected:   r = { 2, 0.20f }; break;
            case RingState::Kind::monitoring:  r = { 2, 0.40f }; break;
            case RingState::Kind::recording:   r = { 2, 0.22f + 0.18f * (1.0f + std::sin (time * juce::MathConstants<float>::twoPi / 3.2f)) }; break;
            case RingState::Kind::transfer:    r = { 2, 0.15f + 0.75f * (float) state.ring.transferProgress }; break;
            case RingState::Kind::error:       r = { 2, 0.40f }; break;
            case RingState::Kind::disconnected: break;
        }
        return r;
    }

    // The floor around the device, drawn after it (the floor hidden under the
    // device fails the depth test).
    // High tier: one pass writing the contact shadow into the floor target;
    //   the pool and key spot are computed analytically in the composite.
    // Low tier: up to three passes over the backdrop: key-light spot (dark look,
    //   screen), contact shadow (alpha toward the tint), ring pool (screen).
    void StageComponent::drawGround (const Mat& viewProj, const SharedState& state, float time, int vw, int vh)
    {
        if (debugView >= 5)
            return;
        const auto ring = ringLight (state, time);
        const auto ringColour = toVec (StageLook::ringColour (look, state.ring.kind));
        const auto tint = toVec (look.shadowTint);

        Mat identity;
        partProgram.setMat4 ("uModel", modelToWorld().mat);
        partProgram.setMat4 ("uPartToModel", identity.mat);
        partProgram.setMat4 ("uViewProj", viewProj.mat);
        partProgram.set ("uKind", 3);
        partProgram.set ("uShadowTint", tint.x, tint.y, tint.z);
        partProgram.set ("uRingMode", ring.mode);
        partProgram.set ("uRingLevel", ring.level);
        partProgram.set ("uRingColour", ringColour.x, ringColour.y, ringColour.z);
        partProgram.set ("uRingPeriod", ringDiscoverPeriod);
        partProgram.set ("uFloorSpot", look.floorSpot);
        partProgram.set ("uTime", time);
        partProgram.set ("uViewport", (float) vw, (float) vh);

        glDepthMask (GL_FALSE);
        glBindVertexArray (groundVao);
        if (targets.isHdr())
        {
            glDisable (GL_BLEND);
            partProgram.set ("uGroundPass", 0);
            glDrawArrays (GL_TRIANGLE_STRIP, 0, 4);
        }
        else
        {
            glEnable (GL_BLEND);
            auto pass = [this] (int which, GLenum src, GLenum dst)
            {
                partProgram.set ("uGroundPass", which);
                glBlendFunc (src, dst);
                glDrawArrays (GL_TRIANGLE_STRIP, 0, 4);
            };
            if (look.floorSpot > 0.0f)
                pass (1, GL_ONE_MINUS_DST_COLOR, GL_ONE);   // screen
            pass (0, GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA); // shadow
            if (ring.mode != 0 && ring.level > 0.0f)
                pass (2, GL_ONE_MINUS_DST_COLOR, GL_ONE);   // screen
            glDisable (GL_BLEND);
        }
        glDepthMask (GL_TRUE);
    }

    void StageComponent::drawModel (const Mat& viewProj, const SharedState& state)
    {
        const auto toWorld = modelToWorld();
        const auto orange = linear (cueOrange);
        const auto green = linear (cueGreen.brighter (0.6f));
        partProgram.setMat4 ("uViewProj", viewProj.mat);

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

            partProgram.setMat4 ("uModel", then (local, toWorld).mat);
            partProgram.setMat4 ("uPartToModel", local.mat);
            partProgram.set ("uKind", kind);
            partProgram.set ("uGlow", glow.x, glow.y, glow.z);

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

       #if JUCE_DEBUG
        // HALO_STAGE_SHADER_DIR: rebuild every program when a source changes.
        if (shaders.fromDisk() && ++framesSinceShaderCheck >= 30)
        {
            framesSinceShaderCheck = 0;
            if (shaders.changed())
            {
                log.line ("shaders: changed on disk, rebuilding");
                buildPrograms();
            }
        }
       #endif

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
        const bool ready = meshLoaded && partProgram.valid() && depthProgram.valid() && w > 0 && h > 0;

        stats.beginFrame();
        renderFrame (state, nowSeconds, dt, w, h, ready);
        stats.endFrame ("continuous");
        if (statsEnabled)
            if (const auto line = stats.poll (nowSeconds, tierName); line.isNotEmpty())
                log.line (line + " targets=" + juce::String (targets.allocatedWidth()) + "x" + juce::String (targets.allocatedHeight())
                          + " allocations=" + juce::String (targets.reallocations()));
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

    void StageComponent::setPartUniforms (const StageView& camera, const Mat& lightViewProj)
    {
        glActiveTexture (GL_TEXTURE1);
        glBindTexture (GL_TEXTURE_2D, shadowTarget.texture);
        glActiveTexture (GL_TEXTURE2);
        glBindTexture (GL_TEXTURE_2D, heightTarget.texture);
        glActiveTexture (GL_TEXTURE3);
        glBindTexture (GL_TEXTURE_2D, printTexture.getTextureID());
        glActiveTexture (GL_TEXTURE0);

        partProgram.use();
        partProgram.set ("uTexture", 0);
        partProgram.set ("uShadowMap", 1);
        partProgram.set ("uHeightMap", 2);
        partProgram.set ("uPrint", 3);
        partProgram.set ("uEye", camera.eye.x, camera.eye.y, camera.eye.z);
        partProgram.setMat4 ("uLightViewProj", lightViewProj.mat);
        partProgram.set ("uShadowRadius", 0.0014f / (2.0f * shadowExtent));
        partProgram.set ("uHeightRect", 0.0f, 0.0f, heightHalfX, heightHalfY);
        partProgram.set ("uHeightTexel", 2.0f * heightHalfX / (float) heightTarget.width);
        partProgram.set ("uDeviceSize", deviceWidth, deviceDepth);
        partProgram.set ("uKeyDir", keyDir.x, keyDir.y, keyDir.z);
        partProgram.set ("uTopDir", topDir.x, topDir.y, topDir.z);
        partProgram.set ("uFillDir", fillDir.x, fillDir.y, fillDir.z);
        partProgram.set ("uExposure", look.exposure);
        partProgram.set ("uToneMapper", (int) look.toneMapper);
        partProgram.set ("uDebug", debugView);
    }

    void StageComponent::renderFrame (const SharedState& state, double nowSeconds, float dt, int w, int h, bool ready)
    {
        GLint mainFramebuffer = 0;
        glGetIntegerv (GL_FRAMEBUFFER_BINDING, &mainFramebuffer);

        if (! ready)
        {
            // Never a black flash: the backdrop token until the stage can draw.
            const auto c = look.backdrop;
            glViewport (0, 0, juce::jmax (1, w), juce::jmax (1, h));
            glClearColor (c.getFloatRed(), c.getFloatGreen(), c.getFloatBlue(), 1.0f);
            glClear (GL_COLOR_BUFFER_BIT);
            return;
        }

        // Shadow and height maps first: both follow the pads as they travel.
        const auto lightViewProj = keyLightViewProj();
        animatePads (state, dt);
        glDisable (GL_BLEND);
        drawDepth (shadowTarget, lightViewProj, true);
        drawDepth (heightTarget, heightProjection (groundZ), false);
        glBindTexture (GL_TEXTURE_2D, heightTarget.texture);
        glGenerateMipmap (GL_TEXTURE_2D); // cone-traced AO reads wide taps from coarse mips
        glBindFramebuffer (GL_FRAMEBUFFER, (GLuint) mainFramebuffer); // the passes below output here

        const auto frame = targets.prepare (w, h, nowSeconds, log);

        // Fit the shadow-casting bounds into the stage at this angle and aspect
        // (6% margin, centred), then apply the user's zoom.
        const auto camera = StageCamera::view (deviceBox, yawDegrees.load(), pitchDegrees.load(), zoom.load(),
                                               (float) w / (float) h, orthoView.load(),
                                               presetOffsetActive.load() ? presetOffset : Vec3f {});
        // A pose freezes the clock too, so two captures of one pose match.
        const float time = pose ? 1.0f : (float) (nowSeconds - startTime);

        if (targets.isHdr())
            renderHigh (state, camera, frame, w, h, time);
        else
            renderLow (state, camera, frame, w, h, time);

        // Back to JUCE's framebuffer and a clean state for the next frame.
        glBindFramebuffer (GL_FRAMEBUFFER, (GLuint) mainFramebuffer);
        glDisable (GL_BLEND);
        glDepthMask (GL_TRUE);
        juce::ignoreUnused (mainFramebuffer);
    }

    // High tier (plan section 3.2): MSAA HDR scene -> custom resolve ->
    // composite (tone map once, floor, exact sRGB, dither) -> optional FXAA.
    void StageComponent::renderHigh (const SharedState& state, const StageView& camera, const RenderTargets::Frame& frame,
                                     int w, int h, float time)
    {
        GLint output = 0;
        glGetIntegerv (GL_FRAMEBUFFER_BINDING, &output);
        juce::ignoreUnused (output);

        // 1. Scene.
        glBindFramebuffer (GL_FRAMEBUFFER, targets.msFbo);
        glViewport (0, 0, frame.width, frame.height);
        const GLenum both[] { GL_COLOR_ATTACHMENT0, GL_COLOR_ATTACHMENT1 };
        glDrawBuffers (2, both);
        const float zero[] { 0.0f, 0.0f, 0.0f, 0.0f };
        glClearBufferfv (GL_COLOR, 0, zero);
        glClearBufferfv (GL_COLOR, 1, zero);
        glDepthMask (GL_TRUE);
        glClearDepth (1.0);
        glClear (GL_DEPTH_BUFFER_BIT);
        glEnable (GL_MULTISAMPLE);

        const Mat viewProj (camera.viewProjection.data());
        if (debugView == 5)
        {
            const GLenum deviceOnly[] { GL_COLOR_ATTACHMENT0, GL_NONE };
            glDrawBuffers (2, deviceOnly);
            drawCard (frame.height);
        }
        else
        {
            glEnable (GL_DEPTH_TEST);
            glDepthFunc (GL_LEQUAL);
            setPartUniforms (camera, keyLightViewProj());
            const GLenum deviceOnly[] { GL_COLOR_ATTACHMENT0, GL_NONE };
            glDrawBuffers (2, deviceOnly);
            drawModel (viewProj, state);
            const GLenum floorOnly[] { GL_NONE, GL_COLOR_ATTACHMENT1 };
            glDrawBuffers (2, floorOnly);
            drawGround (viewProj, state, time, frame.width, frame.height);
        }
        glDisable (GL_DEPTH_TEST);

        // 2. Resolve.
        glBindFramebuffer (GL_FRAMEBUFFER, targets.resolveFbo);
        glDrawBuffers (2, both);
        glViewport (0, 0, frame.width, frame.height);
        resolveProgram.use();
        glActiveTexture (GL_TEXTURE0);
        glBindTexture (GL_TEXTURE_2D_MULTISAMPLE, targets.msDevice);
        glActiveTexture (GL_TEXTURE1);
        glBindTexture (GL_TEXTURE_2D_MULTISAMPLE, targets.msFloor);
        resolveProgram.set ("uDeviceMS", 0);
        resolveProgram.set ("uFloorMS", 1);
        resolveProgram.set ("uSamples", juce::jmax (1, targets.samples()));
        glBindVertexArray (emptyVao);
        glDrawArrays (GL_TRIANGLES, 0, 3);
        glBindTexture (GL_TEXTURE_2D_MULTISAMPLE, 0);
        glActiveTexture (GL_TEXTURE0);
        glBindTexture (GL_TEXTURE_2D_MULTISAMPLE, 0);

        // 3. Composite, to the window (or to the post target when FXAA follows).
        const bool viaFxaa = fxaaEngaged;
        const int outW = viaFxaa ? frame.width : w, outH = viaFxaa ? frame.height : h;
        glBindFramebuffer (GL_FRAMEBUFFER, viaFxaa ? targets.postFbo : (GLuint) output);
        glViewport (0, 0, outW, outH);
        compositeProgram.use();
        glActiveTexture (GL_TEXTURE0);
        glBindTexture (GL_TEXTURE_2D, targets.device);
        glActiveTexture (GL_TEXTURE1);
        glBindTexture (GL_TEXTURE_2D, targets.floor);
        compositeProgram.set ("uDevice", 0);
        compositeProgram.set ("uFloor", 1);
        compositeProgram.set ("uUvScale", viaFxaa ? frame.uvScaleX * (float) frame.width / (float) outW : frame.uvScaleX,
                                          viaFxaa ? frame.uvScaleY * (float) frame.height / (float) outH : frame.uvScaleY);
        compositeProgram.set ("uViewport", (float) outW, (float) outH);
        compositeProgram.set ("uScale", (float) context.getRenderingScale() * (float) outW / (float) w);
        compositeProgram.set ("uCell", look.gridCellPt);
        const auto canvas = toVec (look.floor), minor = toVec (look.gridMinor), major = toVec (look.gridMajor);
        compositeProgram.set ("uCanvas", canvas.x, canvas.y, canvas.z);
        compositeProgram.set ("uMinor", minor.x, minor.y, minor.z, look.gridMinor.getFloatAlpha());
        compositeProgram.set ("uMajor", major.x, major.y, major.z, look.gridMajor.getFloatAlpha());
        const auto tint = toVec (look.shadowTint);
        compositeProgram.set ("uShadowTint", tint.x, tint.y, tint.z);
        compositeProgram.set ("uFloorSpot", look.floorSpot);
        const auto ring = ringLight (state, time);
        const auto ringColour = toVec (StageLook::ringColour (look, state.ring.kind));
        compositeProgram.set ("uRingColour", ringColour.x, ringColour.y, ringColour.z);
        compositeProgram.set ("uRingMode", ring.mode);
        compositeProgram.set ("uRingLevel", ring.level);
        compositeProgram.set ("uRingPeriod", ringDiscoverPeriod);
        compositeProgram.set ("uTime", time);
        compositeProgram.set ("uDeviceSize", deviceWidth, deviceDepth);
        const auto inverse = invert (camera.viewProjection);
        compositeProgram.setMat4 ("uInvViewProj", inverse.data());
        compositeProgram.set ("uFloorY", groundZ);
        compositeProgram.set ("uExposure", look.exposure);
        compositeProgram.set ("uToneMapper", (int) look.toneMapper);
        compositeProgram.set ("uBypass", (debugView != 0 && debugView != 5) ? 1 : 0);
        compositeProgram.set ("uDither", viaFxaa ? 0 : 1);
        glDrawArrays (GL_TRIANGLES, 0, 3);

        // 4. FXAA, then the dither, into the window.
        if (viaFxaa)
        {
            glBindFramebuffer (GL_FRAMEBUFFER, (GLuint) output);
            glViewport (0, 0, w, h);
            fxaaProgram.use();
            glActiveTexture (GL_TEXTURE0);
            glBindTexture (GL_TEXTURE_2D, targets.post);
            fxaaProgram.set ("uImage", 0);
            fxaaProgram.set ("uViewport", (float) w, (float) h);
            fxaaProgram.set ("uUvScale", frame.uvScaleX, frame.uvScaleY);
            fxaaProgram.set ("uDither", 1);
            glDrawArrays (GL_TRIANGLES, 0, 3);
        }
        glBindVertexArray (0);
        glActiveTexture (GL_TEXTURE1);
        glBindTexture (GL_TEXTURE_2D, 0);
        glActiveTexture (GL_TEXTURE0);
        glBindTexture (GL_TEXTURE_2D, 0);
    }

    // Low tier: today's forward path into an LDR target (tone mapped per
    // fragment, exact sRGB), then FXAA and the dither into the window.
    void StageComponent::renderLow (const SharedState& state, const StageView& camera, const RenderTargets::Frame& frame,
                                    int w, int h, float time)
    {
        GLint output = 0;
        glGetIntegerv (GL_FRAMEBUFFER_BINDING, &output);

        glBindFramebuffer (GL_FRAMEBUFFER, targets.sceneFbo);
        glViewport (0, 0, frame.width, frame.height);
        glClearColor (0, 0, 0, 1);
        glDepthMask (GL_TRUE);
        glClear (GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

        if (debugView == 5)
            drawCard (frame.height);
        else
        {
            drawBackground (frame.height);
            const Mat viewProj (camera.viewProjection.data());
            glEnable (GL_DEPTH_TEST);
            glDepthFunc (GL_LEQUAL);
            setPartUniforms (camera, keyLightViewProj());
            drawModel (viewProj, state);
            drawGround (viewProj, state, time, frame.width, frame.height);
            glDisable (GL_DEPTH_TEST);
        }

        glBindFramebuffer (GL_FRAMEBUFFER, (GLuint) output);
        glViewport (0, 0, w, h);
        fxaaProgram.use();
        glActiveTexture (GL_TEXTURE0);
        glBindTexture (GL_TEXTURE_2D, targets.scene);
        fxaaProgram.set ("uImage", 0);
        fxaaProgram.set ("uViewport", (float) w, (float) h);
        fxaaProgram.set ("uUvScale", frame.uvScaleX, frame.uvScaleY);
        fxaaProgram.set ("uDither", 1);
        glBindVertexArray (emptyVao);
        glDrawArrays (GL_TRIANGLES, 0, 3);
        glBindVertexArray (0);
        glBindTexture (GL_TEXTURE_2D, 0);
    }
}
