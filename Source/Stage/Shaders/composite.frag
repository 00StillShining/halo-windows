// The High tier's last scene pass (plan section 3.2, step 5): the device over
// the floor, tone mapped once, through the exact sRGB output curve, dithered.
//
//   device: unpremultiply -> x exposure -> tone map -> re-premultiply
//   floor:  base = backdrop, lit by the key spot (dark look), blended toward
//           the shadow tint by the contact shadow, then the ring pool
//           screen-blended on top: floor = base + (1 - base) * light. All in
//           display values, exactly as the Low tier blends them, so both tiers
//           match and the pool keeps its warm orange on a cool floor (a
//           display-linear screen of saturated orange reads pink there). No
//           light leaves base exact: the stage border is the token to the bit.
//   out = device over floor -> sRGB -> +-0.5 LSB dither (unless FXAA follows,
//         which dithers after itself).
#include "common.glsl"
#include "backdrop.glsl"
#include "floor.glsl"

uniform sampler2D uDevice;    // resolved: premultiplied radiance, a = coverage
uniform sampler2D uFloor;     // resolved: rgb reserved, a = contact shadow
uniform vec2 uUvScale;        // the part of the targets this frame used
uniform vec2 uViewport;       // output size in pixels
uniform float uScale;         // pixels per point (backdrop grid)
uniform float uCell;
uniform vec3 uCanvas;         // backdrop, display values
uniform vec4 uMinor, uMajor;
uniform vec3 uShadowTint;     // display value
uniform float uFloorSpot;     // display-encoded key spot strength (dark look)
uniform vec3 uRingColour;     // display value
uniform int uRingMode;
uniform float uRingLevel, uRingPeriod, uTime;
uniform vec2 uDeviceSize;     // model metres
uniform mat4 uInvViewProj;    // world from clip
uniform float uFloorY;        // world height of the floor
uniform float uExposure;
uniform int uToneMapper;
uniform int uBypass;          // debug views: device values straight to the output curve
uniform int uDither;          // 0 when FXAA follows (it dithers)
out vec4 fragColour;

// The model-space floor point under this pixel, or false if the ray misses.
bool floorPoint (vec2 fragCoord, out vec2 modelXY)
{
    vec2 ndc = fragCoord / uViewport * 2.0 - 1.0;
    vec4 a = uInvViewProj * vec4 (ndc, -1.0, 1.0);
    vec4 b = uInvViewProj * vec4 (ndc, 1.0, 1.0);
    vec3 p0 = a.xyz / a.w, p1 = b.xyz / b.w;
    float dy = p1.y - p0.y;
    if (abs (dy) < 1e-9)
        return false;
    float t = (uFloorY - p0.y) / dy;
    if (t < 0.0)
        return false;
    vec3 hit = mix (p0, p1, t);
    modelXY = vec2 (hit.x, -hit.z); // world (x, y, z) = model (x, z, -y)
    return true;
}

void main()
{
    vec2 fc = gl_FragCoord.xy;
    vec2 uv = fc / uViewport * uUvScale;
    vec4 device = texture (uDevice, uv);
    vec4 floorTerms = texture (uFloor, uv);

    vec3 outLinear;
    if (uBypass != 0)
    {
        outLinear = device.rgb;
    }
    else
    {
        // Floor.
        vec3 baseEnc = backdropSrgb (fc, uViewport.y, uScale, uCell, uCanvas, uMinor, uMajor);
        float border = stageBorder (fc, uViewport);
        vec2 modelXY;
        float spot = 0.0, pool = 0.0;
        if (floorPoint (fc, modelXY))
        {
            float gap = footprintGap (modelXY, uDeviceSize);
            spot = floorSpot (gap, uFloorSpot) * border;
            pool = ringPool (modelXY, uDeviceSize, gap, uRingMode, uRingLevel, uTime, uRingPeriod) * border;
        }
        // The contact shadow blends toward the tint in display values, the
        // response the shadow terms were tuned for (a linear multiply reads
        // far weaker in the penumbra).
        vec3 litEnc = mix (baseEnc + (1.0 - baseEnc) * spot, uShadowTint, floorTerms.a);
        litEnc += (1.0 - litEnc) * uRingColour * pool;
        vec3 floorColour = srgbDecode (litEnc);

        // Device over floor.
        vec3 deviceColour = vec3 (0.0);
        if (device.a > 0.0)
            deviceColour = tonemap (device.rgb / device.a * uExposure, uToneMapper) * device.a;
        outLinear = deviceColour + floorColour * (1.0 - device.a);
    }

    vec3 encoded = srgbEncode (outLinear);
    if (uDither != 0)
        encoded += (interleavedNoise (fc) - 0.5) * (0.9 / 255.0); // +-0.45 LSB: never moves an exact 8-bit value
    fragColour = vec4 (encoded, luma (encoded)); // alpha: luma for FXAA
}
