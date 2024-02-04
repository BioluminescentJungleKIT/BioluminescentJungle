// The content of this file is licensed under MIT.
// Copyright (c) 2024 Ilia Bozhinov, Lars Erber.

#version 450
#include "wind.glsl"

layout(set = 0, binding = 0) uniform UniformBufferObject {
    mat4 modl;  // global
    mat4 view;
    mat4 proj;
    vec2 jitt;
    float time;
} ubo;

layout(location = 0) in vec3 inPosition;
layout(location = 1) in vec3 inColor;
layout(location = 2) in float inIntensity;
layout(location = 3) in float wind;

layout(location = 0) out vec3 position;
layout(location = 1) out vec3 intensity;

void main() {
    vec4 worldPos = ubo.modl * vec4(inPosition, 1.0);
    worldPos = windDisplacement(worldPos, ubo.time, worldPos.xyz + vec3(0,0,-0.25), wind);
    vec4 clipPos = ubo.proj * ubo.view * worldPos;
    clipPos += vec4(ubo.jitt, 0, 0) * clipPos.w;
    position = (inverse(ubo.proj * ubo.view) * clipPos).xyz;

    intensity = inColor * inIntensity/55;
}
