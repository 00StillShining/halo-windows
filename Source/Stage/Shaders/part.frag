// Every device part, and the ground quad around it.
//
// High tier (HALO_HDR 1): writes scene-linear radiance to location 0 (tone
// mapping happens once, in composite.frag); the ground writes only its contact
// shadow to location 1 (the pool and key spot are analytic in the composite).
// Low tier (HALO_HDR 0): tone maps here, writes display values, and draws the
// ground in three blended passes over the backdrop.
#include "common.glsl"
#include "floor.glsl"

in vec3 vNormal; in vec3 vColour; in vec3 vEmissive; in vec2 vUV; in vec2 vMaterial;
in vec3 vWorld; in vec3 vModel; in vec3 vModelNormal;
flat in int vFlags; flat in int vSlot; flat in int vMask; in float vAO; in float vTransmission;

// Material flags (Core/HmeshFormat.h).
const int FLAG_GLASS = 1;
const int FLAG_LCD = 2;
const int FLAG_DECAL_TARGET = 16;

uniform int uKind;            // 0 the device (one draw; materials say LCD or printed deck), 3 ground
uniform vec3 uEye;
uniform vec3 uSlotGlow[32];   // linear emissive per slot (pad strike, active group)
// D3's pad cue also warms its surroundings: up to four small diffuse-only
// point lights at the struck pads' apertures (radius 30 mm), world space.
uniform vec3 uPadLightPos[4];
uniform vec3 uPadLightColour[4]; // linear irradiance at 1 cm, 0 = off
uniform sampler2D uTexture;   // LCD (sRGB image)
uniform sampler2D uFaceAtlas; // deck silkscreen: SDF per ink (R green, G orange) over uFaceRect
uniform int uFaceAtlasOn;
uniform vec4 uFaceRect;      // x0, y0, width, depth in reference mm (x from the left, y from the ports)
uniform sampler2D uKeyAtlas;  // keys' legends: SDF per ink (R green, G orange, B white, A charcoal), at vUV
uniform int uKeyAtlasOn;
uniform vec3 uKeyInk[4];     // the colourway's inks for the keys atlas channels (linear)
uniform vec3 uFaceInk[2];    // ... and for the face silkscreen's green and orange
uniform vec2 uDeviceSize;     // model width, depth (m)

uniform sampler2DShadow uShadowMap;
uniform mat4 uLightViewProj;
uniform float uShadowRadius;  // (unused since PCSS-lite; kept for the Low tier's uniform list)
uniform sampler2D uShadowDepth;      // the shadow map's depth as a plain float (blocker search)
uniform float uShadowDepthRange;     // metres from the light's near to far plane
uniform float uShadowTexel;          // metres across the shadow map (1 in UV)

uniform sampler2D uDeckAO;    // baked AO of the top surfaces over uFaceRect (tools/model/ep40/ao.py)
uniform int uDeckAOOn;

// The studio (Stage/Environment): prefiltered reflections, split-sum table,
// SH9 ambient irradiance (with the floor bounce), and the analytic key and rim.
uniform samplerCube uEnv;
uniform sampler2D uBrdfLut;
uniform float uEnvMaxMip;
uniform vec3 uSH[9];
uniform vec3 uKeyDir, uRimDir;  // towards the lights, world
uniform vec3 uKeyE, uRimE;      // irradiance at normal incidence (look intensities applied)
uniform float uKeyRadius;       // angular radius of the key softbox (rad)
uniform float uSpecular;        // look: scales the studio reflections (rim intensity)
uniform float uGlassSheen;      // screen acrylic: reflection scale (0 = no sheen)
uniform float uGlassRoughness;  // screen acrylic: perceptual roughness of the sheen
uniform float uGlassPeak;       // screen acrylic: soft ceiling of the sheen (linear luma)
uniform float uExposure;
uniform float uMirror;        // > 0: the floor reflection pass (dark look strength), M8
uniform float uMirrorFloorY;  // world height of the floor plane
uniform int uToneMapper;
uniform int uDebug;           // 0 normal; 1 AO, 2 key shadow, 3 height contours, 4 floor terms,
                              // 6 mask, 7 albedo, 8 emissive, 9 world normals

// Low tier ground passes only.
uniform int uGroundPass;      // 0 contact shadow, 1 key-light spot
uniform vec3 uShadowTint;     // display value
uniform float uFloorSpot;
uniform vec2 uViewport;

out vec4 fragColour;
#if HALO_HDR
out vec4 floorOut;            // rgb reserved (floor reflection, M8), a = contact shadow
#endif

// A debug view's display value, so the composite's output curve gives it back.
vec4 displayValue (vec3 v)
{
#if HALO_HDR
    return vec4 (srgbDecode (v), 1.0);
#else
    return vec4 (v, 1.0);
#endif
}

vec4 radiance (vec3 c)
{
#if HALO_HDR
    return vec4 (c, 1.0);
#else
    return vec4 (srgbEncode (tonemap (c * uExposure, uToneMapper)), 1.0);
#endif
}

// Where a lit device colour goes: the device target, or, in the floor
// reflection pass, the floor target's rgb, weighted by black glass's Fresnel
// at this view and faded within about 20 mm of the floor so it reads as a
// sheen under the case, never a mirror.
void emit (vec3 lit)
{
#if HALO_HDR
    if (uMirror > 0.0)
    {
        float below = max (uMirrorFloorY - vWorld.y, 0.0);
        float cosView = clamp ((uEye.y - uMirrorFloorY) / max (length (uEye - vWorld), 1e-4), 0.0, 1.0);
        float k = uMirror * (0.3 + 0.7 * pow (1.0 - cosView, 3.0)) * exp (-below / 0.007);
        floorOut = vec4 (lit * k, 0.0);
        fragColour = vec4 (0.0);
        return;
    }
#endif
    fragColour = radiance (lit);
}

//---------------------------------------------------------------- shadows
const vec2 poisson[12] = vec2[12] (
    vec2 (-0.326, -0.406), vec2 (-0.840, -0.074), vec2 (-0.696,  0.457),
    vec2 (-0.203,  0.621), vec2 ( 0.962, -0.195), vec2 ( 0.473, -0.480),
    vec2 ( 0.519,  0.767), vec2 ( 0.185, -0.893), vec2 ( 0.507,  0.064),
    vec2 ( 0.896,  0.412), vec2 (-0.322, -0.933), vec2 (-0.792, -0.598));

// PCSS-lite: a blocker search in the plain depth copy finds how far the
// receiver sits behind what shades it; the penumbra then grows with that
// distance times the softbox's angular size, so a key's shadow is crisp where
// it meets the deck and opens out away from it (M8).
float keyShadow (vec3 world, vec3 n, float softness, float noise)
{
    vec3 p = world + n * 0.00030 + uKeyDir * 0.00015;
    vec4 c = uLightViewProj * vec4 (p, 1.0);
    vec3 s = c.xyz / c.w * 0.5 + 0.5;
    if (any (lessThan (s.xy, vec2 (0.0))) || any (greaterThan (s.xy, vec2 (1.0))))
        return 1.0;
    float a = noise * 6.2831853;
    mat2 rot = mat2 (cos (a), sin (a), -sin (a), cos (a));

    // Blockers within the widest penumbra this light can cast (6 mm).
    float search = 0.006 / uShadowTexel;
    float blocker = 0.0, found = 0.0;
    for (int i = 0; i < 12; ++i)
    {
        float d = texture (uShadowDepth, s.xy + rot * poisson[i] * search).r;
        if (d < s.z - 0.0004)
        {
            blocker += d;
            found += 1.0;
        }
    }
    if (found == 0.0)
        return 1.0;
    float behind = (s.z - blocker / found) * uShadowDepthRange;            // metres, receiver behind blocker
    float penumbra = clamp (behind * tan (uKeyRadius), 0.00025, 0.006);     // metres
    float radius = penumbra / uShadowTexel * softness;

    // Hardware-filtered taps on a rotated Poisson disc, mirrored at half
    // radius so the penumbra's core stays dense.
    float lit = 0.0;
    for (int i = 0; i < 8; ++i)
    {
        vec2 o = rot * poisson[i] * radius;
        lit += texture (uShadowMap, vec3 (s.xy + o, s.z - 0.00025));
        lit += texture (uShadowMap, vec3 (s.xy - o * 0.5, s.z - 0.00025));
    }
    return lit / 16.0;
}

//---------------------------------------------------------------- studio
vec3 irradianceSH (vec3 n)
{
    vec3 e = uSH[0] * 0.282095
           + uSH[1] * (0.488603 * n.y) + uSH[2] * (0.488603 * n.z) + uSH[3] * (0.488603 * n.x)
           + uSH[4] * (1.092548 * n.x * n.y) + uSH[5] * (1.092548 * n.y * n.z)
           + uSH[6] * (0.315392 * (3.0 * n.z * n.z - 1.0))
           + uSH[7] * (1.092548 * n.x * n.z) + uSH[8] * (0.546274 * (n.x * n.x - n.y * n.y));
    return max (e, vec3 (0.0));
}

// Kaplanyan-Hoffman: widen the lobe by the normal's variance across the
// pixel, so thin bevels and fillets never sparkle. Returns perceptual roughness.
// The widening is capped at 0.04 (was 0.18) so a 0.7 mm cap fillet a pixel or
// two wide still catches a line of the key at hero framing (M5 RECORD/PLAY);
// 4x MSAA resolves what is left.
float specularAA (vec3 n, float roughness)
{
    vec3 du = dFdx (n), dv = dFdy (n);
    float variance = 0.25 * (dot (du, du) + dot (dv, dv));
    float a = roughness * roughness;
    float a2 = clamp (a * a + min (2.0 * variance, 0.04), 0.0, 1.0);
    return sqrt (sqrt (a2));
}

// Reflection of the studio for a dielectric (F0 0.04): GGX-prefiltered
// environment x split-sum DFG with multi-scatter energy compensation, Lagarde's
// specular occlusion, and the key's share of the reflection shadowed with it.
vec3 studioSpecular (vec3 n, vec3 v, float roughness, float shadow, float ao)
{
    float NoV = clamp (dot (n, v), 1e-4, 1.0);
    vec3 r = reflect (-v, n);
    vec2 dfg = texture (uBrdfLut, vec2 (NoV, roughness)).rg;
    vec3 f0 = vec3 (0.04);
    vec3 spec = f0 * dfg.x + dfg.y;
    vec3 energy = 1.0 + f0 * (1.0 / max (dfg.x + dfg.y, 1e-4) - 1.0);
    float occlusion = clamp (pow (NoV + ao, exp2 (-16.0 * roughness - 1.0)) - 1.0 + ao, 0.0, 1.0);
    float keyShare = smoothstep (cos (uKeyRadius + roughness * 1.3 + 0.05), cos (uKeyRadius * 0.5), dot (r, uKeyDir));
    // Mip m holds roughness (m / maxMip)^2 (env_bake.py).
    vec3 env = textureLod (uEnv, r, sqrt (roughness) * uEnvMaxMip).rgb;
    return env * spec * energy * occlusion * mix (1.0, shadow, keyShare) * uSpecular;
}

// Ink coverage from an SDF atlas (0.5 at the edge): each edge antialiased over
// one screen pixel, from four sub-pixel taps at half the footprint, so 0.3 mm
// strokes stay visible (as grey, not gone) when a cap is a few pixels across.
vec4 inkCover (sampler2D atlas, vec2 uv)
{
    vec2 gx = dFdx (uv) * 0.5, gy = dFdy (uv) * 0.5;
    vec4 fw = max (fwidth (textureGrad (atlas, uv, gx, gy)) * 0.5, vec4 (1e-4));
    vec4 cover = vec4 (0.0);
    for (int i = 0; i < 4; ++i)
    {
        vec2 o = (float (i & 1) - 0.5) * gx + (float (i >> 1) - 0.5) * gy;
        cover += clamp ((textureGrad (atlas, uv + o, gx, gy) - 0.5) / fw + 0.5, 0.0, 1.0);
    }
    return cover * 0.25;
}

// Where the shaded point really is (the reflection pass shades the unmirrored point).
vec3 shadePos;

vec3 shade (vec3 albedo, float roughness, vec3 n, vec3 v, float shadow, float ao)
{
    float rough = specularAA (n, roughness);
    // Satin plastic: the key wraps a little past the terminator; frosted
    // stems (vTransmission) wrap further and pass the key through to their
    // far side, so they glow faintly where backlit (plan section 3.4).
    float wrap = 0.08 + vTransmission * 0.6;
    float keyNoL = clamp ((dot (n, uKeyDir) + wrap) / (1.0 + wrap), 0.0, 1.0);
    float rimNoL = max (dot (n, uRimDir), 0.0);
    vec3 irradiance = irradianceSH (n) * ao
                    + uKeyE * keyNoL * shadow * mix (1.0, ao, 0.35)
                    + uRimE * rimNoL * ao
                    + (uKeyE + uRimE) * vTransmission * 0.5 * clamp (-dot (n, uKeyDir), 0.0, 1.0) * mix (0.6, 1.0, ao);
    float NoV = clamp (dot (n, v), 1e-4, 1.0);
    vec2 dfg = texture (uBrdfLut, vec2 (NoV, rough)).rg;
    float fresnel = 0.04 * dfg.x + dfg.y;
    for (int i = 0; i < 4; ++i)
    {
        vec3 toLight = uPadLightPos[i] - shadePos;
        float d = length (toLight);
        float reach = 1.0 - smoothstep (0.0, 0.030, d);
        irradiance += uPadLightColour[i] * max (dot (n, toLight / max (d, 1e-5)), 0.0) * reach * reach;
    }
    vec3 diffuse = albedo / PI * irradiance * (1.0 - fresnel);
    return diffuse + studioSpecular (n, v, rough, shadow, ao);
}

//---------------------------------------------------------------- ground
void ground (float noise)
{
    float gap = footprintGap (vModel.xy, uDeviceSize);
    float border = stageBorder (gl_FragCoord.xy, uViewport);

#if ! HALO_HDR
    if (uGroundPass == 1)
    {
        fragColour = vec4 (vec3 (1.0, 0.97, 0.92) * floorSpot (gap, uFloorSpot) * border, 1.0); // screen-blended
        return;
    }
#endif

    // Contact shadow: a wide soft occlusion (the room can't see under the
    // case), a tighter contact line, and the key light's own shadow.
    float fade = 1.0 - smoothstep (0.11, 0.30, length (vModel.xy * vec2 (1.2, 0.92)));
    if (fade <= 0.0)
        discard;
    float shadow = gap < 0.03 ? keyShadow (vWorld, vec3 (0.0, 1.0, 0.0), 2.5, noise) : 1.0;
    // The floor's view of the room closed off by the case beside it: about
    // half the sky behind a 12 mm wall, fading out within ~12 mm (analytic,
    // the device is a box on the floor; this was a height-map AO).
    float near = 1.0 - 0.5 * inversesqrt (1.0 + pow (gap / 0.012, 2.0)) * (1.0 - smoothstep (0.004, 0.014, gap));
    float wide = 1.0 - exp (-gap / 0.016);
    float a = (1.0 - near) * 0.55 + (1.0 - wide) * 0.42 + (1.0 - shadow) * 0.30;
    a = clamp (a, 0.0, 0.82) * fade * border;
#if HALO_HDR
    fragColour = vec4 (0.0);
    floorOut = vec4 (0.0, 0.0, 0.0, a);
#else
    fragColour = vec4 (uShadowTint, a);
    if (uDebug == 4) fragColour = vec4 (wide, shadow, near, 1.0);
#endif
}

void main()
{
#if HALO_HDR
    floorOut = vec4 (0.0);
#endif
    float noise = interleavedNoise (gl_FragCoord.xy);

    if (uKind == 3)
    {
        ground (noise);
        return;
    }

    // 6: silhouette mask of the device.
    if (uDebug == 6)
    {
        fragColour = displayValue (vec3 (1.0));
        return;
    }

    vec3 n = normalize (vNormal);
    if (! gl_FrontFacing) n = -n;
    vec3 mn = normalize (vModelNormal);
    if (! gl_FrontFacing) mn = -mn;
    vec3 v = normalize (uEye - vWorld);

    // The reflection pass: the mirror image of the real, lit device is that
    // device seen from the mirrored eye, so shade the unmirrored point with the
    // unmirrored normal and the mirrored eye (light, shadow and studio stay real).
    vec3 shadeWorld = vWorld;
    if (uMirror > 0.0)
    {
        shadeWorld = vec3 (vWorld.x, 2.0 * uMirrorFloorY - vWorld.y, vWorld.z);
        n *= vec3 (1.0, -1.0, 1.0);
        v = normalize (vec3 (uEye.x, 2.0 * uMirrorFloorY - uEye.y, uEye.z) - shadeWorld);
    }
    shadePos = shadeWorld;
    float shadow = keyShadow (shadeWorld, n, 1.0, noise);
    // Baked AO (tools/model/ep40/ao.py): per vertex, and from the deck texture
    // on the top surfaces, whose few large triangles carry too few vertices.
    float ao = vAO;
    if ((vFlags & FLAG_DECAL_TARGET) != 0 && mn.z > 0.5 && uDeckAOOn != 0)
    {
        vec2 aoUV = (vec2 (vModel.x * 1000.0 + 88.0, 120.0 - vModel.y * 1000.0) - uFaceRect.xy) / uFaceRect.zw;
        ao = texture (uDeckAO, aoUV).r;
    }
    // Faces near the floor see less of the room.
    ao *= mix (0.72, 1.0, clamp (smoothstep (-0.0095, -0.002, vModel.z) + max (mn.z, 0.0), 0.0, 1.0));

    if ((vFlags & FLAG_GLASS) != 0)
    {
        // The screen acrylic (blended over the LCD, Stage/StageComponent):
        // the studio's rim strip crossing it as a soft diagonal sheen. The
        // screen is the stage's most important surface, so the sheen is a
        // highlight, never a veil: scaled (uGlassSheen) and rolled off toward
        // a ceiling of about a third of the LCD's own range, so icons and
        // digits stay readable through it at every framing. Its edges show
        // as tinted acrylic.
        // Only the outer faces: the slab's underside, seen through the top,
        // would add the sheen a second time.
        if (vModelNormal.z < -0.5)
            discard;
        vec3 glass = studioSpecular (n, v, specularAA (n, uGlassRoughness), shadow, 1.0) * uGlassSheen;
        glass /= 1.0 + luma (glass) / uGlassPeak;
        float edge = 1.0 - smoothstep (0.5, 0.9, abs (mn.z));
        vec3 tint = vColour * irradianceSH (n) * edge * 0.6;
#if HALO_HDR
        fragColour = vec4 (glass + tint, mix (0.97, 0.35, edge));
#else
        // Display values blend additively here, so add what the sheen adds to
        // a typical LCD level once tone mapped, not the sheen tone mapped on
        // its own (which the curve's steep toe would exaggerate several times).
        const vec3 lcdLevel = vec3 (0.0035, 0.113, 0.051);  // the LCD backlight green, linear (DisplayRenderer)
        vec3 lifted = srgbEncode (tonemap ((lcdLevel * 0.97 + glass + tint) * uExposure, uToneMapper));
        vec3 base = srgbEncode (tonemap (lcdLevel * 0.97 * uExposure, uToneMapper));
        // Screen-blended (Stage/StageComponent): exact at that level, smaller over brighter icons.
        fragColour = vec4 (max (lifted - base, vec3 (0.0)) / max (1.0 - base, vec3 (0.05)), mix (0.97, 0.35, edge));
#endif
        return;
    }

    if ((vFlags & FLAG_LCD) != 0)
    {
        // The LCD is self-lit under a glossy window: nearly unshaded, plus
        // the studio's reflection in the glass.
        vec3 tex = srgbDecode (texture (uTexture, vUV).rgb);
        if (uDebug == 7 || uDebug == 8) { fragColour = displayValue (srgbEncode (tex)); return; }
        // Backlit under the acrylic (drawn after, with its sheen): nearly unshaded.
        vec3 c = tex * mix (0.86, 1.0, shadow) * mix (0.8, 1.0, ao);
        emit (c);
        return;
    }

    vec3 albedo = vColour;
    float roughness = clamp (vMaterial.x, 0.04, 1.0);

    if ((vFlags & FLAG_DECAL_TARGET) != 0 && mn.z > 0.5)
    {
        // Model metres -> reference mm -> the silkscreen's texture.
        vec2 uv = (vec2 (vModel.x * 1000.0 + 88.0, 120.0 - vModel.y * 1000.0) - uFaceRect.xy) / uFaceRect.zw;
        if (uFaceAtlasOn != 0 && all (greaterThanEqual (uv, vec2 (0.0))) && all (lessThanEqual (uv, vec2 (1.0))))
        {
            vec2 cover = inkCover (uFaceAtlas, uv).rg;
            albedo = mix (albedo, uFaceInk[1], cover.g);     // TE: orange #FF681D
            albedo = mix (albedo, uFaceInk[0], cover.r);     // TE: green #00503A
            roughness = min (roughness + 0.08 * max (cover.r, cover.g), 1.0);
        }
    }
    else if ((vMask & 1) != 0 && uKeyAtlasOn != 0 && mn.z > 0.5)
    {
        // Laser-printed legends, slightly rougher than the cap.
        vec4 cover = inkCover (uKeyAtlas, vUV);
        albedo = mix (albedo, uKeyInk[2], cover.b);   // TE: white #F4F2EC
        albedo = mix (albedo, uKeyInk[3], cover.a);   // TE: charcoal #2B2B28
        albedo = mix (albedo, uKeyInk[1], cover.g);   // TE: orange #FF681D
        albedo = mix (albedo, uKeyInk[0], cover.r);   // TE: green #00503A
        roughness = min (roughness + 0.08 * max (max (cover.r, cover.g), max (cover.b, cover.a)), 1.0);
    }

    // 7: unlit albedo (print included); 8: emissive only.
    if (uDebug == 7) { fragColour = displayValue (srgbEncode (albedo)); return; }
    if (uDebug == 8) { int gs = (vMask >> 1) & 31; fragColour = displayValue (srgbEncode (vEmissive * 0.6 + (gs > 0 ? uSlotGlow[gs] : vec3 (0.0)))); return; }
    if (uDebug == 9) { fragColour = displayValue (n * 0.5 + 0.5); return; }
    if (uDebug == 1) { fragColour = displayValue (vec3 (ao)); return; }
    if (uDebug == 2) { fragColour = displayValue (vec3 (shadow)); return; }
    if (uDebug == 3) { fragColour = displayValue (vec3 (vAO)); return; }   // vertex AO alone

    vec3 lit = shade (albedo, roughness, n, v, shadow, ao);
    // Aperture underglow (D3): a well's mask bits 1-5 name its key's slot.
    int glowSlot = (vMask >> 1) & 31;
    lit += vEmissive * 0.6 + (glowSlot > 0 ? uSlotGlow[glowSlot] : vec3 (0.0));
    emit (lit);
}
