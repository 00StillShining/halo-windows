// The whole device in one draw (and the ground quad). Attributes by index:
// 0 aPos (model space, rest pose), 1 aNormal (octahedral), 2 aUV,
// 3 aBytes (material, slot, mask, ao; bytes fed as floats).
#include "slots.glsl"

in vec3 aPos; in vec2 aNormal; in vec2 aUV; in vec4 aBytes;
uniform mat4 uModel;          // model -> world
uniform mat4 uViewProj;
uniform sampler2D uMaterials; // 4 texels per material (Stage/HaloMesh.h)

out vec3 vNormal; out vec3 vColour; out vec3 vEmissive; out vec2 vUV; out vec2 vMaterial;
out vec3 vWorld; out vec3 vModel; out vec3 vModelNormal;
flat out int vFlags; flat out int vSlot; flat out int vMask; out float vAO; out float vTransmission;

void main()
{
    int material = int (aBytes.x + 0.5);
    int slot = int (aBytes.y + 0.5);
    vec4 t0 = texelFetch (uMaterials, ivec2 (0, material), 0);
    vec4 t1 = texelFetch (uMaterials, ivec2 (1, material), 0);
    vec4 t2 = texelFetch (uMaterials, ivec2 (2, material), 0);

    vec3 model = slotPosition (slot, aPos);
    vec3 modelNormal = slotNormal (slot, octDecode (aNormal));
    vec4 world = uModel * vec4 (model, 1.0);
    vWorld = world.xyz;
    vModel = model;
    vModelNormal = modelNormal;
    vNormal = normalize (mat3 (uModel) * modelNormal);
    vColour = t0.rgb;
    vMaterial = vec2 (t0.a, t1.a);
    vEmissive = t1.rgb;
    vFlags = int (t2.x + 0.5);
    vSlot = slot;
    vMask = int (aBytes.z + 0.5);
    vUV = aUV;
    vAO = aBytes.w / 255.0;
    vTransmission = t2.w;
    gl_Position = uViewProj * world;
}
