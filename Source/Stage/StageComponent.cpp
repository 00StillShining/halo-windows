#include "Stage/StageComponent.h"
#include "Core/Ep40MidiMapping.h"
#include "Core/ToneCurves.h"

#include <HaloStageAssets.h>

using namespace juce::gl;

namespace halo
{
    namespace
    {
        // Mechanics from HaloMechanics.swift.
        constexpr float keyTravel = 0.0013f;            // metres: one travel for every strike (O5 pending)
        // Aperture underglow (D3): HDR orange x (0.4 + 0.6 velocity) on the
        // struck pad's liner, fading over ~150 ms (3 tau) after release.
        constexpr float glowFloor = 0.4f, glowScale = 0.6f, glowDecayTau = 0.05f, glowHdr = 3.0f;
        constexpr float ringDiscoverPeriod = 4.0f;

        // Halo cue colours on the device itself (pad strike, active group). Device
        // facts, not look tokens: the riddim orange and green (plan section 2.5).
        const juce::Colour cueOrange (0xffFA5A16), cueGreen (0xff0A4A36);

        // Device and lighting rig (model space is Z-up metres; world is Y-up).
        constexpr float deviceWidth = 0.176f, deviceDepth = 0.240f;
        constexpr int shadowMapSize = 2048;
        constexpr float shadowExtent = 0.20f; // half-size of the key light's ortho box
        // The key light's direction until the studio environment supplies it.
        const juce::Vector3D<float> defaultKeyDir = juce::Vector3D<float> (-0.45f, 1.0f, 0.55f).normalised();

        // Attribute and output names, bound by index (HaloMesh's vertex layout).
        const std::initializer_list<const char*> partAttributes { "aPos", "aNormal", "aUV", "aBytes" };

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

        Mat keyLightViewProj (juce::Vector3D<float> keyDir)
        {
            const auto view = lookAt (keyDir * 0.6f, { 0, 0, 0 }, { 0, 1, 0 });
            return then (view, orthographic (shadowExtent, shadowExtent, 0.05f, 1.2f));
        }

        juce::Vector3D<float> toVec (juce::Colour c) { return { c.getFloatRed(), c.getFloatGreen(), c.getFloatBlue() }; }

        juce::Vector3D<float> linear (juce::Colour c)
        {
            return { tone::srgbDecode (c.getFloatRed()), tone::srgbDecode (c.getFloatGreen()), tone::srgbDecode (c.getFloatBlue()) };
        }
    }

    //==============================================================================
    StageComponent::StageComponent (AppModel& m) : model (m)
    {
        setOpaque (true);
        keyDirection = defaultKeyDir;

        // The stage owns its anti-aliasing (MSAA in its own HDR targets, or FXAA),
        // so JUCE's window framebuffer asks for none.
        juce::OpenGLPixelFormat format;
        format.multisamplingLevel = 0;
        context.setPixelFormat (format);
        context.setMultisamplingEnabled (false);
        context.setOpenGLVersionRequired (juce::OpenGLContext::openGL3_2);
        context.setRenderer (this);
        context.setComponentPaintingEnabled (false);
        // Frames only on demand (D13); HALO_STAGE_CONTINUOUS=1 restores the old loop.
        onDemand = juce::SystemStats::getEnvironmentVariable ("HALO_STAGE_CONTINUOUS", {}) != "1";
        context.setContinuousRepainting (! onDemand);

        // Dev hooks for repeatable screenshots (riddim-3d-plan.md, section 8).
        // HALO_STAGE_CAMERA=yaw,pitch,zoom; HALO_STAGE_PRESET=hero|top|low|keys|edge;
        // HALO_STAGE_DEBUG=1..4 view AO, key shadow, height map or floor terms,
        // 5 the material swatch card, 6 the silhouette mask, 7 unlit albedo,
        // 8 emissive only. HALO_STAGE_POSE freezes input (Core/StagePose.h).
        // HALO_STAGE_TIER=high|low|still, HALO_STAGE_TONEMAP=neutral|agx|aces,
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

        // HALO_STAGE_GLASS=sheen[,roughness[,peak]]: tune (or with 0, remove)
        // the screen acrylic's reflection.
        if (const auto glass = juce::StringArray::fromTokens (env ("HALO_STAGE_GLASS"), ",", {}); glass.size() > 0)
        {
            glassSheen = glass[0].getFloatValue();
            if (glass.size() > 1) glassRoughness = glass[1].getFloatValue();
            if (glass.size() > 2) glassPeak = glass[2].getFloatValue();
            log.line ("glass: sheen " + juce::String (glassSheen) + " roughness " + juce::String (glassRoughness)
                      + " peak " + juce::String (glassPeak));
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
        if (tierRequest == "still")
            enterStillTier ("requested");
        else
        {
            context.attachTo (*this);
            if (onDemand)
                startTimerHz (30);
        }
    }

    void StageComponent::setLook (const StageLook& newLook)
    {
        {
            const juce::SpinLock::ScopedLockType sl (lock);
            shared.look = newLook;
            shared.lookDirty = true;
        }
        requestFrame ("look");
    }

    void StageComponent::requestFrame (const char* reason)
    {
        if (stillTier)
        {
            repaint();
            return;
        }
        const double now = juce::Time::getMillisecondCounterHiRes() * 0.001;
        double until = wakeUntil.load();
        while (until < now + 0.6 && ! wakeUntil.compare_exchange_weak (until, now + 0.6)) {}
        frameReason = reason;
        context.triggerRepaint();
    }

    void StageComponent::timerCallback()
    {
        const std::array<float, 4> camera { yawDegrees.load(), pitchDegrees.load(), zoom.load(),
                                            orthoView.load() ? 1.0f : 0.0f };
        if (camera != lastCamera)
        {
            lastCamera = camera;
            requestFrame ("camera");
        }
    }

    void StageComponent::enterStillTier (const juce::String& why)
    {
        if (stillTier.exchange (true))
            return;
        log.line ("tier: still (" + why + ")");
        stopTimer();
        if (context.isAttached())
            context.detach();
        planImage = juce::ImageCache::getFromMemory (HaloStageAssets::ep40_plan_png, HaloStageAssets::ep40_plan_pngSize);
        setOpaque (true);
        repaint();
    }

    void StageComponent::paint (juce::Graphics& g)
    {
        if (! stillTier)
            return;
        StageLook currentLook;
        juce::Image lcd;
        std::array<bool, 12> held {};
        {
            const juce::SpinLock::ScopedLockType sl (lock);
            currentLook = shared.look;
            lcd = shared.displayImage;
            for (size_t i = 0; i < held.size(); ++i)
                held[i] = shared.pads[i].held;
        }
        g.fillAll (currentLook.backdrop);
        // The device's plan, 176 x 240 mm, fitted with a margin.
        const auto area = getLocalBounds().toFloat().reduced (getHeight() * 0.06f);
        const float scale = juce::jmin (area.getWidth() / 176.0f, area.getHeight() / 240.0f);
        const juce::Rectangle<float> device (area.getCentreX() - 88.0f * scale, area.getCentreY() - 120.0f * scale,
                                             176.0f * scale, 240.0f * scale);
        auto mm = [&] (float x0, float y0, float x1, float y1)
        {
            return juce::Rectangle<float> (device.getX() + x0 * scale, device.getY() + y0 * scale, (x1 - x0) * scale, (y1 - y0) * scale);
        };
        g.setImageResamplingQuality (juce::Graphics::highResamplingQuality);
        if (planImage.isValid())
            g.drawImage (planImage, device);
        // The live LCD in its window (the screen's reference rectangle).
        g.setColour (juce::Colour (0xff0b5e40));
        g.fillRect (mm (0.0f, 56.5f, 176.0f, 104.0f));
        if (lcd.isValid())
            g.drawImage (lcd, mm (2.65f, 59.72f, 172.65f, 102.19f));
        // Pad cues (D3): the struck pads' apertures glow.
        static constexpr float columnLeft[] { 55.7f, 79.7f, 103.6f }, rowTop[] { 136.3f, 160.3f, 184.2f, 208.2f };
        for (size_t i = 0; i < held.size(); ++i)
            if (held[i])
            {
                const auto cell = mm (columnLeft[i % 3], rowTop[i / 3], columnLeft[i % 3] + 16.0f, rowTop[i / 3] + 16.0f);
                g.setColour (cueOrange.withAlpha (0.35f));
                g.drawRoundedRectangle (cell.expanded (0.8f * scale), 2.0f * scale, 1.6f * scale);
                g.setColour (cueOrange);
                g.drawRoundedRectangle (cell, 1.5f * scale, 0.7f * scale);
            }
        g.setColour (currentLook.backdrop.contrasting (0.55f));
        g.setFont (juce::FontOptions (12.0f));
        g.drawText ("3D view unavailable on this GPU: plan view", getLocalBounds().removeFromBottom (22), juce::Justification::centred);
    }

    void StageComponent::resized()
    {
        requestFrame ("resize");
    }

    // GL thread: whether the scene is still changing on its own.
    bool StageComponent::stillMoving (const SharedState& state) const
    {
        for (size_t i = 0; i < padAnim.size(); ++i)
        {
            const float target = state.pads[i].held ? keyTravel : 0.0f;
            if (! padAnim[i].spring.settled (target) || padAnim[i].glow > 0.003f)
                return true;
        }
        // The ring's own animation: the recording pulse and the discovering
        // comet both move with time (ringLight / floor.glsl ringPool mode 1).
        return state.ring.kind == RingState::Kind::recording
            || state.ring.kind == RingState::Kind::discovering;
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
        stopTimer();
        model.removeListener (this);
        context.detach();
    }

    //==============================================================================
    void StageComponent::statusChanged()
    {
        if (pose)
            return;
        requestFrame ("status");
        const juce::SpinLock::ScopedLockType sl (lock);
        shared.ring = model.ring();
    }

    void StageComponent::displayChanged (const DisplayState& state)
    {
        if (pose)
            return;
        requestFrame ("display");
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
        requestFrame ("pad");
    }

    void StageComponent::allPadsReleased()
    {
        if (pose)
            return;
        const juce::SpinLock::ScopedLockType sl (lock);
        for (auto& pad : shared.pads)
            pad.held = false;
        requestFrame ("pad");
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
        if (onCameraChanged) onCameraChanged(); // UI redesign camera API
    }

    void StageComponent::mouseDoubleClick (const juce::MouseEvent&)
    {
        yawDegrees = heroYaw;
        pitchDegrees = heroPitch;
        zoom = 1.0f;
        orthoView = false;
        presetOffsetActive = false;
        if (onCameraChanged) onCameraChanged(); // UI redesign camera API
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
                && depthProgram.build (shaders, log, "depth", "depth.vert", "depth.frag", true, partAttributes, { "fragColour" })
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
                && depthProgram.build (shaders, log, "depth", "depth.vert", "depth.frag", false, partAttributes, { "fragColour" })
                && backgroundProgram.build (shaders, log, "background", "fullscreen.vert", "background.frag", false, {}, { "fragColour" })
                && cardProgram.build (shaders, log, "card (LDR)", "fullscreen.vert", "card.frag", false, {}, { "fragColour" })
                && fxaaProgram.build (shaders, log, "fxaa", "fullscreen.vert", "fxaa.frag", false, {}, { "fragColour" });
            if (! ok)
            {
                log.line ("tier: low tier failed too; the stage cannot draw");
                tierName = "none";
                juce::MessageManager::callAsync ([safe = juce::Component::SafePointer<StageComponent> (this)]
                {
                    if (safe != nullptr)
                        safe->enterStillTier ("no GL tier could build");
                });
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

   #if JUCE_DEBUG
    // KHR_debug (M11): count GL errors and high-severity messages; log the first few.
    static void KHRONOS_APIENTRY onGlDebugMessage (GLenum, GLenum type, GLuint, GLenum severity, GLsizei,
                                                   const GLchar* message, const void* user)
    {
        if (type != GL_DEBUG_TYPE_ERROR && severity != GL_DEBUG_SEVERITY_HIGH)
            return;
        auto* stage = static_cast<StageComponent*> (const_cast<void*> (user));
        stage->noteGlDebugError (message);
    }

    void StageComponent::noteGlDebugError (const char* message)
    {
        if (++glDebugErrors <= 5)
            log.line ("gl debug: " + juce::String::fromUTF8 (message != nullptr ? message : "(no message)"));
    }
   #endif

    void StageComponent::newOpenGLContextCreated()
    {
       #if JUCE_DEBUG
        if (glDebugMessageCallback != nullptr
            && (juce::OpenGLHelpers::isExtensionSupported ("GL_KHR_debug") || juce::OpenGLHelpers::isExtensionSupported ("GL_ARB_debug_output")))
        {
            glEnable (GL_DEBUG_OUTPUT);
            glEnable (GL_DEBUG_OUTPUT_SYNCHRONOUS);
            glDebugMessageCallback (onGlDebugMessage, this);
            log.line ("gl debug: KHR_debug callback on");
        }
       #endif
        {
            std::string error;
            meshLoaded = mesh.parse (HaloStageAssets::EP40_hmesh, (size_t) HaloStageAssets::EP40_hmeshSize, error);
            if (! meshLoaded)
                log.line ("mesh: EP40.hmesh rejected: " + juce::String (error));
        }
        if (meshLoaded)
            mesh.upload();
        {
            std::string error;
            if (! keysAtlas.upload (HaloStageAssets::ep40_keys_sdf_htex, (size_t) HaloStageAssets::ep40_keys_sdf_htexSize, error))
                log.line ("decals: ep40_keys_sdf.htex rejected: " + juce::String (error));
            if (! faceAtlas.upload (HaloStageAssets::ep40_face_sdf_htex, (size_t) HaloStageAssets::ep40_face_sdf_htexSize, error))
                log.line ("decals: ep40_face_sdf.htex rejected: " + juce::String (error));
            if (! deckAO.upload (HaloStageAssets::ep40_deck_ao_htex, (size_t) HaloStageAssets::ep40_deck_ao_htexSize, error))
                log.line ("ao: ep40_deck_ao.htex rejected: " + juce::String (error));
        }

        // Slots: the pads in grid order, then the group pads (the converter and
        // the legacy loader assign them by name). Each turns about its own pivot.
        slotPivots.fill ({});
        for (const auto& part : mesh.model.parts)
            if (part.slot != 0 && part.slot < (int) slotPivots.size())
                slotPivots[part.slot] = { part.pivot[0], part.pivot[1], part.pivot[2] };

        // Bounds of the parts that cast shadows (the anchors, like halo_ring,
        // do not): the floor sits at their lowest point, and the camera fits
        // their box, knobs included.
        {
            const auto& lo = mesh.model.boundsMin;
            const auto& hi = mesh.model.boundsMax;
            if (lo[0] < hi[0])
            {
                groundZ = lo[2];
                // Model (x, y, z) -> world (x, z, -y).
                deviceBox = { { lo[0], lo[2], -hi[1] }, { hi[0], hi[2], -lo[1] } };
            }
            log.line ("bounds (mm): x " + juce::String (lo[0] * 1000.0f, 2) + ".." + juce::String (hi[0] * 1000.0f, 2)
                      + " y " + juce::String (lo[1] * 1000.0f, 2) + ".." + juce::String (hi[1] * 1000.0f, 2)
                      + " z " + juce::String (lo[2] * 1000.0f, 2) + ".." + juce::String (hi[2] * 1000.0f, 2)
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
            log.line ("mesh: .hmesh v" + juce::String ((int) mesh.model.version) + ", " + juce::String ((int) mesh.model.parts.size())
                      + " parts, " + juce::String ((int) mesh.model.materials.size()) + " materials, "
                      + juce::String ((int) (mesh.model.indices.size() / 3)) + " triangles");
        }
        stats.create();
        stats.tracksLookups = true;
        if (statsEnabled)
            log.line (juce::String ("stats: GPU timer ") + (stats.hasTimer() ? "available" : "unavailable"));

        glGenVertexArrays (1, &emptyVao);
        buildPrograms();

        // The studio: reflections, ambient SH, and the key the shadow map follows.
        if (environment.create (log))
        {
            const auto& d = environment.key().direction;
            keyDirection = juce::Vector3D<float> (d[0], d[1], d[2]).normalised();
        }
        environment.setFloor (look.floor);

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
                const auto* part = mesh.model.find (e.part);
                if (part == nullptr)
                    continue;
                std::map<int, double> area;
                const auto& vs = mesh.model.vertices;
                const auto& is = mesh.model.indices;
                for (size_t t = part->firstIndex; t + 2 < (size_t) part->firstIndex + part->indexCount; t += 3)
                {
                    const auto& a = vs[is[t]];
                    const auto& b = vs[is[t + 1]];
                    const auto& c = vs[is[t + 2]];
                    const juce::Vector3D<float> ab (b.position[0] - a.position[0], b.position[1] - a.position[1], b.position[2] - a.position[2]);
                    const juce::Vector3D<float> ac (c.position[0] - a.position[0], c.position[1] - a.position[1], c.position[2] - a.position[2]);
                    area[a.material] += 0.5 * (double) (ab ^ ac).length();
                }
                auto best = std::max_element (area.begin(), area.end(),
                                              [] (const auto& x, const auto& y) { return x.second < y.second; });
                if (best == area.end())
                    continue;
                const auto& base = mesh.model.materials[(size_t) best->first].base;
                swatches.push_back ({ e.name, { base[0], base[1], base[2] } });
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
        glEnableVertexAttribArray (2);
        glVertexAttribPointer (2, 2, GL_FLOAT, GL_FALSE, 5 * sizeof (float), (const void*) (3 * sizeof (float)));
        glDisableVertexAttribArray (1); glVertexAttrib2f (1, 0, 0);       // octahedral (0, 0) = +Z
        glDisableVertexAttribArray (3); glVertexAttrib4f (3, 0, 0, 0, 255); // material 0, slot 0 (static)
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
                // The same depth as a plain float, read unfiltered by the soft
                // shadow's blocker search (a depth texture in compare mode cannot be).
                target.depthCopy = texture (GL_R32F, GL_RED, GL_FLOAT);
                glTexParameteri (GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
                glTexParameteri (GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
                glFramebufferTexture2D (GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, target.depthCopy, 0);
                glDrawBuffer (GL_COLOR_ATTACHMENT0);
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
        depthMapsValid = false;
        makeTarget (shadowTarget, shadowMapSize, shadowMapSize, true);


       #if JUCE_DEBUG
        runToneSelfTest();
       #endif

        startTime = lastFrameTime = juce::Time::getMillisecondCounterHiRes() * 0.001;
    }

    void StageComponent::openGLContextClosing()
    {
       #if JUCE_DEBUG
        if (glDebugMessageCallback != nullptr)
            glDebugMessageCallback (nullptr, nullptr);
       #endif
        mesh.release();
        keysAtlas.release();
        displayTexture.release();
        faceAtlas.release();
        deckAO.release();
        releasePrograms();
        targets.release();
        environment.release();
        stats.release();
        for (auto* target : { &shadowTarget })
        {
            if (target->fbo != 0) glDeleteFramebuffers (1, &target->fbo);
            if (target->texture != 0) glDeleteTextures (1, &target->texture);
            if (target->depthCopy != 0) glDeleteTextures (1, &target->depthCopy);
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
                anim.glow = glowFloor + glowScale * target.velocity;
            }
            const float depth = target.held ? keyTravel : 0.0f;
            if (pose)
            {
                anim.spring = { depth, 0.0f }; // frozen: the pose shows the settled state
                continue;
            }
            anim.spring.step (depth, dt);
            if (! target.held)
                anim.glow *= std::exp (-dt / glowDecayTau);
        }

        // Pad travel as slot offsets (model space, along -Z).
        slotOffsets.fill ({});
        for (size_t i = 0; i < 12; ++i)
            slotOffsets[1 + i] = { 0.0f, 0.0f, -padAnim[i].spring.depth };
    }

    // Depth-only pass of the whole device into `target`. The key light's map works
    // in world space; the top-down height map works directly in model space.
    void StageComponent::drawDepth (const DepthTarget& target, const Mat& viewProj, bool partsInWorld)
    {
        glBindFramebuffer (GL_FRAMEBUFFER, target.fbo);
        glViewport (0, 0, target.width, target.height);
        glDepthMask (GL_TRUE);
        glClearDepth (1.0);
        glClearColor (1.0f, 1.0f, 1.0f, 1.0f); // the far plane: no blocker
        glClear (GL_DEPTH_BUFFER_BIT | GL_COLOR_BUFFER_BIT);
        glEnable (GL_DEPTH_TEST);
        glDepthFunc (GL_LESS);
        glEnable (GL_POLYGON_OFFSET_FILL);
        glPolygonOffset (1.1f, 2.0f);

        depthProgram.use();
        depthProgram.setMat4 ("uViewProj", viewProj.mat);
        setSlotUniforms (depthProgram);
        const Mat identity;
        depthProgram.setMat4 ("uModel", partsInWorld ? modelToWorld().mat : identity.mat);
        ++stats.geometryDraws;
        mesh.draw();
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

    void StageComponent::drawModel (const Mat& viewProj, const SharedState& state, bool mirror)
    {
        const auto toWorld = modelToWorld();
        const auto orange = linear (cueOrange);
        const auto green = linear (cueGreen.brighter (0.6f));
        partProgram.setMat4 ("uViewProj", viewProj.mat);

        // Halo's pad cue (D3): the struck pad's aperture liner glows (by the
        // well's slot, part.frag); no cap ever changes colour. The active group
        // shows on the LCD's group icon, not as a tint on the pad.
        std::array<juce::Vector3D<float>, Hmesh::maxSlots> glow {};
        for (size_t i = 0; i < 12; ++i)
            glow[1 + i] = orange * (padAnim[i].glow * glowHdr);
        juce::ignoreUnused (green, state);
        static const auto glowNames = []
        {
            std::array<std::string, Hmesh::maxSlots> names;
            for (size_t i = 0; i < names.size(); ++i)
                names[i] = "uSlotGlow[" + std::to_string (i) + "]";
            return names;
        }();
        for (size_t i = 0; i < glow.size(); ++i)
            partProgram.set (glowNames[i].c_str(), glow[i].x, glow[i].y, glow[i].z);

        // One draw: the LCD and the printed deck are told apart by their
        // materials' flags, moving parts by their slots.
        glActiveTexture (GL_TEXTURE0);
        if (displayTexture.getTextureID() != 0)
            displayTexture.bind();
        if (mirror)
        {
            // Reflect world y about the floor (y = groundZ): y' = 2 groundZ - y.
            float m[16] { 1, 0, 0, 0, 0, -1, 0, 0, 0, 0, 1, 0, 0, 2.0f * groundZ, 0, 1 };
            partProgram.setMat4 ("uModel", then (toWorld, Mat (m)).mat);
        }
        else
            partProgram.setMat4 ("uModel", toWorld.mat);
        partProgram.set ("uMirror", mirror ? look.floorReflection : 0.0f);
        partProgram.set ("uMirrorFloorY", groundZ);
        partProgram.set ("uKind", 0);
        setSlotUniforms (partProgram);
        ++stats.geometryDraws;
        mesh.draw();

        // The screen acrylic (D5), after everything opaque: it adds its
        // reflection and lets the LCD through (rgb added, alpha = transmittance),
        // leaving depth and the device mask's alpha alone.
        if (mesh.hasGlass() && debugView == 0 && ! mirror)
        {
            glEnable (GL_BLEND);
            // Low tier: display values, so a screen blend (src x (1 - dst)) that
            // rolls the sheen off over bright icons instead of clipping them.
            if (tierName == "low")
                glBlendFuncSeparate (GL_ONE_MINUS_DST_COLOR, GL_SRC_ALPHA, GL_ZERO, GL_ONE);
            else
                glBlendFuncSeparate (GL_ONE, GL_SRC_ALPHA, GL_ZERO, GL_ONE);
            glDepthMask (GL_FALSE);
            ++stats.geometryDraws;
            mesh.drawGlass();
            glDepthMask (GL_TRUE);
            glDisable (GL_BLEND);
        }
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
                lookChanged = true;
            }
        }
        if (state.displayDirty && state.displayImage.isValid())
            displayTexture.loadImage (state.displayImage);
        if (lookChanged)
        {
            environment.setFloor (look.floor); // the floor bounce follows the floor token, no rebake
            lookChanged = false;
        }

        const auto scale = (float) context.getRenderingScale();
        const int w = juce::roundToInt (scale * (float) getWidth());
        const int h = juce::roundToInt (scale * (float) getHeight());
        const bool ready = meshLoaded && partProgram.valid() && depthProgram.valid() && w > 0 && h > 0;

        stats.beginFrame();
        renderFrame (state, nowSeconds, dt, w, h, ready);
        stats.endFrame (onDemand ? frameReason.load() : "continuous");
        // On demand: keep drawing while the scene moves, while the stage has
        // not yet settled at this size (HALO_STAGE_READY), or briefly after a
        // request; otherwise stop until the next change asks.
        if (onDemand && (! readyLogged || nowSeconds < wakeUntil.load() || stillMoving (state)))
        {
            if (stillMoving (state))
                frameReason = "motion";
            context.triggerRepaint();
        }

       #if JUCE_DEBUG
        // HALO_STAGE_READBACK=<file.png>: the stage's own pixels, read straight
        // from its framebuffer once it is ready (no window capture in between).
        if (ready && readyLogged && ! readbackDone)
        {
            readbackDone = true;
            if (const auto path = juce::SystemStats::getEnvironmentVariable ("HALO_STAGE_READBACK", {}); path.isNotEmpty())
            {
                std::vector<juce::uint8> pixels ((size_t) (w * h * 4));
                glPixelStorei (GL_PACK_ALIGNMENT, 1);
                glReadPixels (0, 0, w, h, GL_RGBA, GL_UNSIGNED_BYTE, pixels.data());
                juce::Image image (juce::Image::RGB, w, h, false);
                for (int y = 0; y < h; ++y)
                    for (int x = 0; x < w; ++x)
                    {
                        const auto* p = &pixels[(size_t) (((h - 1 - y) * w + x) * 4)];
                        image.setPixelAt (x, y, juce::Colour (p[0], p[1], p[2]));
                    }
                juce::File file (path);
                file.deleteFile();
                if (juce::FileOutputStream out (file); out.openedOk())
                    juce::PNGImageFormat().writeImageToStream (image, out);
                int translucent = 0;
                for (size_t i = 3; i < pixels.size(); i += 4)
                    translucent += pixels[i] != 255 ? 1 : 0;
                log.line ("readback: " + path + " (" + juce::String (translucent) + " pixels with alpha < 255)");
            }
        }
       #endif
        if (statsEnabled)
            if (const auto line = stats.poll (nowSeconds, tierName); line.isNotEmpty())
                log.line (line + " targets=" + juce::String (targets.allocatedWidth()) + "x" + juce::String (targets.allocatedHeight())
                          + " allocations=" + juce::String (targets.reallocations())
                          + " gl_errors=" + juce::String (glDebugErrors.load()));
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

    void StageComponent::setSlotUniforms (GlProgram& program)
    {
        static const auto names = []
        {
            std::array<std::array<std::string, 3>, Hmesh::maxSlots> n;
            for (size_t i = 0; i < n.size(); ++i)
                n[i] = { "uSlotOffset[" + std::to_string (i) + "]", "uSlotRot[" + std::to_string (i) + "]",
                         "uSlotPivot[" + std::to_string (i) + "]" };
            return n;
        }();
        for (size_t i = 0; i < names.size(); ++i)
        {
            program.set (names[i][0].c_str(), slotOffsets[i].x, slotOffsets[i].y, slotOffsets[i].z);
            program.set (names[i][1].c_str(), 0.0f, 0.0f, 0.0f, 1.0f); // pads travel straight down: no rotation yet
            program.set (names[i][2].c_str(), slotPivots[i].x, slotPivots[i].y, slotPivots[i].z);
        }
    }

    void StageComponent::setPartUniforms (const StageView& camera, const Mat& lightViewProj)
    {
        glActiveTexture (GL_TEXTURE1);
        glBindTexture (GL_TEXTURE_2D, shadowTarget.texture);
        glActiveTexture (GL_TEXTURE2);
        glBindTexture (GL_TEXTURE_2D, deckAO.texture);
        glActiveTexture (GL_TEXTURE3);
        glBindTexture (GL_TEXTURE_2D, faceAtlas.texture);
        glActiveTexture (GL_TEXTURE0);

        partProgram.use();
        partProgram.set ("uTexture", 0);
        partProgram.set ("uShadowMap", 1);
        partProgram.set ("uDeckAO", 2);
        partProgram.set ("uDeckAOOn", deckAO.valid() ? 1 : 0);
        partProgram.set ("uFaceAtlas", 3);
        partProgram.set ("uFaceAtlasOn", faceAtlas.valid() ? 1 : 0);
        // tools/model/build_ep40.py FACE_REGION: the whole top, port edge to front edge, reference mm.
        partProgram.set ("uFaceRect", 0.0f, 0.0f, 176.0f, 240.0f);
        partProgram.set ("uEye", camera.eye.x, camera.eye.y, camera.eye.z);
        partProgram.setMat4 ("uLightViewProj", lightViewProj.mat);
        partProgram.set ("uShadowRadius", 0.0014f / (2.0f * shadowExtent));
        partProgram.set ("uDeviceSize", deviceWidth, deviceDepth);
        glActiveTexture (GL_TEXTURE4);
        glBindTexture (GL_TEXTURE_CUBE_MAP, environment.cubeMap());
        glActiveTexture (GL_TEXTURE5);
        glBindTexture (GL_TEXTURE_2D, environment.brdfLut());
        glActiveTexture (GL_TEXTURE0);
        glActiveTexture (GL_TEXTURE6);
        glBindTexture (GL_TEXTURE_2D, mesh.materials);
        glActiveTexture (GL_TEXTURE7);
        glBindTexture (GL_TEXTURE_2D, keysAtlas.texture);
        glActiveTexture (GL_TEXTURE8);
        glBindTexture (GL_TEXTURE_2D, shadowTarget.depthCopy);
        glActiveTexture (GL_TEXTURE0);
        partProgram.set ("uMaterials", 6);
        partProgram.set ("uKeyAtlas", 7);
        partProgram.set ("uShadowDepth", 8);
        partProgram.set ("uShadowDepthRange", 1.2f - 0.05f);  // keyLightViewProj's near..far (m)
        partProgram.set ("uShadowTexel", 2.0f * shadowExtent);   // metres across the map
        partProgram.set ("uKeyAtlasOn", keysAtlas.valid() ? 1 : 0);
        partProgram.set ("uEnv", 4);
        partProgram.set ("uBrdfLut", 5);
        partProgram.set ("uEnvMaxMip", environment.maxMip());
        static const char* shNames[9] { "uSH[0]", "uSH[1]", "uSH[2]", "uSH[3]", "uSH[4]", "uSH[5]", "uSH[6]", "uSH[7]", "uSH[8]" };
        const auto& sh = environment.irradianceSH();
        for (size_t i = 0; i < 9; ++i)
            partProgram.set (shNames[i], sh[i][0] * look.fillIntensity, sh[i][1] * look.fillIntensity, sh[i][2] * look.fillIntensity);
        const auto& key = environment.key();
        const auto& rim = environment.rim();
        partProgram.set ("uKeyDir", keyDirection.x, keyDirection.y, keyDirection.z);
        partProgram.set ("uRimDir", rim.direction[0], rim.direction[1], rim.direction[2]);
        partProgram.set ("uKeyE", key.irradiance[0] * look.keyIntensity, key.irradiance[1] * look.keyIntensity, key.irradiance[2] * look.keyIntensity);
        partProgram.set ("uRimE", rim.irradiance[0] * look.rimIntensity, rim.irradiance[1] * look.rimIntensity, rim.irradiance[2] * look.rimIntensity);
        partProgram.set ("uKeyRadius", key.angularRadius);
        partProgram.set ("uSpecular", 1.0f);
        partProgram.set ("uGlassSheen", glassSheen);
        partProgram.set ("uGlassRoughness", glassRoughness);
        partProgram.set ("uGlassPeak", glassPeak);
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
        const auto lightViewProj = keyLightViewProj (keyDirection);
        animatePads (state, dt);
        glDisable (GL_BLEND);
        std::array<float, 3 + 3 * Hmesh::maxSlots> inputs {};
        inputs[0] = keyDirection.x; inputs[1] = keyDirection.y; inputs[2] = keyDirection.z;
        for (size_t i = 0; i < slotOffsets.size(); ++i)
        {
            inputs[3 + 3 * i] = slotOffsets[i].x;
            inputs[4 + 3 * i] = slotOffsets[i].y;
            inputs[5 + 3 * i] = slotOffsets[i].z;
        }
        if (! depthMapsValid || inputs != depthInputs)
        {
            drawDepth (shadowTarget, lightViewProj, true);
            depthInputs = inputs;
            depthMapsValid = true;
            ++stats.depthRenders;
        }
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
            setPartUniforms (camera, keyLightViewProj (keyDirection));
            const GLenum deviceOnly[] { GL_COLOR_ATTACHMENT0, GL_NONE };
            glDrawBuffers (2, deviceOnly);
            drawModel (viewProj, state);
            const GLenum floorOnly[] { GL_NONE, GL_COLOR_ATTACHMENT1 };
            glDrawBuffers (2, floorOnly);
            // The floor reflection (dark look): the mirrored device into the floor
            // target's rgb, behind the real device in depth; the ground then
            // writes only its contact-shadow alpha over it.
            const bool reflect = look.floorReflection > 0.0f && debugView == 0;
            if (reflect)
            {
                glFrontFace (GL_CW); // mirrored: keep gl_FrontFacing meaning outside
                drawModel (viewProj, state, true);
                glFrontFace (GL_CCW);
                partProgram.set ("uMirror", 0.0f);
                glColorMaski (1, GL_FALSE, GL_FALSE, GL_FALSE, GL_TRUE);
            }
            drawGround (viewProj, state, time, frame.width, frame.height);
            if (reflect)
                glColorMaski (1, GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
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
        compositeProgram.set ("uGridMm", look.floorGridMm, look.floorGridMm * 5.0f);
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
            setPartUniforms (camera, keyLightViewProj (keyDirection));
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
