#version 450

layout(set = 0, binding = 0) uniform UBO_Global {
    mat4 proj;
    mat4 view;
    float time;
} ubo_global;

layout(set = 1, binding = 0) uniform UBO_Terrain {
    // noise params will go here
    uint grid_size;
    float y_scale;
    // fbm
    float H;
    uint octaves;
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

// pipeline in/out
layout(location = 0) in vec3 inPosition;
layout(location = 0) out vec3 fragColor;

float hash3_1(vec3 p) {
    p = fract(p * 0.3183099 + 0.1);
    p *= 17.0;
    return fract(p.x * p.y * p.z * (p.x + p.y + p.z));
}

vec2 hash2_2(vec2 p) {
    p = vec2(dot(p, vec2(127.1, 311.7)),
             dot(p, vec2(269.5, 183.3)));
    return -1.0 + 2.0 * fract(sin(p) * 43758.5453123);
}

// returns 3D gradient noise (in .x) and its derivatives (in .yz)
vec3 noised( in vec2 x )
{
    vec2 i = floor( x );
    vec2 f = fract( x );

    vec2 u = f*f*f*(f*(f*6.0-15.0)+10.0);
    vec2 du = 30.0*f*f*(f*(f-2.0)+1.0);
    
    vec2 ga = hash2_2( i + vec2(0.0,0.0) );
    vec2 gb = hash2_2( i + vec2(1.0,0.0) );
    vec2 gc = hash2_2( i + vec2(0.0,1.0) );
    vec2 gd = hash2_2( i + vec2(1.0,1.0) );
    
    float va = dot( ga, f - vec2(0.0,0.0) );
    float vb = dot( gb, f - vec2(1.0,0.0) );
    float vc = dot( gc, f - vec2(0.0,1.0) );
    float vd = dot( gd, f - vec2(1.0,1.0) );

    return vec3( va + u.x*(vb-va) + u.y*(vc-va) + u.x*u.y*(va-vb-vc+vd),   // value
                 ga + u.x*(gb-ga) + u.y*(gc-ga) + u.x*u.y*(ga-gb-gc+gd) +  // derivatives
                 du * (u.yx*(va-vb-vc+vd) + vec2(vb,vc) - va));
}

float fbm( in vec2 x, in float H, uint octaves )
{    
    float G = exp2(-H);
    float f = 1.0;
    float a = 1.0;
    float t = 0.0;
    for( int i=0; i<octaves; i++ )
    {
        t += a*noised(f*x).x; // throwing away derivatives for now
        f *= 2.0;
        a *= G;
    }
    return t;
}

float domain_warp(in vec2 p, float H, uint octaves) {

    vec2 q = vec2( fbm( p + vec2(0.0,0.0), H, octaves ),
                   fbm( p + vec2(5.2,1.3), H, octaves ) );

    vec2 r = vec2( fbm( p + 4.0*q + vec2(1.7,9.2), H, octaves ),
                   fbm( p + 4.0*q + vec2(8.3,2.8), H, octaves ));

    return fbm( p + 4.0*r, H, octaves );
}

void main() {
    // patch placement
    InstanceData id = instances[gl_InstanceIndex];
    vec2 patch_origin = vec2(id.x, id.z);           // patch origin is in the center of the patch
    vec2 pos_xz = patch_origin + inPosition.xz * id.size; // assume model origin is also in the center of the geometry

    // y offset
    float scale = 4.0 / float(ubo_terrain.grid_size);
    float y = domain_warp(pos_xz * scale, ubo_terrain.H, ubo_terrain.octaves);
    y *= ubo_terrain.y_scale;

    // projection
    gl_Position = ubo_global.proj * ubo_global.view * vec4(pos_xz.x, y, pos_xz.y, 1.0);

    //fragColor = vec3(inPosition.xz, .0);
    fragColor = vec3(0.2, 1.0, 0.2);

}
