#version 450

layout(location = 0) in vec4 fragColor;
layout(location = 1) in vec3 normal;
layout(location = 2) in vec4 currpos;
layout(location = 3) in vec4 lastpos;

layout(location = 0) out vec4 outColor;
layout(location = 1) out vec4 outNormal;
layout(location = 2) out vec2 outMotion;
layout(location = 3) out vec4 outEmission;

void main() {
    outColor = fragColor;
    outEmission = fragColor;
    outNormal = vec4(normalize(normal), 0.0);
    outMotion = (lastpos/lastpos.w - currpos/currpos.w).xy;
}
