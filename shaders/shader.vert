#version 450

layout(set = 0, binding = 0) uniform UniformBufferObject {
    mat4 modl;  // global
    mat4 view;
    mat4 proj;
    vec2 jitt;
} ubo;

layout(set = 1, binding = 0) buffer ModelTransform {
    mat4 model[];
} model;

layout(location = 0) in vec3 inPosition;
layout(location = 1) in vec4 inColor;
layout(location = 2) in vec3 inNormal;

layout(location = 0) out vec3 fragColor;
layout(location = 1) out vec3 normal;

void main() {
    gl_Position = ubo.proj * ubo.view * ubo.modl * model.model[gl_InstanceIndex] * vec4(inPosition, 1.0);
    gl_Position += gl_Position.w * vec4(ubo.jitt.x, ubo.jitt.y, 0, 0);
    fragColor = inColor.rgb;
    normal = (transpose(inverse(ubo.modl * model.model[gl_InstanceIndex])) * vec4(inNormal, 0.0)).xyz;
}
