// The stage backdrop in display (sRGB) values: the floor token plus an
// optional screen-space grid (graph paper: a minor line every `cell`
// points and a major line every four). Until the perspective floor (M3).

float gridLine (float coord, float spacing, float width)
{
    float d = abs (mod (coord + width * 0.5, spacing) - width * 0.5);
    return 1.0 - smoothstep (width * 0.5, width * 0.5 + 1.0, d);
}

// fragCoord in pixels with y up; height in pixels; scale = pixels per point.
vec3 backdropSrgb (vec2 fragCoord, float height, float scale, float cellPt, vec3 canvas, vec4 minor, vec4 major)
{
    vec2 p = vec2 (fragCoord.x, height - fragCoord.y);
    float cell = cellPt * scale;
    float w = max (1.0, scale);
    float minorLine = max (gridLine (p.x, cell, w), gridLine (p.y, cell, w));
    float majorLine = max (gridLine (p.x, cell * 4.0, w), gridLine (p.y, cell * 4.0, w));
    vec3 c = canvas;
    c = mix (c, minor.rgb, minorLine * minor.a);
    c = mix (c, major.rgb, majorLine * major.a);
    return c;
}
