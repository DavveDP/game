#version 450

layout(set = 0, binding = 0) uniform UBO_Global {
    mat4 proj;
    mat4 view;
    float time;
} ubo_global;

struct NoiseParams {
    // fbm
    float H;
    uint octaves;
    
    // height design function
    uint first_segment_index;
    uint segment_count;
};

layout(set = 1, binding = 0) uniform UBO_Terrain {
    // mesh related globals
    uint grid_size;
    float y_scale;

    NoiseParams A, B, C;
} ubo_terrain;

struct SplineSegment {
    vec2 p0, p1, p2, p3;
};

layout(set = 1, binding = 1) readonly buffer SplineSegmentBuffer {
    SplineSegment segments[];
};

struct InstanceData {
    float x;
    float z;
    float size;
    uint buf_index;
};

layout(set = 1, binding = 2) readonly buffer InstanceBuffer {
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

// Gradient Noise by Inigo Quilez - iq/2013
// https://www.shadertoy.com/view/XdXGW8
float noise(vec2 st) {
    vec2 i = floor(st);
    vec2 f = fract(st);

    vec2 u = f*f*(3.0-2.0*f);

    return mix( mix( dot( hash2_2(i + vec2(0.0,0.0) ), f - vec2(0.0,0.0) ),
                     dot( hash2_2(i + vec2(1.0,0.0) ), f - vec2(1.0,0.0) ), u.x),
                mix( dot( hash2_2(i + vec2(0.0,1.0) ), f - vec2(0.0,1.0) ),
                     dot( hash2_2(i + vec2(1.0,1.0) ), f - vec2(1.0,1.0) ), u.x), u.y);
}


float fbm2( in vec2 x, in float H, uint octaves)
{    
    float G = exp2(-H);
    float f = 1.0;
    float d = 1.0;
    float t = 0.0;
    float norm = 0.0;
    for( int i=0; i<octaves; i++ )
    {
        t += d*noise(f*x);
        norm += d;
        f *= 2.0;
        d *= G;
    }
    t /= norm;
    t = t * 0.5 + 0.5;
    return t;
}

vec2 hash2_2_int(ivec2 p) {
    uvec2 q = uvec2(p);
    q = q * uvec2(1664525u, 1013904223u) + q.yx;
    q.x += q.y * 1664525u;
    q.y += q.x * 1013904223u;
    return vec2(q) / float(0xFFFFFFFFu) * 2.0 - 1.0;
}


float domain_warp(in vec2 p, float H, uint octaves) {

    vec2 q = vec2( fbm2( p + vec2(0.0,0.0), H, octaves ),
                   fbm2( p + vec2(5.2,1.3), H, octaves ) );

    vec2 r = vec2( fbm2( p + 4.0*q + vec2(1.7,9.2), H, octaves ),
                   fbm2( p + 4.0*q + vec2(8.3,2.8), H, octaves ));

    return fbm2( p + 4.0*r, H, octaves );
}

float eval_spline(uint start_index, uint count, float x) {
    uint index = start_index;
    float height = 0.0;
    for (uint i = 0; i < count; i++) {
        SplineSegment s = segments[index];
        float lo = s.p0.x; 
        float hi = s.p3.x;
        float t = (x - lo) / (hi - lo);
        float t_1 = 1 - t;
        
        float y = 
            t_1 * t_1 * t_1 * s.p0.y +
            3.0 * t_1 * t_1 * t * s.p1.y +
            3.0 * t_1 * t * t * s.p2.y +
            t * t * t * s.p3.y;

        float m = step(lo, x) * step(x, hi); // masks the segment
        height += m*y;
        index++;
    }
    return height;
}

void main() {
    // patch placement
    InstanceData id = instances[gl_InstanceIndex];
    vec2 patch_origin = vec2(id.x, id.z);           // patch origin is in the center of the patch
    vec2 world_pos_xz = patch_origin + inPosition.xz * id.size; // assume model origin is also in the center of the geometry


    // noise (y offset)

    // for each noise (A, B, C) unrolled (using H and octaves, and random offset)
    // A: lower freq, domain warp
    // B and C: normal fbm
    // for each loop over segments, starting at starting index, n times
    //evaluate poly a, b, c, d multiplying by step with hi, lo and summing
    float a = smoothstep(0.1, 0.9, fbm2(world_pos_xz * 0.005, 0.5, 3));
    float b = smoothstep(0.2, 0.8, fbm2(world_pos_xz * 0.02, 0.5, 2));
    float c = smoothstep(0.2, 0.8, fbm2(world_pos_xz * 0.5, 0.5, 1));
    c *= c;



    // use spline-based height functions
    float height = 0.0;
    height += eval_spline(ubo_terrain.A.first_segment_index, ubo_terrain.A.segment_count, a);
    //height += b;//eval_spline(ubo_terrain.B.first_segment_index, ubo_terrain.B.segment_count, b);
    //height += c;//eval_spline(ubo_terrain.C.first_segment_index, ubo_terrain.C.segment_count, c);

    float s =
        step(0.077586, a) +
        step(0.267241, a) +
        step(0.301724, a) +
        step(0.461207, a) +
        step(0.517241, a) +
        step(0.560345, a) +
        step(0.767241, a);

    vec3 colors[8] = vec3[](
            vec3(1,0,0), // seg0 (red)
            vec3(1,0.5,0), // orange
            vec3(1,1,0), // yellow
            vec3(0,1,0), // green
            vec3(0,1,1), // cyan
            vec3(0,0,1), // blue
            vec3(1,0,1), // magenta
            vec3(1,1,1)  // seg7 (white)
            );



    fragColor = vec3(colors[int(s)]);

    //float height = b;

    gl_Position = ubo_global.proj * ubo_global.view * vec4(world_pos_xz.x, height, world_pos_xz.y, 1.0);

    //fragColor = vec3(0.2, 0.8, 0.2);
}
