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

layout(location = 0) in vec3 fragColor;
layout(location = 1) in vec3 fragNormal;
layout(location = 2) in float fragCamDist;
layout(location = 3) in float fragHeight;
layout(location = 4) in float fragIsWater;

layout(location = 0) out vec4 outColor;

void main() {
    vec3 n = normalize(fragNormal);
    vec3 l = normalize(ubo_global.sun_dir);

    float diffuse  = max(dot(n, l), 0.0);
    float wrap     = max(dot(n, l) * 0.5 + 0.5, 0.0) * 0.3; // soften shadow terminator
    vec3  ambient  = 0.12 * ubo_global.sky_col + 0.08 * vec3(0.4, 0.3, 0.2); // warm ground bounce

    // simple specular for water-ish areas
    vec3  view_dir = vec3(0.0, 0.0, -1.0); // approximate, good enough
    float spec     = 0.0;
    if (fragHeight < 2.0) {
        vec3 h = normalize(l + vec3(0,1,0));
        spec = pow(max(dot(n, h), 0.0), 32.0) * 0.4 * smoothstep(2.0, -5.0, fragHeight);
    }

    vec3 lit = fragColor * (ambient + ubo_global.sun_col * (diffuse + wrap));
    lit += ubo_global.sun_col * spec;

    // in frag shader, after computing lit:
    if (fragIsWater > 0.5) {
        // stronger specular on water
        vec3 l = normalize(ubo_global.sun_dir);
        vec3 h = normalize(l + vec3(0.0, 1.0, 0.0));
        float spec = pow(max(dot(normalize(fragNormal), h), 0.0), 128.0) * 0.8;
        lit += ubo_global.sun_col * spec;
        // slight transparency fake: blend toward deep color at grazing normals
        float fresnel = pow(1.0 - max(fragNormal.y, 0.0), 3.0);
        lit = mix(lit, vec3(0.04, 0.10, 0.22), fresnel * 0.5);
    }

    // subtle sky color bleed on surfaces facing up
    lit += fragColor * ubo_global.sky_col * 0.06 * max(n.y, 0.0);

    // fog with a slight blue-purple tint at distance
    float fog_t = clamp((fragCamDist - ubo_global.fog_start) /
            (ubo_global.fog_end - ubo_global.fog_start), 0.0, 1.0);
    fog_t = fog_t * fog_t; // squared falloff looks more natural
    vec3 fog_col = mix(ubo_global.sky_col, vec3(0.55, 0.60, 0.75), 0.3);
    vec3 final = mix(lit, fog_col, fog_t);

    outColor = vec4(final, 1.0);
}
