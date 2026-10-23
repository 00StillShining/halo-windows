// Floor terms around the device, shared by the HDR composite (which finds the
// floor point under each pixel analytically) and the Low tier's ground passes.
// All distances in model metres; `deviceSize` is the footprint (width, depth).

// Distance from the case's rounded footprint (0 inside).
float footprintGap (vec2 modelXY, vec2 deviceSize)
{
    const float corner = 0.001; // the case's nearly sharp vertical edges (spec.CORNER_RADIUS)
    vec2 q = abs (modelXY) - (deviceSize * 0.5 - corner);
    return max (length (max (q, 0.0)) + min (max (q.x, q.y), 0.0) - corner, 0.0);
}

// Floor terms fade to nothing within 5% of the stage edge, so the stage
// border is always exactly the backdrop token (plan section 3.2).
float stageBorder (vec2 fragCoord, vec2 viewport)
{
    float edge = min (min (fragCoord.x, fragCoord.y), min (viewport.x - fragCoord.x, viewport.y - fragCoord.y));
    // Exactly zero over the outermost two pixels: near black, the sRGB curve
    // turns even a 0.1% residue into visible steps.
    return smoothstep (2.0, 0.05 * min (viewport.x, viewport.y), edge);
}

// The ring pool: Halo's RingState as light on the floor, hugging the footprint
// (plan D6). It rises over the first 3 mm so the contact line stays dark.
// mode 0 none, 1 discovering (a dashed comet along the perimeter), 2 steady.
float ringPool (vec2 modelXY, vec2 deviceSize, float gap, int mode, float level, float time, float period)
{
    if (mode == 0)
        return 0.0;
    float pool = level * exp (-max (gap - 0.002, 0.0) / 0.018) * smoothstep (0.0, 0.003, gap);
    if (mode == 1)
    {
        // 48 dashes along the perimeter, like the Mac's ring segments.
        vec2 h = deviceSize * 0.5;
        vec2 c = clamp (modelXY, -h, h);
        float t;
        if (abs (modelXY.x) - h.x > abs (modelXY.y) - h.y)
            t = modelXY.x > 0.0 ? h.y + c.y : 2.0 * h.y + 2.0 * h.x + (h.y - c.y);
        else
            t = modelXY.y > 0.0 ? 2.0 * h.y + (h.x - c.x) : 4.0 * h.y + 2.0 * h.x + (c.x + h.x);
        float pos = t / (4.0 * (h.x + h.y)) * 48.0;
        float head = fract (time / period) * 48.0;
        float d = mod (head - pos + 48.0, 48.0);
        float dash = 1.0 - smoothstep (0.62, 0.78, fract (pos));
        pool *= dash * exp (-d / 4.5);
    }
    return clamp (pool, 0.0, 1.0);
}

// The key light on a dark floor (StageLook::floorSpot): brightest by the case,
// gone long before the stage edge. Display-encoded brightness.
float floorSpot (float gap, float strength)
{
    return strength * exp (-pow (gap / 0.085, 2.0)) * (1.0 - smoothstep (0.12, 0.25, gap));
}
