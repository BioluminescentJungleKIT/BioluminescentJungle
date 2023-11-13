#version 450

layout(set = 0, binding = 0) uniform UniformBufferObject {
    mat4 model;  // global
    mat4 view;
    mat4 proj;
} ubo;

layout(location = 0) in vec3 inPosition;
layout(location = 1) in vec3 inColor;
layout(location = 2) in float inIntensity;

layout(location = 0) out vec3 position;
layout(location = 1) out vec3 intensity;

void main() {
    position = (ubo.model * vec4(inPosition, 1.0)).xyz;
    // TODO: we could do that in the source code, or am I missing something?
    intensity = inColor * inIntensity;
}
