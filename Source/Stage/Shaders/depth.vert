// Both map passes: the key light's shadow map (no colour attachment) and the
// top-down height map, which keeps model-space z in a filterable float target.
in vec3 aPos;
uniform mat4 uModel;
uniform mat4 uViewProj;
out float vHeight;

void main()
{
    vec4 p = uModel * vec4 (aPos, 1.0);
    vHeight = p.z;
    gl_Position = uViewProj * p;
}
