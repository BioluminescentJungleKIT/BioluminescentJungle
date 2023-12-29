#version 450

layout(location = 0) out vec4 outColor;

layout(set = 0, binding = 0) uniform sampler2D accColor;

layout(set = 0, binding = 1) uniform GlobalFogUBO {
    mat4 view;
    mat4 projection;
    mat4 inverseVP;
    vec3 color;

    float ambientFactor;
    float brightness;
    float absorption;
    float near;
    float far;

    // Stuff for SSR
    float viewportWidth;
    float viewportHeight;
    float ssrStrength;
    float ssrHitThreshold;
    float ssrEdgeSmoothing;
    int ssrRaySteps;
} fog;

layout(set = 0, binding = 2) uniform sampler2D albedo;
layout(set = 0, binding = 3) uniform sampler2D depth;
layout(set = 0, binding = 4) uniform sampler2D normal;
layout(set = 0, binding = 5) uniform sampler2D motion;

vec3 getNDC(float depth) {
    return vec3((gl_FragCoord.x / fog.viewportWidth - 0.5) * 2.0,
            (gl_FragCoord.y / fog.viewportHeight - 0.5) * 2.0, depth);
}

vec3 calculateWorldPosition(vec3 ndcPos) {
    float depth = texelFetch(depth, ivec2(gl_FragCoord), 0).r;
    vec4 clip = fog.inverseVP * vec4(ndcPos, 1.0);
    return (clip / clip.w).xyz;
}

float getWorldDepth(float d) {
    return (fog.far*fog.near)/((1-d) * (fog.far - fog.near) - (1-d));
}

float fogAbsorption(float distance) {
    return exp(-fog.absorption * distance);
}

vec3 fogAmbientTerm(float worldDepth) {
    vec3 ambientLight = fog.ambientFactor * fog.color;
    return ambientLight * fogAbsorption(worldDepth);
}

vec4 sampleNdc(sampler2D tex, vec2 ndc) {
    return texture(tex, ndc * 0.5 + 0.5);
}

float getFadeFactor01(float f) {
    return 1.0 - exp(10*fog.ssrEdgeSmoothing*(f-1));
}

float getFadeOutFactor(vec2 ndc, vec2 dir) {
    if (ndc.x < -1 || ndc.x > 1 || ndc.y < -1 || ndc.y > 1) return 0;

    return getFadeFactor01(abs(ndc.x)) * getFadeFactor01(abs(ndc.y));
}

void traceReflection(vec3 N, float d, float ssrStrength, inout vec4 color) {
    const vec3 ndcPos = getNDC(d);
    const vec3 worldPos = calculateWorldPosition(ndcPos);

    // TODO: probably better to put this in the ubo
    const vec3 cameraWorldPos = (inverse(fog.view) * vec4(0, 0, 0, 1)).xyz;
    const vec3 viewDir = normalize(worldPos - cameraWorldPos);
    const vec3 reflectedDir = viewDir - 2 * dot(N, viewDir) * N;

    const vec4 furtherAlongRefl = (fog.projection * fog.view * vec4(worldPos + reflectedDir * 0.1, 1));
    vec3 ndcReflectedDir = normalize(furtherAlongRefl.xyz / furtherAlongRefl.w - ndcPos);
    ndcReflectedDir /= max(abs(ndcReflectedDir.x), abs(ndcReflectedDir.y));
    ndcReflectedDir *= 3;

    const int steps = fog.ssrRaySteps;
    vec2 ndc = ndcPos.xy;

    vec3 cur = ndcPos;
    for (int i = 1; i < steps; i++) {
        float fadeFactor = getFadeOutFactor(cur.xy, ndcReflectedDir.xy);
        if (fadeFactor <= 0) break;

        cur += ndcReflectedDir / steps;
        float curDepth = sampleNdc(depth, cur.xy).r;

        float hit = cur.z - curDepth;

        if (hit > 0) {
            if (hit > fog.ssrHitThreshold) {
                // We are hitting the underside
                return;
            }

            // Since we read values before the fog has set it, we need to compute the ambient light for each hit
            vec3 acc = sampleNdc(accColor, cur.xy).rgb;
            vec3 albedo = sampleNdc(albedo, cur.xy).rgb;
            acc += fogAmbientTerm(getWorldDepth(curDepth)) * albedo.rgb;

            float pathLength = length(calculateWorldPosition(vec3(cur.xy, curDepth)) - worldPos);
            color += vec4(acc * fogAbsorption(pathLength) * fadeFactor, 0.0) * ssrStrength;
            return;
        }
    }

    //color = vec4(1, 0, 0, 1);
}

void main() {
    vec4 color = texelFetch(accColor, ivec2(gl_FragCoord), 0);
    float d = texelFetch(depth, ivec2(gl_FragCoord), 0).x;
    vec4 albedo = texelFetch(albedo, ivec2(gl_FragCoord), 0);

    float worldDepth = getWorldDepth(d);
    color.rgb += albedo.rgb * fogAmbientTerm(d);

    vec3 fogLight = fog.brightness * fog.color;
    // depth illumination: integral from 0 to depth: color * exp(-absorption*x) dx
    if (fog.absorption != 0)
        color.rgb += (fogLight - fogLight * fogAbsorption(worldDepth))/fog.absorption;

    if (fog.ssrStrength > 0) {
        vec4 normal = texelFetch(normal, ivec2(gl_FragCoord), 0);
        // TODO: d < 1.0f works because 1.0 is depth buffer clear value. We do not clear the normals, so just comparing
        // normal.a > 0 gives false positives.
        if (normal.a > 0 && d < 1.0f) {
            traceReflection(normal.xyz, d, normal.a * fog.ssrStrength, color);
        }
    }

    outColor = vec4(color.rgb, 1);
}
