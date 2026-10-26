#pragma once

#include "App/AppModel.h"
#include "Core/StageCamera.h"
#include "Core/PadSpring.h"
#include "Core/StagePose.h"
#include "Stage/DecalAtlas.h"
#include "Stage/DeviceColourway.h"
#include "Stage/DisplayRenderer.h"
#include "Stage/Environment.h"
#include "Stage/GlProgram.h"
#include "Stage/HaloMesh.h"
#include "Stage/RenderTargets.h"
#include "Stage/ShaderLibrary.h"
#include "Stage/StageLog.h"
#include "Stage/StageLook.h"
#include "Stage/StageStats.h"

#include <juce_gui_basics/juce_gui_basics.h>
#include <juce_opengl/juce_opengl.h>

#include <array>
#include <atomic>
#include <vector>

namespace halo
{
    // The hero 3D EP-40 (port of EP40StageView / EP40SceneController /
    // KeyTravelAnimator / HaloRingRig). OpenGL 3.2, rendered on JUCE's GL thread.
    //
    // The message thread only writes small "targets" (which pads are held, the
    // latest display image) under a spin lock. The GL thread
    // animates toward them, so pad travel and LED decay stay smooth however
    // bursty the MIDI is.
    class StageComponent : public juce::Component,
                           private juce::OpenGLRenderer,
                           private AppModel::Listener,
                           private juce::Timer
    {
    public:
        explicit StageComponent (AppModel&);
        ~StageComponent() override;

        void mouseDown (const juce::MouseEvent&) override;
        void mouseDrag (const juce::MouseEvent&) override;
        void mouseUp (const juce::MouseEvent&) override;
        void mouseDoubleClick (const juce::MouseEvent&) override;
        void mouseWheelMove (const juce::MouseEvent&, const juce::MouseWheelDetails&) override;
        void resized() override;
        void paint (juce::Graphics&) override; // the Still tier only (M11)

        // The stage's surroundings (backdrop, floor, shadow). Copied
        // under the lock; the GL thread picks it up on its next frame.
        void setLook (const StageLook&);

        // The device's colourway (Stage/DeviceColourway.h): TE's original or
        // one matching the app. Applied live, no re-bake.
        void setColourway (const DeviceColourway&);

        // ---- UI redesign camera API (plan R5: the ORBIT encoder). Additive and
        // owned by the UI work; the stage's own drag / wheel handling is unchanged.
        static constexpr float orbitMinYaw = -60.0f, orbitMaxYaw = 110.0f;
        float orbitYaw() const noexcept { return yawDegrees.load(); }
        float orbitPitch() const noexcept { return pitchDegrees.load(); }
        void setOrbit (float yaw, float pitch)
        {
            yawDegrees = juce::jlimit (orbitMinYaw, orbitMaxYaw, yaw);
            pitchDegrees = juce::jlimit (StageCamera::minPitch, StageCamera::maxPitch, pitch);
            orthoView = false;
            if (onCameraChanged) onCameraChanged();
        }
        void resetOrbit()
        {
            yawDegrees = heroYaw;
            pitchDegrees = heroPitch;
            zoom = 1.0f;
            orthoView = false;
            presetOffsetActive = false;
            if (onCameraChanged) onCameraChanged();
        }
        float heroOrbitYaw() const noexcept { return heroYaw; }
        float heroOrbitPitch() const noexcept { return heroPitch; }
        std::function<void()> onCameraChanged; // message thread: a drag, a reset or setOrbit moved the view
        // ---- end of the UI redesign camera API

    private:
        // OpenGLRenderer (GL thread)
        void newOpenGLContextCreated() override;
        void renderOpenGL() override;
        void openGLContextClosing() override;

        // On-demand repaint (D13, M10): every change asks for frames through
        // this funnel, with its reason; the GL thread keeps drawing only while
        // something moves (a pad settling, a glow fading, a recording pulse)
        // or for 0.6 s after a request. Idle costs no frames.
        void requestFrame (const char* reason);
        // Still tier (M11): when no GL tier can run (or HALO_STAGE_TIER=still),
        // the GL context is detached and the stage paints TE's plan view with
        // the live LCD image and the pad cues over it. Never a blank stage.
        void enterStillTier (const juce::String& why);
        std::atomic<bool> stillTier { false };
        juce::Image planImage;
    public:
        void noteGlDebugError (const char* message); // GL thread, Debug: from the KHR_debug callback
    private:
        void timerCallback() override; // message thread: notices camera moves (drag, wheel, ORBIT API)
        bool onDemand = true;          // HALO_STAGE_CONTINUOUS=1 turns it off
        std::atomic<double> wakeUntil { 0.0 };
        std::atomic<const char*> frameReason { "startup" };
        std::array<float, 4> lastCamera {};

        // AppModel::Listener (message thread)
        void statusChanged() override;
        void displayChanged (const DisplayState&) override;
        void padTransition (const PadHoldRegistry::Transition&) override;
        void allPadsReleased() override;

        struct PadTarget
        {
            bool held = false;
            float velocity = 0.0f;
            int strikeCount = 0; // bumps on every press/restrike so the LED re-flashes
        };

        struct SharedState
        {
            std::array<PadTarget, 12> pads {};
            std::optional<int> activeGroup;
            bool displayLive = false;
            juce::Image displayImage;
            bool displayDirty = false;
            StageLook look = StageLook::paper();
            bool lookDirty = true;
            DeviceColourway colourway;
            bool colourwayDirty = true;
        };

        struct PadAnim
        {
            PadSpring spring;      // travel (Core/PadSpring.h)
            float glow = 0.0f;     // aperture underglow level (D3)
            int seenStrikes = 0;
        };

        struct DepthTarget
        {
            GLuint fbo = 0, texture = 0, depthBuffer = 0, depthCopy = 0; // depthCopy: R32F, for the blocker search
            int width = 0, height = 0;
        };

        // A material swatch for the HALO_STAGE_DEBUG=5 card: the dominant
        // albedo (by area) of one named part.
        struct Swatch
        {
            juce::String name;
            juce::Vector3D<float> linear;
        };

        void animatePads (const SharedState&, float dt);
        bool stillMoving (const SharedState&) const; // GL thread: on-demand repaint
        void renderFrame (const SharedState&, double nowSeconds, float dt, int width, int height, bool ready);
        void renderHigh (const SharedState&, const StageView&, const RenderTargets::Frame&, int width, int height);
        void renderLow (const SharedState&, const StageView&, const RenderTargets::Frame&, int width, int height);
        void noteFrameForReady (int width, int height);
        void applyPose (const StagePose&);
        bool buildPrograms();
        void releasePrograms();
        void runToneSelfTest();
        void drawDepth (const DepthTarget&, const juce::Matrix3D<float>& viewProj, bool partsInWorld);
        void drawBackground (int height);
        void drawCard (int height);
        void setPartUniforms (const StageView&, const juce::Matrix3D<float>& lightViewProj);
        void drawGround (const juce::Matrix3D<float>& viewProj, int viewportWidth, int viewportHeight);
        // mirror: the floor reflection pass (M8): the device mirrored in the floor
        // plane, written to the floor target's rgb; glass is skipped.
        void drawModel (const juce::Matrix3D<float>& viewProj, const SharedState&, bool mirror = false);
        void setSlotUniforms (GlProgram&);

        AppModel& model;
        StageLog log; // first: other members log while they are built
        juce::OpenGLContext context;
        DisplayRenderer displayRenderer;

        // GL-thread state
        HaloMesh mesh;
        DecalAtlas keysAtlas; // the keys' legends (four-ink SDF)
        DecalAtlas faceAtlas; // the deck's silkscreen (green and orange SDF over FaceRegion)
        DecalAtlas deckAO;    // baked AO of the top surfaces, model XY (tools/model/ep40/ao.py)
        ShaderLibrary shaders { log };
        GlProgram partProgram, depthProgram, backgroundProgram, cardProgram;
        GlProgram resolveProgram, compositeProgram, fxaaProgram, selfTestProgram;
        RenderTargets targets;
        Environment environment;
        juce::Vector3D<float> keyDirection { -0.45f, 1.0f, 0.55f }; // from the studio; the shadow map follows it
        bool lookChanged = false;
        // The screen acrylic's sheen: scale, roughness, soft ceiling (linear luma).
        float glassSheen = 0.55f, glassRoughness = 0.05f, glassPeak = 0.13f;
        bool fxaaEngaged = false;
        int framesSinceShaderCheck = 0;
        juce::OpenGLTexture displayTexture;
        DepthTarget shadowTarget;
        // The shadow map depends only on the key direction and the slots'
        // offsets (the device is static in model space, the camera is not in
        // it), so it is re-rendered only when those change.
        std::array<float, 3 + 3 * Hmesh::maxSlots> depthInputs {};
        bool depthMapsValid = false;
        GLuint emptyVao = 0, groundVao = 0, groundVbo = 0;
        // Per-slot motion (slot 0 static, 1-12 pads, 13-16 groups), this frame.
        std::array<juce::Vector3D<float>, Hmesh::maxSlots> slotOffsets {}, slotPivots {};
        StageLook look = StageLook::paper();   // GL thread's copy
        DeviceColourway colourway;             // GL thread's copy
        float groundZ = -0.008f;               // model z of the floor: lowest shadow-casting point
        StageBox deviceBox;                    // world-space bounds of the shadow-casting parts
        std::array<PadAnim, 12> padAnim {};
        double lastFrameTime = 0.0, startTime = 0.0;
        bool meshLoaded = false;
        int debugView = 0; // HALO_STAGE_DEBUG
        std::vector<Swatch> swatches;

        // Dev hooks and diagnostics (docs/redesign/riddim-3d-plan.md, section 8).
        StageStats stats;
        bool statsEnabled = false;          // HALO_STAGE_STATS=1
        juce::String tierName { "high" };   // the tier in use: high (HDR) or low (LDR forward)
        juce::String tierRequest;           // HALO_STAGE_TIER
        bool forceFxaa = false;             // HALO_STAGE_FXAA=1 (Debug)
        std::optional<StagePose> pose;      // HALO_STAGE_POSE: frozen input and clock
        int readyWidth = 0, readyHeight = 0, framesAtSize = 0;
        double sizeSince = 0.0;
        bool readyLogged = false, readbackDone = false;
        std::atomic<int> glDebugErrors { 0 };   // Debug: KHR_debug errors and high-severity messages (M11)

        // Shared between threads
        juce::SpinLock lock;
        SharedState shared;

        // Camera (message thread writes, GL thread reads). The radius comes from
        // fitting the device into the stage (Core/StageCamera); zoom scales it.
        static constexpr float heroYaw = 28.0f, heroPitch = 38.0f;
        std::atomic<float> yawDegrees { heroYaw }, pitchDegrees { heroPitch }, zoom { 1.0f };
        std::atomic<bool> orthoView { false };
        Vec3f presetOffset; // world metres, set once by HALO_STAGE_PRESET before the GL thread starts
        std::atomic<bool> presetOffsetActive { true };
        float dragStartYaw = 0.0f, dragStartPitch = 0.0f;
        // Orbit inertia (M10): a released drag glides on and eases to a stop.
        // Any other camera change (the ORBIT API, a reset, a new drag) stops it.
        float dragYawRate = 0.0f, dragPitchRate = 0.0f;   // degrees per second, smoothed
        double lastDragTime = 0.0;
        float lastDragYaw = 0.0f, lastDragPitch = 0.0f;
        float flingYaw = 0.0f, flingPitch = 0.0f;         // degrees per second while gliding
        float flingWroteYaw = 0.0f, flingWrotePitch = 0.0f;
        double lastTimerTime = 0.0;

        JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (StageComponent)
    };
}
