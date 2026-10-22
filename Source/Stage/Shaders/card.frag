// HALO_STAGE_DEBUG=5, the material swatch card: horizontal bands, top to
// bottom, each one material's albedo with no lighting at all. The High tier
// writes it as radiance (the composite applies exposure, the active tone
// curve and the output curve, exactly as for the lit device); the Low tier
// applies them here.
#include "common.glsl"

uniform float uHeight;
uniform int uCount;
uniform float uExposure;
uniform int uToneMapper;
uniform vec3 uSwatch[16];
out vec4 fragColour;

void main()
{
    float y = (uHeight - gl_FragCoord.y) / uHeight;
    int i = clamp (int (y * float (uCount)), 0, uCount - 1);
#if HALO_HDR
    fragColour = vec4 (uSwatch[i], 1.0);
#else
    fragColour = vec4 (srgbEncode (tonemap (uSwatch[i] * uExposure, uToneMapper)), 1.0);
#endif
}
