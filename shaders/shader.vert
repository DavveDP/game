#version 450

layout(set = 0, binding = 0) uniform UBO_Global {
    mat4 proj;
    mat4 view;
    vec3 sun_dir;
    vec3 sun_col;
    vec3 sky_col;
    float fog_start;
    float fog_end;
    float time;
} ubo_global;

struct InstanceData {
    float x;
    float z;
    float size;
    uint buf_index;
};

layout(set = 1, binding = 0) readonly buffer InstanceBuffer {
    InstanceData instances[];
};

layout(location = 0) in vec3 inPosition;
layout(location = 0) out vec3 fragColor;
layout(location = 1) out vec3 fragNormal;
layout(location = 2) out float fragCamDist;
layout(location = 3) out float fragHeight;
layout(location = 4) out float fragIsWater;

vec2 hash2(vec2 p) {
    p = vec2(dot(p, vec2(127.1, 311.7)), dot(p, vec2(269.5, 183.3)));
    return -1.0 + 2.0 * fract(sin(p) * 43758.5453123);
}

float gnoise(vec2 p) {
    vec2 i = floor(p);
    vec2 f = fract(p);
    vec2 u = f*f*(3.0-2.0*f);
    return mix(mix(dot(hash2(i+vec2(0,0)), f-vec2(0,0)),
                   dot(hash2(i+vec2(1,0)), f-vec2(1,0)), u.x),
               mix(dot(hash2(i+vec2(0,1)), f-vec2(0,1)),
                   dot(hash2(i+vec2(1,1)), f-vec2(1,1)), u.x), u.y);
}

float fbm(vec2 p, int octaves) {
    float v = 0.0, a = 0.5, f = 1.0;
    for (int i = 0; i < octaves; i++) {
        v += a * gnoise(p * f);
        f *= 2.1;
        a *= 0.48;
    }
    return v;
}

float sample_height(vec2 p) {
    float continent = fbm(p * 0.00008, 3);
    continent = smoothstep(-0.5, 0.6, continent); // wider range, more high terrain

    float hills = fbm(p * 0.0006, 3);

    float ridges = 0.0;
    float ra = 0.5, rf = 0.0005;
    for (int i = 0; i < 3; i++) {
        ridges += ra * (1.0 - abs(gnoise(p * rf)));
        rf *= 2.2; ra *= 0.45;
    }

    float h = continent * (
        5000.0 * hills +
        3500.0 * ridges * smoothstep(0.2, 0.6, continent)
    );
    h += 25.0 * fbm(p * 0.04, 3);
    return h;
}

// water: flatten below sea level and add time-based ripple
float sample_height_surface(vec2 p) {
    float h = sample_height(p);
    return h < 0.0 ? 0.0 : h;  // completely flat water, free
}

void main() {
    InstanceData id = instances[gl_InstanceIndex];
    vec2 world_pos_xz = vec2(id.x, id.z) + inPosition.xz * id.size;

    float raw_height = sample_height(world_pos_xz);  // for color decisions
    float height     = sample_height_surface(world_pos_xz);  // actual vertex position

    float eps = id.size * 0.004;
    float hR  = sample_height_surface(world_pos_xz + vec2(eps, 0.0));
    float hU  = sample_height_surface(world_pos_xz + vec2(0.0, eps));
    fragNormal = normalize(vec3(-(hR - height), 2.0 * eps, -(hU - height)));

    bool is_water = raw_height < 0.0;
    float slope = 1.0 - fragNormal.y;

    vec3 color_deep_water = vec3(0.04, 0.10, 0.22);
    vec3 color_shallow    = vec3(0.10, 0.22, 0.38);
    vec3 color_sand       = vec3(0.76, 0.70, 0.50);
    vec3 color_grass      = vec3(0.22, 0.38, 0.14);
    vec3 color_highland   = vec3(0.30, 0.28, 0.20);
    vec3 color_rock       = vec3(0.42, 0.38, 0.32);
    vec3 color_snow       = vec3(0.92, 0.94, 0.98);

    vec3 col;
    if (is_water) {
        float depth = clamp(-raw_height / 600.0, 0.0, 1.0);
        col = mix(color_shallow, color_deep_water, smoothstep(0.0, 1.0, depth));
    } else {
        col = color_grass;
        col = mix(col, color_sand,     smoothstep(80.0,  0.0,   raw_height)); // wide beach band
        col = mix(col, color_grass,    smoothstep(40.0, 120.0,  raw_height)); // grass overtakes sand
        col = mix(col, color_highland, smoothstep(800.0, 1800.0, raw_height));
        col = mix(col, color_rock,     smoothstep(0.3,   0.55,  slope));
        col = mix(col, color_snow,     smoothstep(2500.0, 3500.0, raw_height) * smoothstep(0.45, 0.25, slope));
    }

    fragColor   = col;
    fragIsWater = is_water ? 1.0 : 0.0;  // pass to frag shader
    fragHeight  = raw_height;
    fragCamDist = length(vec3(ubo_global.view * vec4(world_pos_xz.x, height, world_pos_xz.y, 1.0)));
    gl_Position = ubo_global.proj * ubo_global.view * vec4(world_pos_xz.x, height, world_pos_xz.y, 1.0);
}
