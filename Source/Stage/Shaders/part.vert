// Every device part and the ground quad. Attributes are bound by index:
// 0 aPos, 1 aNormal, 2 aColour, 3 aEmissive, 4 aUV, 5 aMaterial.
in vec3 aPos; in vec3 aNormal; in vec3 aColour; in float aEmissive; in vec2 aUV; in vec2 aMaterial;
uniform mat4 uModel;        // part -> world
uniform mat4 uPartToModel;  // part -> model (Z-up, metres)
uniform mat4 uViewProj;
out vec3 vNormal; out vec3 vColour; out float vEmissive; out vec2 vUV; out vec2 vMaterial;
out vec3 vWorld; out vec3 vModel; out vec3 vModelNormal;

void main()
{
    vec4 world = uModel * vec4 (aPos, 1.0);
    vWorld = world.xyz;
    vModel = (uPartToModel * vec4 (aPos, 1.0)).xyz;
    vNormal = normalize (mat3 (uModel) * aNormal);
    vModelNormal = normalize (mat3 (uPartToModel) * aNormal);
    vColour = aColour; vEmissive = aEmissive; vUV = aUV; vMaterial = aMaterial;
    gl_Position = uViewProj * world;
}
