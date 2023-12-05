#version 450

layout(location = 0) in vec2 uv;
layout(location = 1) in vec3 normal;
layout(location = 2) in vec4 currpos;
layout(location = 3) in vec4 lastpos;

layout(location = 0) out vec4 outColor;
layout(location = 1) out vec3 outNormal;
layout(location = 2) out vec2 outMotion;

layout(set = 2, binding = 0) uniform sampler2D texSampler;

float rand(vec2 co){
    return fract(sin(dot(co, vec2(12.9898, 78.233))) * 43758.5453);
}

void main() {
    vec4 color = texture(texSampler, uv);
    // alpha hashing
    if ( color.a <= rand(uv * 10000 + gl_FragCoord.xy) ) discard;
    outColor = color;
    outNormal = normalize(normal);
    outMotion = (lastpos/lastpos.w - currpos/currpos.w).xy;
}
