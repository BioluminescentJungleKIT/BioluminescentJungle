// The content of this file is licensed under MIT.
// Copyright (c) 2024 Ilia Bozhinov, Lars Erber.

#version 450

layout(location = 0) in vec2 uv;
layout(location = 1) in vec3 normal;
layout(location = 2) in vec4 currpos;
layout(location = 3) in vec4 lastpos;

layout(location = 0) out vec4 outColor;
layout(location = 1) out vec4 outNormal;
layout(location = 2) out vec2 outMotion;
layout(location = 3) out vec4 outEmission;

layout(set = 2, binding = 0) uniform sampler2D texSampler;
layout(set = 2, binding = 1) uniform sampler2D emitTexSampler;

float rand(vec2 co){
    return fract(sin(dot(co, vec2(12.9898, 78.233))) * 43758.5453);
}

void main() {
    float random = rand(uv * 10000 + gl_FragCoord.xy);
    vec4 color = texture(texSampler, uv);
    if (color.a < 0.05 || (color.a < 0.95 && color.a <= random)) discard;
    outEmission = vec4(texture(emitTexSampler, uv).rgb, 2.0/255);  // fixed strength with textures for now
    outColor = vec4(color.rgb, 0.0);
    outNormal = vec4(normalize(normal), 0.0);
    outMotion = (lastpos/lastpos.w - currpos/currpos.w).xy;
}
