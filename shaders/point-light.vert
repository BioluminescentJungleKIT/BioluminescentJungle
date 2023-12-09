#version 450

layout(set = 0, binding = 0) uniform UniformBufferObject {
    mat4 modl;  // global
    mat4 view;
    mat4 proj;
    vec2 jitt;
} ubo;

layout(location = 0) in vec3 inPosition;
layout(location = 1) in vec3 inColor;
layout(location = 2) in float inIntensity;

layout(location = 0) out vec3 position;
layout(location = 1) out vec3 intensity;

void main() {
    vec4 clipPos = ubo.proj * ubo.view * ubo.modl * vec4(inPosition, 1.0);
    clipPos += vec4(ubo.jitt, 0, 0) * clipPos.w;
    position = (inverse(ubo.proj * ubo.view) * clipPos).xyz;

    intensity = inColor * inIntensity/250;
}
