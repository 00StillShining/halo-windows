// Shared by every stage shader. The loader (Stage/ShaderLibrary) puts
// "#version 150", the HALO_HDR define and the tone constants from
// Core/ToneCurveConstants.inc (as "const float name = value;") in front of it,
// so GLSL and C++ (Core/ToneCurves.h) share one set of numbers.

const float PI = 3.14159265;

// ---------------------------------------------------------------- sRGB, exact
float srgbEncode1 (float x)
{
    x = clamp (x, 0.0, 1.0);
    return x <= srgbLinearKnee ? x * srgbLinearSlope : srgbScale * pow (x, 1.0 / srgbGamma) - srgbOffset;
}

float srgbDecode1 (float v)
{
    v = clamp (v, 0.0, 1.0);
    return v <= srgbEncodedKnee ? v / srgbLinearSlope : pow ((v + srgbOffset) / srgbScale, srgbGamma);
}

vec3 srgbEncode (vec3 c) { return vec3 (srgbEncode1 (c.r), srgbEncode1 (c.g), srgbEncode1 (c.b)); }
vec3 srgbDecode (vec3 c) { return vec3 (srgbDecode1 (c.r), srgbDecode1 (c.g), srgbDecode1 (c.b)); }

float luma (vec3 c) { return dot (c, vec3 (0.2126, 0.7152, 0.0722)); }

// ---------------------------------------------------------------- tone curves
// Scene-linear in, display-linear out. 0 = Khronos PBR Neutral (default),
// 1 = AgX, 2 = ACES (Hill), for comparison only.
vec3 tonemapNeutral (vec3 c)
{
    float x = min (c.r, min (c.g, c.b));
    float offset = x < neutralToeKnee ? x - neutralToeScale * x * x : neutralOffset;
    c -= offset;
    float peak = max (c.r, max (c.g, c.b));
    if (peak < neutralStartCompression)
        return c;
    float d = 1.0 - neutralStartCompression;
    float newPeak = 1.0 - d * d / (peak + d - neutralStartCompression);
    c *= newPeak / peak;
    float g = 1.0 - 1.0 / (neutralDesaturation * (peak - newPeak) + 1.0);
    return mix (c, vec3 (newPeak), g);
}

vec3 tonemapAgx (vec3 c)
{
    mat3 inset = mat3 (agxIn00, agxIn01, agxIn02, agxIn10, agxIn11, agxIn12, agxIn20, agxIn21, agxIn22);
    mat3 outset = mat3 (agxOut00, agxOut01, agxOut02, agxOut10, agxOut11, agxOut12, agxOut20, agxOut21, agxOut22);
    vec3 v = inset * c;
    v = clamp (log2 (max (v, vec3 (1e-10))), agxMinEv, agxMaxEv);
    v = (v - agxMinEv) / (agxMaxEv - agxMinEv);
    vec3 x2 = v * v, x4 = x2 * x2;
    v = agxC6 * x4 * x2 + agxC5 * x4 * v + agxC4 * x4 + agxC3 * x2 * v + agxC2 * x2 + agxC1 * v + agxC0;
    v = outset * v;
    return pow (clamp (v, 0.0, 1.0), vec3 (agxDisplayGamma));
}

vec3 tonemapAces (vec3 c)
{
    // The constants are row-major; GLSL's mat3() takes columns.
    mat3 inMat = mat3 (acesIn00, acesIn10, acesIn20, acesIn01, acesIn11, acesIn21, acesIn02, acesIn12, acesIn22);
    mat3 outMat = mat3 (acesOut00, acesOut10, acesOut20, acesOut01, acesOut11, acesOut21, acesOut02, acesOut12, acesOut22);
    vec3 v = inMat * c;
    v = (v * (v + acesA) - acesB) / (v * (acesC * v + acesD) + acesE);
    return clamp (outMat * v, 0.0, 1.0);
}

vec3 tonemap (vec3 c, int mapper)
{
    c = max (c, vec3 (0.0));
    if (mapper == 1) return tonemapAgx (c);
    if (mapper == 2) return tonemapAces (c);
    return tonemapNeutral (c);
}

// ---------------------------------------------------------------- noise
// Jimenez' interleaved gradient noise: cheap, blue-ish, stable per pixel.
float interleavedNoise (vec2 p)
{
    return fract (52.9829189 * fract (dot (p, vec2 (0.06711056, 0.00583715))));
}
