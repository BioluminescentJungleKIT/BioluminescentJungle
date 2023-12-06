#version 450

layout(location = 0) out vec4 outColor;

layout(set = 0, binding = 0) uniform sampler2D texSampler;

layout(set = 0, binding = 1) uniform GlobalFogUBO {
    vec3 color;
    float ambientFactor;
    float brightness;
    float absorption;
    float near;
    float far;
} fog;

layout(set = 0, binding = 2) uniform sampler2D albedo;
layout(set = 0, binding = 3) uniform sampler2D depth;
layout(set = 0, binding = 4) uniform sampler2D normal;
layout(set = 0, binding = 5) uniform sampler2D motion;

void main() {
    vec4 color = texelFetch(texSampler, ivec2(gl_FragCoord), 0);
    vec4 albedo = texelFetch(albedo, ivec2(gl_FragCoord), 0);
    float d = 1 - texelFetch(depth, ivec2(gl_FragCoord), 0).x;
    float world_depth = (fog.far*fog.near)/(d * (fog.far - fog.near) - d);
    vec3 ambientLight = fog.ambientFactor * fog.color;
    vec3 fogLight = fog.brightness * fog.color;
    color.rgb += albedo.rgb * ambientLight;
    color.rgb *= exp(-fog.absorption*world_depth);
    // depth illumination: integral from 0 to depth: color * exp(-absorption*x) dx
    if (fog.absorption != 0)
        color.rgb += (fogLight - fogLight * exp(-fog.absorption * world_depth))/fog.absorption;
    outColor = vec4(color.rgb, 1);
}
