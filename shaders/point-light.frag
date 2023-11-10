#version 450

layout(location = 0) in vec3 fPosition;
layout(location = 1) in vec3 fIntensity;

layout(location = 0) out vec4 outColor;

layout(set = 0, binding = 0) uniform UniformBufferObject {
    mat4 model;  // global
    mat4 view;
    mat4 proj;
} ubo;

layout(set = 1, binding = 0) uniform sampler2D albedo;
layout(set = 1, binding = 1) uniform sampler2D depth;

layout(set = 2, binding = 0) uniform DebugOptions {
    int showLightBoxes;
    int compositionMode;
} debug;

vec3 calculatePosition() {
    float depth = texelFetch(depth, ivec2(gl_FragCoord), 0).r;
    // TODO
    return vec3(depth);
}

// "random" color from gl_PrimitiveID
vec3 rndColor() {
    return vec3(fract(gl_PrimitiveID * 12137.1629),
            fract(gl_PrimitiveID * 6203620.2467),
            fract(gl_PrimitiveID * 90286239.27303));
}

void main() {
    if (debug.showLightBoxes > 0) {
        outColor = vec4(rndColor() * 0.2, 0.2);
    } else {
        outColor = vec4(0.0);
    }

    if (debug.compositionMode == 0) {
        vec3 albedo = texelFetch(albedo, ivec2(gl_FragCoord), 0).rgb;
        // TODO: phong shading based on the lights
        outColor += vec4(albedo, 1.0);
    } else if (debug.compositionMode == 1) {
        outColor += vec4(texelFetch(albedo, ivec2(gl_FragCoord), 0).rgb, 1.0);
    } else {
        outColor += vec4(vec3(texelFetch(depth, ivec2(gl_FragCoord), 0).r), 1.0);
    }
}
