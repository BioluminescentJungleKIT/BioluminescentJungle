#version 450

layout(location = 0) flat in vec3 fPosition;
layout(location = 1) flat in vec3 fIntensity;
layout(location = 2) flat in vec2 projPosition;

layout(location = 0) out vec4 outColor;

layout(set = 1, binding = 0) uniform sampler2D albedo;
layout(set = 1, binding = 1) uniform sampler2D depth;
layout(set = 1, binding = 2) uniform sampler2D normal;
layout(set = 1, binding = 3) uniform sampler2D motion;

#include "util.glsl"

layout(set = 2, binding = 0, std140) uniform DebugOptions {
    int showLightBoxes;
    int compositionMode;
    float radius;
} debug;

layout(set = 2, binding = 1, std140) uniform LightInfoBlock {
    SceneLightInfo info;
};

vec3 rndColor(vec3 pos) {
    pos = mat3(5.4, -6.3, 1.2, 3.2, 7.9, -2.8, -8.0, 1.5, 4.6) * pos;
    return abs(normalize(pos));
}

void main() {
    if (debug.showLightBoxes > 0) {
        outColor = vec4(rndColor(fPosition) * 0.2, 1.0);
        if (length(gl_FragCoord.xy - projPosition) < 7) {
            // Draw the point lights themselves
            // TODO: we might want to remove this, I doubt this is very performant, but it is useful for debugging.
            outColor = vec4(fIntensity, 1.0);
        }
        return;
    }

    float depth = texelFetch(depth, ivec2(gl_FragCoord), 0).r;
    vec3 fragmentWorldPos = calculatePosition(depth, gl_FragCoord.xy, info);

    if (debug.compositionMode == 7) {
        SurfacePoint point;
        point.worldPos = fragmentWorldPos;
        point.albedo = texelFetch(albedo, ivec2(gl_FragCoord), 0).rgb;
        point.N = texelFetch(normal, ivec2(gl_FragCoord), 0).xyz;

        PointLightParams light;
        light.pos = fPosition;
        light.intensity = fIntensity;
        light.r = debug.radius;

        outColor = vec4(evalPointLight(point, light, info), 1.0);
    } else if (debug.compositionMode == 1) {
        outColor = vec4(texelFetch(albedo, ivec2(gl_FragCoord), 0).rgb, 1.0);
    } else if (debug.compositionMode == 2) {
        outColor = vec4(vec3(pow(depth, 70)), 1.0);
    } else if (debug.compositionMode == 3) {
        outColor = vec4(fragmentWorldPos, 1.0);
    } else if (debug.compositionMode == 4) {
        outColor = vec4(texelFetch(normal, ivec2(gl_FragCoord), 0).xyz, 1.0);
    } else if (debug.compositionMode == 5) {
        outColor = vec4(texelFetch(motion, ivec2(gl_FragCoord), 0).rg, 0.0, 1.0) * 100;
    } else if (debug.compositionMode == 6) {
        vec4 normal = texelFetch(normal, ivec2(gl_FragCoord), 0);
        if (normal.a > 0) {
            outColor = vec4(1.0);
        } else {
            outColor = vec4(0.0);
        }
    }
}
