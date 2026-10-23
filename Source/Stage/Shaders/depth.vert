// Both map passes, the whole device in one draw: the key light's shadow map
// (no colour attachment) and the top-down height map, which keeps model-space
// z in a filterable float target. Same attributes as part.vert.
#include "slots.glsl"

in vec3 aPos; in vec2 aNormal; in vec2 aUV; in vec4 aBytes;
uniform mat4 uModel;     // model -> the pass's space
uniform mat4 uViewProj;
out float vHeight;

void main()
{
    vec3 model = slotPosition (int (aBytes.y + 0.5), aPos);
    vec4 p = uModel * vec4 (model, 1.0);
    vHeight = p.z;
    gl_Position = uViewProj * p;
}
