#pragma once

#include <array>

// The 3D stage's camera (docs/redesign/riddim-3d-plan.md, section 3.6): pure
// maths with no JUCE or GL, so the fit is unit tested.
//
// World space is the stage's: Y up, metres, the device lying in the XZ plane.
// The camera orbits a target at (yaw, pitch); yaw 0 looks from +Z (the player's
// side) and pitch 90 straight down. Matrices are column-major for column
// vectors (OpenGL convention).
namespace halo
{
    struct Vec3f
    {
        float x = 0.0f, y = 0.0f, z = 0.0f;
    };

    struct StageBox
    {
        Vec3f min, max;
        Vec3f centre() const { return { (min.x + max.x) * 0.5f, (min.y + max.y) * 0.5f, (min.z + max.z) * 0.5f }; }
        std::array<Vec3f, 8> corners() const;
    };

    using Mat4f = std::array<float, 16>;

    // General 4x4 inverse (column-major). Returns the identity for a singular matrix.
    Mat4f invert (const Mat4f&);

    struct StageView
    {
        Vec3f eye, target;
        float radius = 1.0f;
        float aspect = 1.0f;
        bool orthographic = false;
        float orthoHalfHeight = 0.1f;
        Mat4f view {}, projection {}, viewProjection {};

        // Normalised device coordinates (x, y in -1..1 inside the viewport; z depth).
        Vec3f project (Vec3f world) const;
    };

    struct StageCamera
    {
        static constexpr float fovYDegrees = 24.0f;
        static constexpr float fitMargin = 0.06f;   // of the viewport, per side
        static constexpr float minZoom = 0.55f, maxZoom = 1.6f;
        static constexpr float minPitch = 12.0f, maxPitch = 85.0f;
        static constexpr float nearPlane = 0.01f, farPlane = 6.0f;

        struct Fit
        {
            Vec3f target;
            float radius = 1.0f;           // perspective
            float orthoHalfHeight = 0.1f;  // orthographic
        };

        // The radius (or ortho half height) and target at which every corner of
        // `box` projects inside the viewport with `margin` to spare on the
        // tighter axis, with the projected box centred.
        static Fit fit (const StageBox& box, float yawDegrees, float pitchDegrees, float aspect,
                        bool orthographic = false, float margin = fitMargin);

        // The full view: fitted, then zoomed (radius x zoom; zoom < 1 is closer),
        // with the target moved by `targetOffset` (world metres) after fitting.
        static StageView view (const StageBox& box, float yawDegrees, float pitchDegrees, float zoom, float aspect,
                               bool orthographic = false, Vec3f targetOffset = {});

        static StageView make (Vec3f target, float yawDegrees, float pitchDegrees, float radius, float aspect,
                               bool orthographic, float orthoHalfHeight);
    };
}
