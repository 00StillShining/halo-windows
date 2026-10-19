#include <doctest/doctest.h>

#include "Core/StageCamera.h"

#include <algorithm>
#include <cmath>

using namespace halo;

namespace
{
    // The EP-40's shadow-casting bounds in stage world space (Y up, metres):
    // 176 x 240 mm, 8.3 mm below the face to the knob tops.
    const StageBox ep40 { { -0.088f, -0.0083f, -0.120f }, { 0.088f, 0.0164f, 0.120f } };

    struct Extent { float x0, x1, y0, y1; };

    Extent projected (const StageView& v, const StageBox& box)
    {
        Extent e { 1e9f, -1e9f, 1e9f, -1e9f };
        for (const auto& c : box.corners())
        {
            const auto p = v.project (c);
            e.x0 = std::min (e.x0, p.x); e.x1 = std::max (e.x1, p.x);
            e.y0 = std::min (e.y0, p.y); e.y1 = std::max (e.y1, p.y);
        }
        return e;
    }
}

TEST_CASE ("stage camera: the device fits with at least a 5% margin at every aspect")
{
    struct Angles { float yaw, pitch; };
    const Angles angles[] { { 28, 38 }, { 28, 42 }, { 0, 85 }, { 28, 12 }, { 90, 10 }, { -60, 60 }, { 110, 30 } };
    for (float aspect = 0.6f; aspect <= 2.5001f; aspect += 0.1f)
        for (const auto& a : angles)
        {
            const auto v = StageCamera::view (ep40, a.yaw, a.pitch, 1.0f, aspect);
            const auto e = projected (v, ep40);
            INFO ("aspect " << aspect << " yaw " << a.yaw << " pitch " << a.pitch);
            // 5% of the viewport per side = 0.1 in NDC.
            CHECK (e.x0 >= -0.9f);
            CHECK (e.x1 <= 0.9f);
            CHECK (e.y0 >= -0.9f);
            CHECK (e.y1 <= 0.9f);
            // ...and actually fills the limiting axis (the 6% target margin).
            CHECK (std::max ({ -e.x0, e.x1, -e.y0, e.y1 }) == doctest::Approx (0.88f).epsilon (0.01));
            // Centred.
            CHECK (std::abs (e.x0 + e.x1) < 0.01f);
            CHECK (std::abs (e.y0 + e.y1) < 0.01f);
        }
}

TEST_CASE ("stage camera: orthographic plan view fits and centres")
{
    for (float aspect : { 0.6f, 1.0f, 1.8f, 2.5f })
    {
        const auto v = StageCamera::view (ep40, 0.0f, 90.0f, 1.0f, aspect, true);
        const auto e = projected (v, ep40);
        INFO ("aspect " << aspect);
        CHECK (e.x0 >= -0.9f);
        CHECK (e.x1 <= 0.9f);
        CHECK (e.y0 >= -0.9f);
        CHECK (e.y1 <= 0.9f);
        CHECK (std::max ({ -e.x0, e.x1, -e.y0, e.y1 }) == doctest::Approx (0.88f).epsilon (0.01));
        // Plan view, ports at the top: the far edge (world -Z) projects upwards.
        CHECK (v.project ({ 0, 0, -0.12f }).y > v.project ({ 0, 0, 0.12f }).y);
    }
}

TEST_CASE ("stage camera: zoom scales the fitted radius and is clamped")
{
    const auto fit = StageCamera::fit (ep40, 28, 38, 1.6f);
    CHECK (StageCamera::view (ep40, 28, 38, 1.0f, 1.6f).radius == doctest::Approx (fit.radius));
    CHECK (StageCamera::view (ep40, 28, 38, 0.1f, 1.6f).radius == doctest::Approx (fit.radius * StageCamera::minZoom));
    CHECK (StageCamera::view (ep40, 28, 38, 9.0f, 1.6f).radius == doctest::Approx (fit.radius * StageCamera::maxZoom));
}

TEST_CASE ("stage camera: yaw 0 looks from the player's side, pitch from above")
{
    const auto v = StageCamera::view (ep40, 0.0f, 30.0f, 1.0f, 1.5f);
    CHECK (v.eye.z > v.target.z);
    CHECK (v.eye.y > v.target.y);
    CHECK (std::abs (v.eye.x - v.target.x) < 1e-4f);
}
