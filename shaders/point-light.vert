#version 450

layout(location = 0) in vec3 inPosition;
layout(location = 1) in vec3 inColor;
layout(location = 2) in float inIntensity;

layout(location = 0) out vec3 position;
layout(location = 1) out vec3 intensity;

void main() {
    position = inPosition;
    // TODO: we could do that in the source code, or am I missing something?
    intensity = inColor * inIntensity;
}
