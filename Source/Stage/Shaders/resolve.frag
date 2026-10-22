// Custom MSAA resolve (plan section 3.2).
//
// Device: each sample is weighted by 1 / (1 + luma), so one very bright
// sample cannot swamp an edge pixel and bright edges stay anti-aliased after
// tone mapping (Karis). Radiance is premultiplied; alpha is coverage.
// Floor: averaged over only the samples the device does not cover, so the
// contact shadow under an edge is not diluted by the "no floor" the device
// samples carry (which would draw a light rim round the silhouette).
#include "common.glsl"

uniform sampler2DMS uDeviceMS;
uniform sampler2DMS uFloorMS;
uniform int uSamples;
out vec4 deviceOut;
out vec4 floorResolved;

void main()
{
    ivec2 p = ivec2 (gl_FragCoord.xy);
    vec4 device = vec4 (0.0), floorSum = vec4 (0.0);
    float weights = 0.0, floorWeights = 0.0;
    for (int i = 0; i < uSamples; ++i)
    {
        vec4 d = texelFetch (uDeviceMS, p, i);
        float w = 1.0 / (1.0 + luma (d.rgb / max (d.a, 1e-4)));
        w = mix (1.0, w, d.a);
        device += d * w;
        weights += w;

        float open = 1.0 - d.a;
        floorSum += texelFetch (uFloorMS, p, i) * open;
        floorWeights += open;
    }
    deviceOut = device / max (weights, 1e-6);
    floorResolved = floorWeights > 0.0 ? floorSum / floorWeights : vec4 (0.0);
}
