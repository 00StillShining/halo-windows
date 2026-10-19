#pragma once

#include "App/AppModel.h"
#include "Stage/DisplayRenderer.h"
#include "Stage/HaloMesh.h"

#include <juce_gui_basics/juce_gui_basics.h>
#include <juce_opengl/juce_opengl.h>

#include <array>
#include <atomic>

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
        };

        struct PadAnim
        {
            float depth = 0.0f;
            float led = 0.0f;
            int seenStrikes = 0;
        };

        void drawBackground();
        void drawShadow (const juce::Matrix3D<float>& viewProj);
        void drawModel (const juce::Matrix3D<float>& viewProj, const SharedState&, float dt, float time);

        AppModel& model;
        juce::OpenGLContext context;
        DisplayRenderer displayRenderer;

        // GL-thread state
        HaloMesh mesh;
        std::unique_ptr<juce::OpenGLShaderProgram> partShader, backgroundShader;
        juce::OpenGLTexture displayTexture;
        GLuint emptyVao = 0, shadowVao = 0, shadowVbo = 0;
        std::array<int, 12> padParts {};
        std::array<int, 4> groupParts {};
        int displayPart = -1, ringPart = -1;
        std::array<PadAnim, 12> padAnim {};
        double lastFrameTime = 0.0, startTime = 0.0;
        bool meshLoaded = false;

        // Shared between threads
        juce::SpinLock lock;
        SharedState shared;

        // Camera (message thread writes, GL thread reads)
        std::atomic<float> yawDegrees { 28.0f }, pitchDegrees { 42.0f }, radius { 0.70f };
        float dragStartYaw = 0.0f, dragStartPitch = 0.0f;

        JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (StageComponent)
    };
}
