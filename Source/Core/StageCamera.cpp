#include "Core/StageCamera.h"

#include <algorithm>
#include <cmath>

namespace halo
{
    namespace
    {
        constexpr float pi = 3.14159265358979f;
        float radians (float d) { return d * pi / 180.0f; }

        Vec3f operator+ (Vec3f a, Vec3f b) { return { a.x + b.x, a.y + b.y, a.z + b.z }; }
        Vec3f operator- (Vec3f a, Vec3f b) { return { a.x - b.x, a.y - b.y, a.z - b.z }; }
        Vec3f operator* (Vec3f a, float s) { return { a.x * s, a.y * s, a.z * s }; }
        float dot (Vec3f a, Vec3f b) { return a.x * b.x + a.y * b.y + a.z * b.z; }
        Vec3f cross (Vec3f a, Vec3f b) { return { a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x }; }
        Vec3f normalised (Vec3f a)
        {
            const float l = std::sqrt (dot (a, a));
            return l > 0.0f ? a * (1.0f / l) : a;
        }

        Mat4f multiply (const Mat4f& a, const Mat4f& b) // a * b, column-major
        {
            Mat4f m {};
            for (int c = 0; c < 4; ++c)
                for (int r = 0; r < 4; ++r)
                {
                    float s = 0.0f;
                    for (int k = 0; k < 4; ++k)
                        s += a[(size_t) (k * 4 + r)] * b[(size_t) (c * 4 + k)];
                    m[(size_t) (c * 4 + r)] = s;
                }
            return m;
        }

        // Direction from the target to the eye.
        Vec3f orbitDirection (float yawDegrees, float pitchDegrees)
        {
            const float yaw = radians (yawDegrees), pitch = radians (pitchDegrees);
            return { std::cos (pitch) * std::sin (yaw), std::sin (pitch), std::cos (pitch) * std::cos (yaw) };
        }

        // The camera basis; near-vertical views take "up" from the yaw instead.
        void basis (float yawDegrees, float pitchDegrees, Vec3f& right, Vec3f& up, Vec3f& back)
        {
            back = orbitDirection (yawDegrees, pitchDegrees);
            const float yaw = radians (yawDegrees);
            const Vec3f worldUp = pitchDegrees > 89.0f ? Vec3f { -std::sin (yaw), 0.0f, -std::cos (yaw) } : Vec3f { 0, 1, 0 };
            right = normalised (cross (worldUp, back));
            up = cross (back, right);
        }
    }

    Mat4f invert (const Mat4f& m)
    {
        // Cofactor expansion (as in MESA's gluInvertMatrix); layout-agnostic.
        Mat4f inv {};
        inv[0] = m[5] * m[10] * m[15] - m[5] * m[11] * m[14] - m[9] * m[6] * m[15] + m[9] * m[7] * m[14] + m[13] * m[6] * m[11] - m[13] * m[7] * m[10];
        inv[4] = -m[4] * m[10] * m[15] + m[4] * m[11] * m[14] + m[8] * m[6] * m[15] - m[8] * m[7] * m[14] - m[12] * m[6] * m[11] + m[12] * m[7] * m[10];
        inv[8] = m[4] * m[9] * m[15] - m[4] * m[11] * m[13] - m[8] * m[5] * m[15] + m[8] * m[7] * m[13] + m[12] * m[5] * m[11] - m[12] * m[7] * m[9];
        inv[12] = -m[4] * m[9] * m[14] + m[4] * m[10] * m[13] + m[8] * m[5] * m[14] - m[8] * m[6] * m[13] - m[12] * m[5] * m[10] + m[12] * m[6] * m[9];
        inv[1] = -m[1] * m[10] * m[15] + m[1] * m[11] * m[14] + m[9] * m[2] * m[15] - m[9] * m[3] * m[14] - m[13] * m[2] * m[11] + m[13] * m[3] * m[10];
        inv[5] = m[0] * m[10] * m[15] - m[0] * m[11] * m[14] - m[8] * m[2] * m[15] + m[8] * m[3] * m[14] + m[12] * m[2] * m[11] - m[12] * m[3] * m[10];
        inv[9] = -m[0] * m[9] * m[15] + m[0] * m[11] * m[13] + m[8] * m[1] * m[15] - m[8] * m[3] * m[13] - m[12] * m[1] * m[11] + m[12] * m[3] * m[9];
        inv[13] = m[0] * m[9] * m[14] - m[0] * m[10] * m[13] - m[8] * m[1] * m[14] + m[8] * m[2] * m[13] + m[12] * m[1] * m[10] - m[12] * m[2] * m[9];
        inv[2] = m[1] * m[6] * m[15] - m[1] * m[7] * m[14] - m[5] * m[2] * m[15] + m[5] * m[3] * m[14] + m[13] * m[2] * m[7] - m[13] * m[3] * m[6];
        inv[6] = -m[0] * m[6] * m[15] + m[0] * m[7] * m[14] + m[4] * m[2] * m[15] - m[4] * m[3] * m[14] - m[12] * m[2] * m[7] + m[12] * m[3] * m[6];
        inv[10] = m[0] * m[5] * m[15] - m[0] * m[7] * m[13] - m[4] * m[1] * m[15] + m[4] * m[3] * m[13] + m[12] * m[1] * m[7] - m[12] * m[3] * m[5];
        inv[14] = -m[0] * m[5] * m[14] + m[0] * m[6] * m[13] + m[4] * m[1] * m[14] - m[4] * m[2] * m[13] - m[12] * m[1] * m[6] + m[12] * m[2] * m[5];
        inv[3] = -m[1] * m[6] * m[11] + m[1] * m[7] * m[10] + m[5] * m[2] * m[11] - m[5] * m[3] * m[10] - m[9] * m[2] * m[7] + m[9] * m[3] * m[6];
        inv[7] = m[0] * m[6] * m[11] - m[0] * m[7] * m[10] - m[4] * m[2] * m[11] + m[4] * m[3] * m[10] + m[8] * m[2] * m[7] - m[8] * m[3] * m[6];
        inv[11] = -m[0] * m[5] * m[11] + m[0] * m[7] * m[9] + m[4] * m[1] * m[11] - m[4] * m[3] * m[9] - m[8] * m[1] * m[7] + m[8] * m[3] * m[5];
        inv[15] = m[0] * m[5] * m[10] - m[0] * m[6] * m[9] - m[4] * m[1] * m[10] + m[4] * m[2] * m[9] + m[8] * m[1] * m[6] - m[8] * m[2] * m[5];

        const double det = (double) m[0] * inv[0] + (double) m[1] * inv[4] + (double) m[2] * inv[8] + (double) m[3] * inv[12];
        if (std::abs (det) < 1e-30)
            return { 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1 };
        for (auto& v : inv)
            v = (float) (v / det);
        return inv;
    }

    std::array<Vec3f, 8> StageBox::corners() const
    {
        return { Vec3f { min.x, min.y, min.z }, Vec3f { max.x, min.y, min.z }, Vec3f { min.x, max.y, min.z },
                 Vec3f { max.x, max.y, min.z }, Vec3f { min.x, min.y, max.z }, Vec3f { max.x, min.y, max.z },
                 Vec3f { min.x, max.y, max.z }, Vec3f { max.x, max.y, max.z } };
    }

    Vec3f StageView::project (Vec3f p) const
    {
        const auto& m = viewProjection;
        const float x = m[0] * p.x + m[4] * p.y + m[8] * p.z + m[12];
        const float y = m[1] * p.x + m[5] * p.y + m[9] * p.z + m[13];
        const float z = m[2] * p.x + m[6] * p.y + m[10] * p.z + m[14];
        const float w = m[3] * p.x + m[7] * p.y + m[11] * p.z + m[15];
        return { x / w, y / w, z / w };
    }

    StageView StageCamera::make (Vec3f target, float yawDegrees, float pitchDegrees, float radius, float aspect,
                                 bool orthographic, float orthoHalfHeight)
    {
        StageView v;
        v.target = target;
        v.radius = radius;
        v.aspect = aspect;
        v.orthographic = orthographic;
        v.orthoHalfHeight = orthoHalfHeight;

        Vec3f s, u, b;
        basis (yawDegrees, pitchDegrees, s, u, b);
        // An orthographic eye just has to sit outside the scene.
        v.eye = target + b * (orthographic ? 1.0f : radius);
        const Vec3f f = b * -1.0f;
        v.view = { s.x, u.x, -f.x, 0, s.y, u.y, -f.y, 0, s.z, u.z, -f.z, 0,
                   -dot (s, v.eye), -dot (u, v.eye), dot (f, v.eye), 1 };

        Mat4f p {};
        if (orthographic)
        {
            const float zn = 0.01f, zf = 3.0f;
            p[0] = 1.0f / (orthoHalfHeight * aspect);
            p[5] = 1.0f / orthoHalfHeight;
            p[10] = -2.0f / (zf - zn);
            p[14] = -(zf + zn) / (zf - zn);
            p[15] = 1.0f;
        }
        else
        {
            const float t = 1.0f / std::tan (radians (fovYDegrees) * 0.5f);
            p[0] = t / aspect;
            p[5] = t;
            p[10] = (farPlane + nearPlane) / (nearPlane - farPlane);
            p[11] = -1.0f;
            p[14] = 2.0f * farPlane * nearPlane / (nearPlane - farPlane);
        }
        v.projection = p;
        v.viewProjection = multiply (p, v.view);
        return v;
    }

    StageCamera::Fit StageCamera::fit (const StageBox& box, float yawDegrees, float pitchDegrees, float aspect,
                                       bool orthographic, float margin)
    {
        const float limit = 1.0f - 2.0f * margin; // NDC half extent the box may use
        const auto corners = box.corners();
        Vec3f s, u, b;
        basis (yawDegrees, pitchDegrees, s, u, b);

        Fit result;
        result.target = box.centre();

        if (orthographic)
        {
            // Centre the box's view-plane extent, then size to the tighter axis.
            float x0 = 1e9f, x1 = -1e9f, y0 = 1e9f, y1 = -1e9f;
            for (const auto& c : corners)
            {
                const auto d = c - result.target;
                x0 = std::min (x0, dot (d, s)); x1 = std::max (x1, dot (d, s));
                y0 = std::min (y0, dot (d, u)); y1 = std::max (y1, dot (d, u));
            }
            result.target = result.target + s * ((x0 + x1) * 0.5f) + u * ((y0 + y1) * 0.5f);
            const float halfW = (x1 - x0) * 0.5f, halfH = (y1 - y0) * 0.5f;
            result.orthoHalfHeight = std::max (halfH, halfW / aspect) / limit;
            return result;
        }

        auto extent = [&] (const StageView& v, float& cx, float& cy)
        {
            float x0 = 1e9f, x1 = -1e9f, y0 = 1e9f, y1 = -1e9f;
            for (const auto& c : corners)
            {
                const auto p = v.project (c);
                x0 = std::min (x0, p.x); x1 = std::max (x1, p.x);
                y0 = std::min (y0, p.y); y1 = std::max (y1, p.y);
            }
            cx = (x0 + x1) * 0.5f;
            cy = (y0 + y1) * 0.5f;
            return std::max ({ -x0, x1, -y0, y1 });
        };

        // The box must stay in front of the near plane: never closer than its
        // own bounding radius.
        const auto half = (box.max - box.min) * 0.5f;
        const float boundRadius = std::sqrt (dot (half, half));
        const float tanHalf = std::tan (radians (fovYDegrees) * 0.5f);

        for (int pass = 0; pass < 6; ++pass)
        {
            float lo = boundRadius * 1.05f + nearPlane, hi = 20.0f;
            for (int i = 0; i < 48; ++i)
            {
                const float mid = 0.5f * (lo + hi);
                float cx = 0, cy = 0;
                if (extent (make (result.target, yawDegrees, pitchDegrees, mid, aspect, false, 0), cx, cy) > limit)
                    lo = mid;
                else
                    hi = mid;
            }
            result.radius = hi;

            // Re-centre: move the target so the projected box is centred.
            float cx = 0, cy = 0;
            extent (make (result.target, yawDegrees, pitchDegrees, result.radius, aspect, false, 0), cx, cy);
            if (std::abs (cx) < 1e-4f && std::abs (cy) < 1e-4f)
                break;
            const float halfH = result.radius * tanHalf;
            result.target = result.target + s * (cx * halfH * aspect) + u * (cy * halfH);
        }
        return result;
    }

    StageView StageCamera::view (const StageBox& box, float yawDegrees, float pitchDegrees, float zoom, float aspect,
                                 bool orthographic, Vec3f targetOffset)
    {
        zoom = std::clamp (zoom, minZoom, maxZoom);
        const auto f = fit (box, yawDegrees, pitchDegrees, aspect, orthographic);
        return make (f.target + targetOffset, yawDegrees, pitchDegrees, f.radius * zoom, aspect, orthographic,
                     f.orthoHalfHeight * zoom);
    }
}
