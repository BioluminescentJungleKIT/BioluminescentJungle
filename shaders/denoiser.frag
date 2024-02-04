// The content of this file is licensed under MIT.
// Copyright (c) 2024 Ilia Bozhinov, Lars Erber.

#version 450

#include "util.glsl"

layout(location = 0) out vec4 outColor;

layout(set = 0, binding = 0) uniform sampler2D accColor;

layout(set = 0, binding = 1) uniform DenoiserUBO {
    vec4 weights[25];
    ivec4 offsets[25];
    mat4 inverseV;
    int iterCnt;
    float albedoSigma;
    float normalSigma;
    float positionSigma;
} denoiser;

layout(set = 0, binding = 2) uniform sampler2D albedo;
layout(set = 0, binding = 3) uniform sampler2D depth;
layout(set = 0, binding = 4) uniform sampler2D normal;
layout(set = 0, binding = 5) uniform sampler2D motion;

layout(push_constant) uniform constants
{
    int iterNumber;
    int multAlbedo;
};

void main() {
    ivec2 pos = ivec2(gl_FragCoord);
    vec3 mAlbedo = texelFetch(albedo, pos, 0).rgb;
    vec4 multiplier = vec4(1.0);
    if (multAlbedo > 0) {
        multiplier = vec4(mAlbedo, 1.0);
    }

    float mD = texelFetch(depth, pos, 0).x;
    if (denoiser.iterCnt < 1 || mD > 0.999f) {
        outColor = texelFetch(accColor, pos, 0) * multiplier;
        return;
    }

    vec3 mPos = calculatePositionFromUV(mD, pos / vec2(textureSize(accColor, 0)), denoiser.inverseV);
    vec3 mNormal = texelFetch(normal, pos, 0).xyz;

    vec4 color = vec4(0);
    float sumW = 0;

    const int stepwidth = (1 << iterNumber);
    for (int i = 0; i < 25; i++) {
       // ivec2 other = pos + denoiser.offsets[i].xy * (1 << (denoiser.iterCnt - iterNumber - 1));
        ivec2 other = pos + denoiser.offsets[i].xy * stepwidth;
        if (any(lessThan(other, ivec2(0, 0))) ||
            any(greaterThanEqual(other, textureSize(accColor, 0)))) {
            continue;
        }

        float oD = texelFetch(depth, other, 0).x;
        if (oD > 0.999f) {
            // Background
            continue;
        }

        vec3 oPos = calculatePositionFromUV(oD, pos / vec2(textureSize(albedo, 0)), denoiser.inverseV);
        vec3 oAlbedo = texelFetch(albedo, other, 0).rgb;
        vec3 oNormal = texelFetch(normal, other, 0).xyz;

        vec3 deltaPos = oPos - mPos;
        vec3 deltaAlbedo = oAlbedo.rgb - mAlbedo.rgb;
        vec3 deltaNormal = oNormal - mNormal;

        float sigmaN = denoiser.normalSigma / (stepwidth * stepwidth);
        float w1 = min(exp(-dot(deltaAlbedo, deltaAlbedo) / denoiser.albedoSigma), 1.0);
        float w2 = min(exp(-dot(deltaNormal, deltaNormal) / sigmaN), 1.0);
        float w3 = min(exp(-dot(deltaPos, deltaPos) / denoiser.positionSigma), 1.0);

        float W = w1 * w2 * w3 * denoiser.weights[i].x;
        color += texelFetch(accColor, other, 0) * W;
        sumW += W;
    }

    outColor = color / sumW * multiplier;
}
