#version 450

layout(location = 0) flat in vec3 fPosition;
layout(location = 1) flat in vec3 fIntensity;
layout(location = 2) flat in vec2 projPosition;

layout(location = 0) out vec4 outColor;

layout(set = 1, binding = 0) uniform sampler2D albedo;
layout(set = 1, binding = 1) uniform sampler2D depth;
layout(set = 1, binding = 2) uniform sampler2D normal;
layout(set = 1, binding = 3) uniform sampler2D motion;

layout(set = 2, binding = 0, std140) uniform DebugOptions {
    int showLightBoxes;
    int compositionMode;
    float radius;
} debug;

layout(set = 2, binding = 1, std140) uniform LightInfo {
    mat4 inverseMVP;
    vec3 cameraPos;
    vec3 cameraUp;
    float viewportWidth;
    float viewportHeight;
} info;

vec3 calculatePosition() {
    float depth = texelFetch(depth, ivec2(gl_FragCoord), 0).r;

    // Convert screen coordinates to normalized device coordinates (NDC)
    vec4 ndc = vec4(
            (gl_FragCoord.x / info.viewportWidth - 0.5) * 2.0,
            (gl_FragCoord.y / info.viewportHeight - 0.5) * 2.0,
            depth, 1.0);

    // Convert NDC throuch inverse clip coordinates to view coordinates
    vec4 clip = info.inverseMVP * ndc;
    return (clip / clip.w).xyz;
}

vec3 rndColor(vec3 pos) {
    pos = mat3(5.4, -6.3, 1.2, 3.2, 7.9, -2.8, -8.0, 1.5, 4.6) * pos;
    return abs(normalize(pos));
}

float max3 (vec3 v) {
    return max (max (v.x, v.y), v.z);
}

void main() {
    if (debug.showLightBoxes > 0) {
        outColor = vec4(rndColor(fPosition) * 0.2, 1.0);
        if (length(gl_FragCoord.xy - projPosition) < 7) {
            // Draw the point lights themselves
            // TODO: we might want to remove this, I doubt this is very performant, but it is useful for debugging.
            outColor = vec4(fIntensity, 1.0);
        }
        return;
    }

    if (debug.compositionMode == 0) {
        vec3 L = fPosition - calculatePosition();
        float dist = length(L);
        L /= dist;
        float f = max(min(1.0 - pow(dist / (debug.radius * sqrt(max3(fIntensity))), 4), 1), 0 ) / (dist * dist);

        if (f > 0.0) {
            vec3 albedo = texelFetch(albedo, ivec2(gl_FragCoord), 0).rgb;
            vec3 N = texelFetch(normal, ivec2(gl_FragCoord), 0).xyz;
            outColor = vec4(fIntensity * f * albedo * max(0.0, dot(L, N)), 1.0);
        } else {
            outColor = vec4(0.0, 0.0, 0.0, 1.0);
        }
    } else if (debug.compositionMode == 1) {
        outColor = vec4(texelFetch(albedo, ivec2(gl_FragCoord), 0).rgb, 1.0);
    } else if (debug.compositionMode == 2) {
        outColor = vec4(vec3(texelFetch(depth, ivec2(gl_FragCoord), 0).r), 1.0);
    } else if (debug.compositionMode == 3) {
        outColor = vec4(calculatePosition(), 1.0);
    } else if (debug.compositionMode == 4) {
        outColor = vec4(texelFetch(normal, ivec2(gl_FragCoord), 0).xyz, 1.0);
    } else if (debug.compositionMode == 5) {
        outColor = vec4(texelFetch(motion, ivec2(gl_FragCoord), 0).rg, 0.0, 1.0) * 100;
    }
}
