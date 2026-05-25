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
layout(location = 0) out vec4 outColor;

void main() {
    float diffuse = max(dot(fragNormal, ubo_global.sun_dir), 0.0);
    vec3 ambient = 0.15 * ubo_global.sky_col;

    vec3 lit = fragColor * (ambient + ubo_global.sun_col * diffuse);

    // fog
    float fog = clamp((fragCamDist - ubo_global.fog_start) / (ubo_global.fog_end - ubo_global.fog_start), 0.0, 1.0);
    vec3 final = mix(lit, ubo_global.sky_col, fog);

    outColor = vec4(final, 1.0);
}
