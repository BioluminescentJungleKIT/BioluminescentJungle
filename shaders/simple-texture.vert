#version 450

layout(set = 0, binding = 0) uniform UniformBufferObject {
    mat4 model;  // global
    mat4 view;
    mat4 proj;
} ubo;

layout(set = 1, binding = 0) buffer ModelTransform {
    mat4 model[];
} model;

layout(location = 0) in vec3 inPosition;
layout(location = 1) in vec2 inUV;

layout(location = 0) out vec2 uv;

void main() {
    gl_Position = ubo.proj * ubo.view * ubo.model * model.model[gl_InstanceIndex] * vec4(inPosition, 1.0);
    uv = inUV;
}
