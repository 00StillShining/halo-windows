#pragma once

// GLSL 150 sources for the hero EP-40 (StageComponent). Header-only on purpose:
// the stage owns its shaders, and nothing else needs to link against them.
//
// Frame structure:
//   1. depth pass from the key light            -> uShadowMap (soft PCF shadows)
//   2. depth pass straight down onto the face   -> uHeightMap (contact AO)
//   3. graph-paper background, ground shadow, then every part, lit with a small
//      studio rig: a shadowed key softbox, a large overhead diffuser, a fill and
//      a hemisphere, with roughness-aware reflections of those same lights.
//
// The device is nearly a height field when seen from above, so ambient
// occlusion is computed by marching the top-down depth map: cheap, stable, and
// it darkens exactly what reads as "real" on a keypad (the gaps between caps,
// the wells round each key, the case's contact with the ground).

namespace halo::stage_shaders
{
    // Shared by both map passes. The height pass keeps model-space z in a
    // filterable float target; the shadow pass has no colour attachment.
    inline const char* depthVertex = R"(
        #version 150
        in vec3 aPos;
        uniform mat4 uModel;
        uniform mat4 uViewProj;
        out float vHeight;
        void main()
        {
            vec4 p = uModel * vec4 (aPos, 1.0);
            vHeight = p.z;
            gl_Position = uViewProj * p;
        })";

    inline const char* depthFragment = R"(
        #version 150
        in float vHeight;
        out vec4 fragHeight;
        void main() { fragHeight = vec4 (vHeight); })";

    inline const char* partVertex = R"(
        #version 150
        in vec3 aPos; in vec3 aNormal; in vec3 aColour; in float aEmissive; in vec2 aUV; in vec2 aMaterial;
        uniform mat4 uModel;        // part -> world
        uniform mat4 uPartToModel;  // part -> model (Z-up, metres)
        uniform mat4 uViewProj;
        out vec3 vNormal; out vec3 vColour; out float vEmissive; out vec2 vUV; out vec2 vMaterial;
        out vec3 vLocal; out vec3 vWorld; out vec3 vModel; out vec3 vModelNormal;
        void main()
        {
            vec4 world = uModel * vec4 (aPos, 1.0);
            vWorld = world.xyz;
            vLocal = aPos;
            vModel = (uPartToModel * vec4 (aPos, 1.0)).xyz;
            vNormal = normalize (mat3 (uModel) * aNormal);
            vModelNormal = normalize (mat3 (uPartToModel) * aNormal);
            vColour = aColour; vEmissive = aEmissive; vUV = aUV; vMaterial = aMaterial;
            gl_Position = uViewProj * world;
        })";

    inline const char* partFragment = R"(
        #version 150
        in vec3 vNormal; in vec3 vColour; in float vEmissive; in vec2 vUV; in vec2 vMaterial;
        in vec3 vLocal; in vec3 vWorld; in vec3 vModel; in vec3 vModelNormal;

        uniform int uKind;            // 0 part, 1 display, 2 halo ring, 3 ground, 4 face plate
        uniform vec3 uEye;
        uniform vec3 uGlow;           // linear emissive added to the part
        uniform sampler2D uTexture;   // LCD
        uniform sampler2D uPrint;     // face silkscreen, premultiplied, model XY
        uniform vec2 uDeviceSize;     // model width, depth (m)
        uniform int uRingMode;        // 0 dark, 1 discover sweep, 2 steady, 3 error
        uniform float uTime;
        uniform vec3 uRingColour;
        uniform float uRingLevel;

        uniform sampler2DShadow uShadowMap;
        uniform mat4 uLightViewProj;
        uniform float uShadowRadius;  // PCF radius in shadow-map UV

        uniform sampler2D uHeightMap; // model-space z of the highest surface
        uniform vec4 uHeightRect;     // centre xy, half extent xy (model space)
        uniform float uHeightTexel;   // metres per texel at mip 0

        uniform vec3 uKeyDir, uTopDir, uFillDir;
        uniform vec3 uShadowTint;
        uniform float uExposure;
        uniform int uDebug;           // 0 normal; 1 AO, 2 key shadow, 3 height contours, 4 floor terms

        out vec4 fragColour;

        const float PI = 3.14159265;

        vec3 toSrgb (vec3 c) { return pow (clamp (c, 0.0, 1.0), vec3 (1.0 / 2.2)); }
        vec3 toLinear (vec3 c) { return pow (c, vec3 (2.2)); }

        // Khronos PBR Neutral: keeps the riddim green and orange true to hue.
        vec3 neutralTonemap (vec3 c)
        {
            const float start = 0.8 - 0.04;
            const float desaturation = 0.15;
            float x = min (c.r, min (c.g, c.b));
            float offset = x < 0.08 ? x - 6.25 * x * x : 0.04;
            c -= offset;
            float peak = max (c.r, max (c.g, c.b));
            if (peak < start) return c;
            const float d = 1.0 - start;
            float newPeak = 1.0 - d * d / (peak + d - start);
            c *= newPeak / peak;
            float g = 1.0 - 1.0 / (desaturation * (peak - newPeak) + 1.0);
            return mix (c, vec3 (newPeak), g);
        }

        float interleavedNoise() { return fract (52.9829189 * fract (dot (gl_FragCoord.xy, vec2 (0.06711056, 0.00583715)))); }

        //---------------------------------------------------------------- shadows
        const vec2 poisson[12] = vec2[12] (
            vec2 (-0.326, -0.406), vec2 (-0.840, -0.074), vec2 (-0.696,  0.457),
            vec2 (-0.203,  0.621), vec2 ( 0.962, -0.195), vec2 ( 0.473, -0.480),
            vec2 ( 0.519,  0.767), vec2 ( 0.185, -0.893), vec2 ( 0.507,  0.064),
            vec2 ( 0.896,  0.412), vec2 (-0.322, -0.933), vec2 (-0.792, -0.598));

        float keyShadow (vec3 world, vec3 n, float softness, float noise)
        {
            vec3 p = world + n * 0.00030 + uKeyDir * 0.00015;
            vec4 c = uLightViewProj * vec4 (p, 1.0);
            vec3 s = c.xyz / c.w * 0.5 + 0.5;
            if (any (lessThan (s.xy, vec2 (0.0))) || any (greaterThan (s.xy, vec2 (1.0))))
                return 1.0;
            float a = noise * 6.2831853;
            mat2 rot = mat2 (cos (a), sin (a), -sin (a), cos (a));
            // Hardware-filtered taps on a rotated Poisson disc, mirrored at half
            // radius so the penumbra's core stays dense.
            float lit = 0.0;
            for (int i = 0; i < 8; ++i)
            {
                vec2 o = rot * poisson[i] * uShadowRadius * softness;
                lit += texture (uShadowMap, vec3 (s.xy + o, s.z - 0.00025));
                lit += texture (uShadowMap, vec3 (s.xy - o * 0.5, s.z - 0.00025));
            }
            return lit / 16.0;
        }

        //---------------------------------------------------------------- height-field AO
        // `footprint` is the width (m) the sample should average over; coarser
        // mips make far taps cone-shaped, so occlusion fades in smoothly instead
        // of switching on as a single tap crosses a key's edge.
        float heightAt (vec2 xy, float footprint)
        {
            vec2 uv = (xy - uHeightRect.xy) / uHeightRect.zw * 0.5 + 0.5;
            return textureLod (uHeightMap, uv, max (log2 (footprint / uHeightTexel), 0.0)).r;
        }

        // Horizon-based occlusion over `directions` fixed directions.
        float heightAO (vec3 p, vec3 n, float reach, int directions)
        {
            vec2 base = p.xy + n.xy * 0.00045;
            float occlusion = 0.0, weights = 0.0;
            float step = 6.2831853 / float (directions);
            for (int i = 0; i < directions; ++i)
            {
                float a = (float (i) + 0.5) * step;
                vec2 dir = vec2 (cos (a), sin (a));
                // Walls only look outwards; tops look everywhere.
                float w = clamp (n.z + dot (dir, n.xy), 0.0, 1.0);
                float slope = 0.0;
                for (int j = 1; j <= 4; ++j)
                {
                    float r = reach * float (j * j);
                    slope = max (slope, (heightAt (base + dir * r, r * 0.7) - p.z) / r);
                }
                occlusion += w * slope * inversesqrt (1.0 + slope * slope);
                weights += w;
            }
            return 1.0 - occlusion / max (weights, 0.0001);
        }

        //---------------------------------------------------------------- studio rig
        // A soft light seen in a reflection: the lobe widens with roughness and
        // dims to conserve energy, but never gets sharper than the light itself.
        float softbox (vec3 r, vec3 dir, float size, float roughness)
        {
            float alpha = max (roughness * roughness, 0.002);
            float lightExp = 2.0 / (size * size);
            float e = min (2.0 / (alpha * alpha) - 2.0, lightExp);
            return (e + 2.0) / (lightExp + 2.0) * pow (max (dot (r, dir), 0.0), e);
        }

        vec3 skyGradient (vec3 d, float roughness)
        {
            float w = 0.15 + roughness * 0.6;
            vec3 sky = vec3 (0.92, 0.95, 1.0) * 0.55;
            vec3 floorColour = vec3 (0.42, 0.44, 0.44) * 0.22;
            return mix (floorColour, sky, smoothstep (-w, w, d.y));
        }

        vec3 reflection (vec3 r, float roughness, float shadow, float specAO)
        {
            vec3 c = skyGradient (r, roughness) * specAO;
            c += vec3 (1.0, 0.97, 0.92) * 3.2 * softbox (r, uKeyDir, 0.32, roughness) * shadow;
            c += vec3 (0.95, 0.97, 1.0) * 1.3 * softbox (r, uTopDir, 0.55, roughness) * specAO;
            c += vec3 (0.90, 0.95, 1.0) * 0.6 * softbox (r, uFillDir, 0.50, roughness);
            return c;
        }

        // Karis' analytic fit of the split-sum environment BRDF.
        vec2 envBRDF (float NoV, float roughness)
        {
            const vec4 c0 = vec4 (-1.0, -0.0275, -0.572, 0.022);
            const vec4 c1 = vec4 (1.0, 0.0425, 1.04, -0.04);
            vec4 r = roughness * c0 + c1;
            float a004 = min (r.x * r.x, exp2 (-9.28 * NoV)) * r.x + r.y;
            return vec2 (-1.04, 1.04) * a004 + r.zw;
        }

        vec3 shade (vec3 albedo, float roughness, float metallic, vec3 n, vec3 v,
                    float shadow, float ao)
        {
            float NoV = clamp (dot (n, v), 0.001, 1.0);
            vec3 r = reflect (-v, n);
            vec3 f0 = mix (vec3 (0.04), albedo, metallic);
            vec2 ab = envBRDF (NoV, roughness);
            vec3 specColour = f0 * ab.x + ab.y;
            float specAO = clamp (ao * 1.2 - 0.1, 0.0, 1.0);

            // Slight wrap softens the terminator the way satin plastic does.
            float key = clamp ((dot (n, uKeyDir) + 0.08) / 1.08, 0.0, 1.0);
            float top = clamp ((dot (n, uTopDir) + 0.2) / 1.2, 0.0, 1.0);
            float fill = max (dot (n, uFillDir), 0.0);
            float hemi = mix (0.16, 0.36, n.y * 0.5 + 0.5);
            vec3 diffuseLight = vec3 (1.0, 0.97, 0.92) * 0.78 * key * shadow * mix (1.0, ao, 0.35)
                              + vec3 (0.95, 0.97, 1.0) * 0.30 * top * ao
                              + vec3 (0.92, 0.95, 1.0) * 0.14 * fill * ao
                              + vec3 (hemi) * ao;

            vec3 diffuse = albedo * (1.0 - metallic) * (1.0 - specColour) * diffuseLight;
            return diffuse + specColour * reflection (r, roughness, shadow, specAO);
        }

        void main()
        {
            float noise = interleavedNoise();

            if (uKind == 3)
            {
                // Ground: only the darkening is drawn, over the graph paper (after
                // the device, so the floor hidden under it is never shaded). A wide
                // soft occlusion (the room can't see under the case), a tighter
                // contact line, and the key light's own shadow.
                float fade = 1.0 - smoothstep (0.11, 0.30, length (vModel.xy * vec2 (1.2, 0.92)));
                if (fade <= 0.0)
                    discard;
                // Distance to the case's rounded footprint drives the broad falloff.
                const float corner = 0.0052;
                vec2 q = abs (vModel.xy) - (uDeviceSize * 0.5 - corner);
                float gap = max (length (max (q, 0.0)) + min (max (q.x, q.y), 0.0) - corner, 0.0);
                float shadow = gap < 0.03 ? keyShadow (vWorld, vec3 (0.0, 1.0, 0.0), 2.5, noise) : 1.0;
                float near = gap < 0.016 ? heightAO (vModel, vec3 (0.0, 0.0, 1.0), 0.0009, 8) : 1.0;
                float wide = 1.0 - exp (-gap / 0.016);
                float a = (1.0 - near) * 0.55 + (1.0 - wide) * 0.42 + (1.0 - shadow) * 0.30;
                a = clamp (a, 0.0, 0.82) * fade;
                fragColour = vec4 (uShadowTint, a);
                if (uDebug == 4) fragColour = vec4 (wide, shadow, near, 1.0);
                return;
            }

            // 6: silhouette mask of the device (the floor ring is not the device).
            if (uDebug == 6)
            {
                if (uKind == 2) discard;
                fragColour = vec4 (1.0);
                return;
            }

            vec3 n = normalize (vNormal);
            if (! gl_FrontFacing) n = -n;
            vec3 mn = normalize (vModelNormal);
            if (! gl_FrontFacing) mn = -mn;
            vec3 v = normalize (uEye - vWorld);

            float shadow = keyShadow (vWorld, n, 1.0, noise);
            float ao = clamp (1.0 - (1.0 - heightAO (vModel, mn, 0.0006, 10)) * 1.6, 0.0, 1.0);
            // Faces near the floor see less of the room.
            ao *= mix (0.72, 1.0, clamp (smoothstep (-0.0095, -0.002, vModel.z) + max (mn.z, 0.0), 0.0, 1.0));

            if (uKind == 1)
            {
                // The LCD is self-lit under a glossy window: show it nearly
                // unshaded, plus the studio's reflection in the glass.
                vec3 tex = toLinear (texture (uTexture, vUV).rgb);
                if (uDebug == 7 || uDebug == 8) { fragColour = vec4 (toSrgb (tex), 1.0); return; }
                float NoV = clamp (dot (n, v), 0.001, 1.0);
                vec2 ab = envBRDF (NoV, 0.06);
                vec3 glass = (0.04 * ab.x + ab.y) * reflection (reflect (-v, n), 0.06, shadow, 1.0);
                vec3 c = tex * mix (0.86, 1.0, shadow) * mix (0.8, 1.0, ao) + glass * 0.6;
                fragColour = vec4 (toSrgb (c), 1.0);
                return;
            }

            vec3 albedo = vColour;
            float roughness = clamp (vMaterial.x, 0.04, 1.0);
            float metallic = vMaterial.y;

            if (uKind == 4 && mn.z > 0.5)
            {
                vec2 uv = vModel.xy / uDeviceSize + 0.5;
                vec4 ink = texture (uPrint, uv);  // premultiplied
                albedo = albedo * (1.0 - ink.a) + ink.rgb;
                roughness = mix (roughness, 0.5, ink.a);
            }

            // 7: unlit albedo (print included); 8: emissive only.
            if (uDebug == 7) { fragColour = vec4 (toSrgb (albedo), 1.0); return; }
            if (uDebug == 8) { fragColour = vec4 (toSrgb (albedo * vEmissive * 0.6 + uGlow), 1.0); return; }
            if (uDebug == 1) { fragColour = vec4 (vec3 (ao), 1.0); return; }
            if (uDebug == 2) { fragColour = vec4 (vec3 (shadow), 1.0); return; }
            if (uDebug == 3) { fragColour = vec4 (vec3 (fract (heightAt (vModel.xy, 0.0) * 100.0)), 1.0); return; }
            vec3 lit = shade (albedo, roughness, metallic, n, v, shadow, ao);
            lit += albedo * vEmissive * 0.6 + uGlow;
            vec3 c = toSrgb (neutralTonemap (lit * uExposure));

            if (uKind == 2 && uRingMode != 0)
            {
                float angle = atan (vLocal.y, vLocal.x);
                float segment = floor ((angle + 3.14159265) / 6.2831853 * 48.0);
                float level = uRingLevel;
                if (uRingMode == 1)
                {
                    float head = floor (fract (uTime / 4.0) * 48.0);
                    float d = mod (head - segment + 48.0, 48.0);
                    float falloff[6] = float[6] (1.0, 0.72, 0.48, 0.28, 0.14, 0.06);
                    level = d < 6.0 ? falloff[int (d)] * 0.9 : 0.0;
                }
                // Segment opacity over the dark ring, like the Mac's 48 unlit segments.
                c = mix (c, toSrgb (uRingColour), level);
            }

            fragColour = vec4 (c, 1.0);
        })";

    inline const char* backgroundVertex = R"(
        #version 150
        void main()
        {
            vec2 p = vec2 ((gl_VertexID << 1) & 2, gl_VertexID & 2);
            gl_Position = vec4 (p * 2.0 - 1.0, 0.0, 1.0);
        })";

    // HALO_STAGE_DEBUG=5, the material swatch card: horizontal bands, top to
    // bottom, each a material's albedo x exposure through the same Neutral tone
    // curve and output curve the part shader uses, with no lighting at all.
    inline const char* cardFragment = R"(
        #version 150
        uniform float uHeight;
        uniform int uCount;
        uniform float uExposure;
        uniform vec3 uSwatch[16];
        out vec4 fragColour;
        vec3 neutralTonemap (vec3 c)
        {
            const float start = 0.8 - 0.04;
            const float desaturation = 0.15;
            float x = min (c.r, min (c.g, c.b));
            float offset = x < 0.08 ? x - 6.25 * x * x : 0.04;
            c -= offset;
            float peak = max (c.r, max (c.g, c.b));
            if (peak < start) return c;
            const float d = 1.0 - start;
            float newPeak = 1.0 - d * d / (peak + d - start);
            c *= newPeak / peak;
            float g = 1.0 - 1.0 / (desaturation * (peak - newPeak) + 1.0);
            return mix (c, vec3 (newPeak), g);
        }
        void main()
        {
            float y = (uHeight - gl_FragCoord.y) / uHeight;
            int i = clamp (int (y * float (uCount)), 0, uCount - 1);
            vec3 c = neutralTonemap (uSwatch[i] * uExposure);
            fragColour = vec4 (pow (clamp (c, 0.0, 1.0), vec3 (1.0 / 2.2)), 1.0);
        })";

    // The graph-paper canvas (GridCanvas.swift): 16 pt cells, a major line every 4.
    inline const char* backgroundFragment = R"(
        #version 150
        uniform float uScale;
        uniform float uHeight;
        uniform vec3 uCanvas;
        uniform vec4 uMinor;
        uniform vec4 uMajor;
        out vec4 fragColour;
        float line (float coord, float spacing, float width)
        {
            float d = abs (mod (coord + width * 0.5, spacing) - width * 0.5);
            return 1.0 - smoothstep (width * 0.5, width * 0.5 + 1.0, d);
        }
        void main()
        {
            vec2 p = vec2 (gl_FragCoord.x, uHeight - gl_FragCoord.y);
            float cell = 16.0 * uScale;
            float w = max (1.0, uScale);
            float minor = max (line (p.x, cell, w), line (p.y, cell, w));
            float major = max (line (p.x, cell * 4.0, w), line (p.y, cell * 4.0, w));
            vec3 c = uCanvas;
            c = mix (c, uMinor.rgb, minor * uMinor.a);
            c = mix (c, uMajor.rgb, major * uMajor.a);
            fragColour = vec4 (c, 1.0);
        })";
}
