// Low tier only: the backdrop drawn first, in display values. (The High tier
// draws it inside composite.frag.)
#include "common.glsl"
#include "backdrop.glsl"

uniform float uScale;   // pixels per point
uniform float uHeight;  // viewport height in pixels
uniform float uCell;    // points per minor grid cell
uniform vec3 uCanvas;
uniform vec4 uMinor, uMajor;
out vec4 fragColour;

void main()
{
    fragColour = vec4 (backdropSrgb (gl_FragCoord.xy, uHeight, uScale, uCell, uCanvas, uMinor, uMajor), 1.0);
}
