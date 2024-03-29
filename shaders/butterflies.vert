// The content of this file is licensed under MIT.
// Copyright (c) 2024 Ilia Bozhinov, Lars Erber.

#version 450

#include "util.glsl"
#include "noise3D.glsl"

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

layout(set = 1, binding = 0) buffer Bufferflies
{
    PointLight utterflies[];
} b;

layout(location = 0) in vec3 inPosition;
layout(location = 1) in vec2 inUV;
layout(location = 2) in vec3 inNormal;

layout(location = 0) out vec2 uv;
layout(location = 1) out vec3 normal;
layout(location = 2) out vec4 currpos;
layout(location = 3) out vec4 lastpos;

mat4 computeModel(PointLight butterfly, float timeDelta) {
    mat4 model;

    vec3 p = butterfly.position.xyz - butterfly.velocity.xyz * timeDelta;

    vec3 f = normalize(butterfly.velocity.xyz);  // forward
    vec3 s = normalize(cross(vec3(0, 0, 1), f)); // up x forward = sideways
    vec3 u  = cross(f, s); // forward x sideways = up

    model[0].xyz = s;
    model[0].w = 0;
    model[1].xyz = f;
    model[1].w = 0;
    model[2].xyz = u;
    model[2].w = 0;
    model[3].xyz = p;
    model[3].w = 1;
    return model;
}

vec3 flap(vec3 inpos, float time) {
    inpos.z *= sin(ubo.time * 10 + (snoise(vec3(ubo.time * 0.6 + 100 * gl_InstanceIndex))+1) * 2);
    return inpos;
}

void main() {
    mat4 model = computeModel(b.utterflies[gl_InstanceIndex], 0);
    // TODO [optimization] outsource uniform multiplications to the CPU
    gl_Position = ubo.proj * ubo.view * ubo.modl * model * vec4(flap(inPosition, ubo.time), 1.0);
    gl_Position += gl_Position.w * vec4(ubo.jitt, 0, 0);
    currpos = gl_Position;

    lastpos = lastubo.proj * lastubo.view * lastubo.modl
              * computeModel(b.utterflies[gl_InstanceIndex], lastubo.time - ubo.time)
              * vec4(flap(inPosition, lastubo.time), 1.0);
    lastpos += lastpos.w * vec4(ubo.jitt, 0, 0);

    uv = inUV;
    normal = (transpose(inverse(ubo.modl * model)) * vec4(inNormal, 0.0)).xyz;
}
