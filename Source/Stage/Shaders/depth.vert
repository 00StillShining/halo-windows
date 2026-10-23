// The key light's shadow map, the whole device in one draw: hardware depth
// for the filtered comparison taps, and the same depth in a float colour
// target for the soft shadow's blocker search. Same attributes as part.vert.
#include "slots.glsl"

in vec3 aPos; in vec2 aNormal; in vec2 aUV; in vec4 aBytes;
uniform mat4 uModel;     // model -> the pass's space
uniform mat4 uViewProj;
out float vDepth;

void main()
{
    vec3 model = slotPosition (int (aBytes.y + 0.5), aPos);
    vec4 p = uModel * vec4 (model, 1.0);
    gl_Position = uViewProj * p;
    vDepth = gl_Position.z / gl_Position.w * 0.5 + 0.5;   // orthographic: as stored
}
