#version 450

layout(location = 0) in vec3 fragColor;
layout(location = 1) in vec3 normal;

layout(location = 0) out vec4 outColor;
layout(location = 1) out vec4 outNormal;

void main() {
    outColor = vec4(fragColor, 1.0);
    outNormal = vec4(normalize(normal), 0.0);
}
