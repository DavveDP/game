#version 450

layout(set = 0, binding = 0) uniform UBO_Global {
    mat4 proj;
    mat4 view;
    float time;
} ubo_global;

layout(set = 1, binding = 0) uniform UBO_Terrain {
    // noise params will go here
    uint a;
    uint b;
} ubo_terrain;

struct InstanceData {
    float x;
    float z;
    float size;
    uint buf_index;
};

layout(set = 1, binding = 1) readonly buffer InstanceBuffer {
    InstanceData instances[];
};

float rand(float x)
{
    return fract(sin(x * 2.9898)) * 43758.5453;
}

// Simple Hash
// Source - https://stackoverflow.com/a/70620975
// Posted by dividebyzero
// Retrieved 2026-04-27, License - CC BY-SA 4.0
//bias: 0.17353355999581582 ( very probably the best of its kind )
uint lowbias32(uint x)
{
    x ^= x >> 16;
    x *= 0x7feb352dU;
    x ^= x >> 15;
    x *= 0x846ca68bU;
    x ^= x >> 16;
    return x;
}

layout(location = 0) in vec3 inPosition;
//layout(location = 1) in vec3 inColor;

layout(location = 0) out vec3 fragColor;

void main() {
    // random displacement
    //float rand = sin(gl_VertexIndex);
    float rand = rand(gl_VertexIndex + ubo_global.time) / 100000.0;

    InstanceData id = instances[gl_InstanceIndex];
    vec2 patch_origin = vec2(id.x, id.z);           // patch origin is in the center of the patch
    vec2 pos_xz = patch_origin + inPosition.xz * id.size; // assume model origin is also in the center of the geometry

    //float offset = (rand + 0.5) * 0.2; // range ~[-0.005, 0.005]
    //float offset = 0.02 * sin(ubo_global.time + gl_VertexIndex);

    // projection
    gl_Position = ubo_global.proj * ubo_global.view * vec4(pos_xz.x, 0, pos_xz.y, 1.0);

    //fragColor = vec3(inPosition.xz, .0);
    fragColor = vec3(0.2, 1.0, 0.2);

}
