// The High tier's last scene pass (plan section 3.2, step 5): the device over
// the floor, tone mapped once, through the exact sRGB output curve, dithered.
//
//   device: unpremultiply -> x exposure -> tone map -> re-premultiply
//   floor:  base = the floor token with graph paper receding in perspective
//           (a real plane under the device, found per pixel from the inverse
//           view-projection), lit by the key spot (dark look), blended toward
//           the shadow tint by the contact shadow, then the ring pool
//           screen-blended on top: floor = base + (1 - base) * light. All in
//           display values, exactly as the Low tier blends them, so both tiers
//           match and the pool keeps its warm orange on a cool floor (a
//           display-linear screen of saturated orange reads pink there). No
//           light leaves base exact: the stage border is the token to the bit.
//   out = device over floor -> sRGB -> +-0.5 LSB dither (unless FXAA follows,
//         which dithers after itself).
#include "common.glsl"
#include "floor.glsl"

uniform sampler2D uDevice;    // resolved: premultiplied radiance, a = coverage
uniform sampler2D uFloor;     // resolved: rgb reserved, a = contact shadow
uniform vec2 uUvScale;        // the part of the targets this frame used
uniform vec2 uViewport;       // output size in pixels
uniform vec3 uCanvas;         // floor token, display values
uniform vec4 uMinor, uMajor;  // floor grid lines (display colour, strength)
uniform vec2 uGridMm;         // minor, major spacing on the floor (mm)
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

// The floor is a real plane at the device's lowest point: each pixel's ray
// meets it at a model-space point (world (x, y, z) = model (x, z, -y)).
// Computed for every pixel (so screen derivatives exist everywhere); `hit`
// says whether the ray actually reaches the floor.
vec2 floorPoint (vec2 fragCoord, out bool hit)
{
    vec2 ndc = fragCoord / uViewport * 2.0 - 1.0;
    vec4 a = uInvViewProj * vec4 (ndc, -1.0, 1.0);
    vec4 b = uInvViewProj * vec4 (ndc, 1.0, 1.0);
    vec3 p0 = a.xyz / a.w, p1 = b.xyz / b.w;
    float dy = p1.y - p0.y;
    float t = abs (dy) > 1e-9 ? (uFloorY - p0.y) / dy : -1.0;
    hit = t > 0.0;
    vec3 p = mix (p0, p1, max (t, 0.0));
    return vec2 (p.x, -p.z);
}

// Ben Golus' "pristine grid": lines of a constant width in floor units,
// anti-aliased by their screen derivatives, thinned to coverage once they are
// narrower than a pixel and faded into their average as cells shrink toward
// a pixel, so the receding floor never moires. uv in cells; width in cells.
float pristineGrid (vec2 uv, float lineWidth)
{
    vec4 ddxy = vec4 (dFdx (uv), dFdy (uv));
    vec2 deriv = vec2 (length (ddxy.xz), length (ddxy.yw));
    vec2 target = vec2 (lineWidth);
    // min (max ()), not clamp (): GLSL leaves clamp undefined when the
    // derivative exceeds 0.5 (and NVIDIA returns NaN, a black dot).
    vec2 drawWidth = min (max (target, deriv), vec2 (0.5));
    vec2 aa = max (deriv, vec2 (1e-6)) * 1.5;
    vec2 g = 1.0 - abs (fract (uv) * 2.0 - 1.0);
    vec2 grid = smoothstep (drawWidth + aa, drawWidth - aa, g);
    grid *= clamp (target / drawWidth, 0.0, 1.0);
    grid = mix (grid, target, clamp (deriv * 2.0 - 1.0, 0.0, 1.0));
    return mix (grid.x, 1.0, grid.y);
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
        // Floor: the token, graph paper receding in perspective round the
        // device (fading out before the stage edge, so the border is the token),
        // the key spot, the contact shadow and the ring pool.
        float border = stageBorder (fc, uViewport);
        bool onFloor;
        vec2 modelXY = floorPoint (fc, onFloor);
        vec2 mm = modelXY * 1000.0;
        float minorLine = pristineGrid (mm / uGridMm.x, 0.25 / uGridMm.x);
        float majorLine = pristineGrid (mm / uGridMm.y, 0.45 / uGridMm.y);
        vec3 baseEnc = uCanvas;
        float spot = 0.0, pool = 0.0;
        if (onFloor)
        {
            float paper = (1.0 - smoothstep (0.24, 0.50, length (modelXY))) * border;
            baseEnc = mix (baseEnc, uMinor.rgb, minorLine * uMinor.a * paper);
            baseEnc = mix (baseEnc, uMajor.rgb, majorLine * uMajor.a * paper);
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
    // Alpha must be 1: the window's framebuffer alpha reaches the desktop
    // compositor, and a dark pixel with a low alpha came out black in captures.
    fragColour = vec4 (avoidKeyedColour (encoded), 1.0);
}
