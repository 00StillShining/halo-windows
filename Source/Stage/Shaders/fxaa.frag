// FXAA (Lottes' compact variant) on the display-encoded image, engaged only
// when the stage's MSAA is short of 4 samples or on the Low tier. The dither
// runs after it, so it is always the last thing to touch a pixel.
#include "common.glsl"

uniform sampler2D uImage;     // display-encoded colour
uniform vec2 uViewport;       // output size in pixels
uniform vec2 uUvScale;        // the part of the image this frame used
uniform int uDither;
out vec4 fragColour;

void main()
{
    const float spanMax = 8.0;
    const float reduceMul = 1.0 / 8.0;
    const float reduceMin = 1.0 / 128.0;

    vec2 texel = uUvScale / uViewport;
    vec2 uv = gl_FragCoord.xy / uViewport * uUvScale;

    float lumaNW = luma (texture (uImage, uv + vec2 (-1.0, -1.0) * texel).rgb);
    float lumaNE = luma (texture (uImage, uv + vec2 ( 1.0, -1.0) * texel).rgb);
    float lumaSW = luma (texture (uImage, uv + vec2 (-1.0,  1.0) * texel).rgb);
    float lumaSE = luma (texture (uImage, uv + vec2 ( 1.0,  1.0) * texel).rgb);
    vec4 centre = texture (uImage, uv);
    float lumaM = luma (centre.rgb);

    float lumaMin = min (lumaM, min (min (lumaNW, lumaNE), min (lumaSW, lumaSE)));
    float lumaMax = max (lumaM, max (max (lumaNW, lumaNE), max (lumaSW, lumaSE)));

    vec2 dir = vec2 (-((lumaNW + lumaNE) - (lumaSW + lumaSE)), ((lumaNW + lumaSW) - (lumaNE + lumaSE)));
    float dirReduce = max ((lumaNW + lumaNE + lumaSW + lumaSE) * (0.25 * reduceMul), reduceMin);
    float rcpDirMin = 1.0 / (min (abs (dir.x), abs (dir.y)) + dirReduce);
    dir = clamp (dir * rcpDirMin, vec2 (-spanMax), vec2 (spanMax)) * texel;

    vec3 a = 0.5 * (texture (uImage, uv + dir * (1.0 / 3.0 - 0.5)).rgb + texture (uImage, uv + dir * (2.0 / 3.0 - 0.5)).rgb);
    vec3 b = a * 0.5 + 0.25 * (texture (uImage, uv + dir * -0.5).rgb + texture (uImage, uv + dir * 0.5).rgb);
    float lumaB = luma (b);
    vec3 colour = (lumaB < lumaMin || lumaB > lumaMax) ? a : b;
    // Leave flat areas exactly as they were (the stage border must stay the token).
    if (lumaMax - lumaMin < 1.0 / 255.0)
        colour = centre.rgb;

    if (uDither != 0)
        colour += (interleavedNoise (gl_FragCoord.xy) - 0.5) * (0.9 / 255.0); // +-0.45 LSB
    fragColour = vec4 (avoidKeyedColour (colour), 1.0);
}
