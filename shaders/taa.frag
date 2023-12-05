#version 450

layout(location = 0) out vec4 outColor;

layout(set = 0, binding = 0) uniform sampler2D currentFrame;

layout(set = 0, binding = 1) uniform TAAUBO {
    float alpha;
    uint widht;
    uint height;
} taa;

layout(set = 0, binding = 2) uniform sampler2D albedo;
layout(set = 0, binding = 3) uniform sampler2D depth;
layout(set = 0, binding = 4) uniform sampler2D normal;
layout(set = 0, binding = 5) uniform sampler2D motion;

layout(set = 0, binding = 6) uniform sampler2D lastFrame;

void main() {
    vec2 pos = gl_FragCoord.xy;
    vec2 oldpos = pos + texelFetch(motion, ivec2(pos), 0).xy * ivec2(taa.widht, taa.height);
    vec4 prevColor = texelFetch(lastFrame, ivec2(oldpos), 0);
    vec4 newColor = texelFetch(currentFrame, ivec2(pos), 0);
    // todo clamping
    outColor = newColor * taa.alpha + prevColor * (1 - taa.alpha);
}
