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

// The key light on a dark floor (StageLook::floorSpot): brightest by the case,
// gone long before the stage edge. Display-encoded brightness.
float floorSpot (float gap, float strength)
{
    return strength * exp (-pow (gap / 0.085, 2.0)) * (1.0 - smoothstep (0.12, 0.25, gap));
}
