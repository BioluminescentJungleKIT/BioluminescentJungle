// The content of this file is licensed under MIT.
// Copyright (c) 2024 Ilia Bozhinov, Lars Erber.

#version 450

layout(set = 0, binding = 0) uniform UniformBufferObject {
    mat4 modl;  // global
    mat4 view;
    mat4 proj;
    vec2 jitt;
    float time;
} ubo;

layout(set = 0, binding = 1) uniform UniformBufferObject2 {
    mat4 modl;  // global
    mat4 view;
    mat4 proj;
    vec2 jitt;
    float time;
} lastubo;

layout(set = 1, binding = 0) buffer ModelTransform {
    mat4 model[];
} model;

layout(location = 0) in vec3 inPosition;
layout(location = 1) in vec2 inUV;
layout(location = 2) in vec3 inNormal;

layout(location = 0) out vec2 uv;
layout(location = 1) out vec3 normal;
layout(location = 2) out vec3 fsPos;
layout(location = 3) out vec4 fsPosClipSpace;
layout(location = 4) out vec4 fsOldPosClipSpace;

void main() {
    mat4 M = ubo.view * ubo.modl * model.model[gl_InstanceIndex];
    vec4 pos = M * vec4(inPosition, 1.0);
    fsPos = pos.xyz;

    gl_Position = ubo.proj * pos;
    gl_Position += gl_Position.w * vec4(ubo.jitt, 0, 0);
    fsPosClipSpace = gl_Position;

    fsOldPosClipSpace = lastubo.proj * lastubo.view * lastubo.modl * model.model[gl_InstanceIndex] * vec4(inPosition, 1.0);
    fsOldPosClipSpace += fsOldPosClipSpace.w * vec4(ubo.jitt, 0, 0);

    uv = inUV + vec2(0, -0.1) * ubo.time; // this fucks with motion vectors, but I don't care
    normal = (transpose(inverse(M)) * vec4(inNormal, 0.0)).xyz;
}
