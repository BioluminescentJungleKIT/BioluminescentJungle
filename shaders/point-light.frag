#version 450

layout(location = 0) in vec3 fPosition;
layout(location = 1) in vec3 fIntensity;

layout(location = 0) out vec4 outColor;

layout(set = 1, binding = 0) uniform sampler2D albedo;
layout(set = 1, binding = 1) uniform sampler2D depth;

layout(set = 2, binding = 0) uniform DebugOptions {
    int showLightBoxes;
    int compositionMode;
} debug;

layout(set = 2, binding = 1) uniform UniformBufferObject {
    mat4 inverseMVP;
    float viewportWidth;
    float viewportHeight;
} transform;

vec3 calculatePosition() {
    float depth = texelFetch(depth, ivec2(gl_FragCoord), 0).r;

    // Convert screen coordinates to normalized device coordinates (NDC)
    vec4 ndc = vec4(
            (gl_FragCoord.x / transform.viewportWidth - 0.5) * 2.0,
            (gl_FragCoord.y / transform.viewportHeight - 0.5) * 2.0,
            (depth - 0.5) * 2.0, 1.0);

    // Convert NDC throuch inverse clip coordinates to view coordinates
    vec4 clip = transform.inverseMVP * ndc;
    return (clip / clip.w).xyz;
}

vec3 rndColor(vec3 pos) {
    pos = mat3(5.4, -6.3, 1.2, 3.2, 7.9, -2.8, -8.0, 1.5, 4.6) * pos;
    return abs(normalize(pos));
}

void main() {
    if (debug.showLightBoxes > 0) {
        outColor = vec4(rndColor(fPosition) * 0.2, 1.0);
        return;
    }

    if (debug.compositionMode == 0) {
        vec3 albedo = texelFetch(albedo, ivec2(gl_FragCoord), 0).rgb;
        // TODO: phong shading based on the lights
        outColor = vec4(albedo, 1.0);
    } else if (debug.compositionMode == 1) {
        outColor = vec4(texelFetch(albedo, ivec2(gl_FragCoord), 0).rgb, 1.0);
    } else if (debug.compositionMode == 2) {
        outColor = vec4(vec3(texelFetch(depth, ivec2(gl_FragCoord), 0).r), 1.0);
    } else {
        outColor = vec4(calculatePosition(), 1.0);
    }
}
