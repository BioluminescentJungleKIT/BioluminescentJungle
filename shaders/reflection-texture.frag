#version 450

layout(location = 0) in vec2 uv;
layout(location = 1) in vec3 normal;
layout(location = 2) in vec4 currpos;
layout(location = 3) in vec4 lastpos;

layout(location = 0) out vec4 outColor;
layout(location = 1) out vec4 outNormal;
layout(location = 2) out vec2 outMotion;

layout(set = 2, binding = 0) uniform sampler2D texSampler;

void main() {
    vec4 color = texture(texSampler, uv);

    outColor = color;
    // Write 1.0 as normal.w, which means surface is reflective
    outNormal = vec4(normalize(normal), 1.0);
    outMotion = (lastpos/lastpos.w - currpos/currpos.w).xy;
}
