#pragma once

#include "App/AppModel.h"
#include "Core/StageCamera.h"
#include "Core/StagePose.h"
#include "Stage/DisplayRenderer.h"
#include "Stage/HaloMesh.h"
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
    // latest display image, the ring state) under a spin lock. The GL thread
    // animates toward them, so pad travel and LED decay stay smooth however
    // bursty the MIDI is.
    class StageComponent : public juce::Component,
                           private juce::OpenGLRenderer,
                           private AppModel::Listener
    {
    public:
        explicit StageComponent (AppModel&);
        ~StageComponent() override;

        void mouseDown (const juce::MouseEvent&) override;
        void mouseDrag (const juce::MouseEvent&) override;
        void mouseDoubleClick (const juce::MouseEvent&) override;
        void mouseWheelMove (const juce::MouseEvent&, const juce::MouseWheelDetails&) override;

        // The stage's surroundings (backdrop, floor, shadow, ring pool). Copied
        // under the lock; the GL thread picks it up on its next frame.
        void setLook (const StageLook&);

    private:
        // OpenGLRenderer (GL thread)
        void newOpenGLContextCreated() override;
        void renderOpenGL() override;
        void openGLContextClosing() override;

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
            RingState ring;
            juce::Image displayImage;
            bool displayDirty = false;
            StageLook look = StageLook::paper();
            bool lookDirty = true;
        };

        struct PadAnim
        {
            float depth = 0.0f;
            float led = 0.0f;
            int seenStrikes = 0;
        };

        struct DepthTarget
        {
            GLuint fbo = 0, texture = 0, depthBuffer = 0;
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
        void drawCard();
        void renderFrame (const SharedState&, double nowSeconds, float dt, int width, int height, bool ready);
        void noteFrameForReady (int width, int height);
        void applyPose (const StagePose&);
        void drawDepth (const DepthTarget&, const juce::Matrix3D<float>& viewProj, bool partsInWorld);
        void drawBackground();
        void drawGround (const juce::Matrix3D<float>& viewProj, const SharedState&, float time);
        void drawModel (const juce::Matrix3D<float>& viewProj, const SharedState&, float time);

        AppModel& model;
        juce::OpenGLContext context;
        DisplayRenderer displayRenderer;

        // GL-thread state
        HaloMesh mesh;
        std::unique_ptr<juce::OpenGLShaderProgram> partShader, depthShader, backgroundShader, cardShader;
        juce::OpenGLTexture displayTexture, printTexture;
        DepthTarget shadowTarget, heightTarget;
        GLuint emptyVao = 0, groundVao = 0, groundVbo = 0;
        std::vector<juce::Matrix3D<float>> partToModel; // this frame, pad travel applied
        std::array<int, 12> padParts {};
        std::array<int, 4> groupParts {};
        int displayPart = -1, ringPart = -1, platePart = -1;
        StageLook look = StageLook::paper();   // GL thread's copy
        float groundZ = -0.008f;               // model z of the floor: lowest shadow-casting point
        StageBox deviceBox;                    // world-space bounds of the shadow-casting parts
        std::array<PadAnim, 12> padAnim {};
        double lastFrameTime = 0.0, startTime = 0.0;
        bool meshLoaded = false;
        int debugView = 0; // HALO_STAGE_DEBUG
        std::vector<Swatch> swatches;

        // Dev hooks and diagnostics (docs/redesign/riddim-3d-plan.md, section 8).
        StageLog log;
        StageStats stats;
        bool statsEnabled = false;          // HALO_STAGE_STATS=1
        juce::String tierName { "high" };   // HALO_STAGE_TIER (only "high" exists before M2)
        std::optional<StagePose> pose;      // HALO_STAGE_POSE: frozen input and clock
        int readyWidth = 0, readyHeight = 0, framesAtSize = 0;
        double sizeSince = 0.0;
        bool readyLogged = false;

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

        JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (StageComponent)
    };
}
