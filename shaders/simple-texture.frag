#version 450

layout(location = 0) in vec2 uv;
layout(location = 1) in vec3 normal;

layout(location = 0) out vec4 outColor;
layout(location = 1) out vec3 outNormal;

layout(set = 2, binding = 0) uniform sampler2D texSampler;

void main() {
    vec4 color = texture(texSampler, uv);
    outColor = color;
    outNormal = normalize(normal);
}
