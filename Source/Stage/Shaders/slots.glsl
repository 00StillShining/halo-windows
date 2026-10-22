// Per-slot motion for the single-draw model (plan D8): slot 0 is the static
// device; slots 1-12 the numeric pads, 13-16 the group pads. Each slot turns
// about its pivot by a quaternion and then moves by an offset (model metres).
// Translation and rotation only: normals turn with the same quaternion.
uniform vec3 uSlotOffset[32];
uniform vec4 uSlotRot[32];     // x, y, z, w
uniform vec3 uSlotPivot[32];

vec3 rotateByQuat (vec4 q, vec3 v)
{
    return v + 2.0 * cross (q.xyz, cross (q.xyz, v) + q.w * v);
}

vec3 slotPosition (int slot, vec3 p)
{
    return rotateByQuat (uSlotRot[slot], p - uSlotPivot[slot]) + uSlotPivot[slot] + uSlotOffset[slot];
}

vec3 slotNormal (int slot, vec3 n)
{
    return rotateByQuat (uSlotRot[slot], n);
}

vec3 octDecode (vec2 e)
{
    vec3 v = vec3 (e, 1.0 - abs (e.x) - abs (e.y));
    if (v.z < 0.0)
        v.xy = (1.0 - abs (v.yx)) * vec2 (v.x >= 0.0 ? 1.0 : -1.0, v.y >= 0.0 ? 1.0 : -1.0);
    return normalize (v);
}
