// Debug-only tone-curve self-test: a 64-pixel ramp through the GLSL curve,
// read back and compared with Core/ToneCurves.h (plan section 3.3). Pixel i
// tone maps (x, 0.5 x, 0.1 x) with x = 4 i / 63.
#include "common.glsl"

uniform int uToneMapper;
out vec4 fragColour;

void main()
{
    float x = 4.0 * floor (gl_FragCoord.x) / 63.0;
    fragColour = vec4 (tonemap (vec3 (x, 0.5 * x, 0.1 * x), uToneMapper), 1.0);
}
