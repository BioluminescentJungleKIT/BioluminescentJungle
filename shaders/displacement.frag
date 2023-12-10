#version 450

layout(location = 0) in vec2 uv;
layout(location = 1) in vec3 normal;
layout(location = 2) in vec3 fsPos;
layout(location = 3) in vec4 fsPosClipSpace;
layout(location = 4) in vec4 fsOldPosClipSpace;

layout(location = 0) out vec4 outColor;
layout(location = 1) out vec3 outNormal;
layout(location = 2) out vec2 outMotion;

layout(set = 0, binding = 0) uniform UniformBufferObject {
    mat4 modl;  // global
    mat4 view;
    mat4 proj;
    vec2 jitt;
} ubo;

layout(set = 2, binding = 0) uniform sampler2D albedo;
layout(set = 2, binding = 1) uniform sampler2D normalMap;
layout(set = 2, binding = 2) uniform sampler2D heightMap;
layout(set = 2, binding = 3) uniform IsNormalMap {
    int isNormalMapOnly;
} isNormal;

layout(set = 3, binding = 0, std140) uniform MaterialSettings {
    float heightScale;
    int raymarchSteps;
    int enableInverseDisplacement;
    int enableLinearApprox;
    int useInvertedFormat;
} mapping;

void computeTangentSpace(vec3 N, out vec3 T, out vec3 B) {
    vec3 Q1 = dFdx(fsPos);
    vec3 Q2 = dFdy(fsPos);
    vec2 st1 = dFdx(uv);
    vec2 st2 = dFdy(uv);

    mat2 m = inverse(transpose(mat2(st1, st2)));
    T = m[0][0] * Q1 + m[1][0] * Q2;
    B = m[0][1] * Q1 + m[1][1] * Q2;

    // construct a scale-invariant frame
    float invmax = inversesqrt( max( dot(T,T), dot(B,B) ) );
    T *= invmax;
    B *= invmax;
}

void readConvertNormal(vec3 T, vec3 B, vec3 N, vec2 uv) {
    float gamma = mapping.useInvertedFormat > 0 ? 1.0/2.2 : 1.0;
    outNormal = pow(texture(normalMap, uv).rgb, vec3(gamma)) * 2 - 1;
    outNormal = normalize(transpose(inverse(mat3(T, B, N))) * outNormal);
    // Convert normal to world space, because our lighting uses it
    outNormal = (transpose(ubo.view) * vec4(outNormal, 0.0)).xyz;
}

void main() {
    outMotion = (fsOldPosClipSpace/fsOldPosClipSpace.w - fsPosClipSpace/fsPosClipSpace.w).xy;
    vec3 N = normalize(normal);

    vec3 T, B;
    computeTangentSpace(N, T, B);
    if (isNormal.isNormalMapOnly > 0 || mapping.enableInverseDisplacement == 0) {
        outColor = texture(albedo, uv);
        // Convert normal to world space, because our lighting uses it
        readConvertNormal(T, B, N, uv);
        return;
    }

    vec3 coT = cross(B, N);
    vec3 coB = cross(N, T);

    // Workaround to compensate for inside-out twisted surfaces
    if (dot(cross(coT, coB), N) > 0) {
        coT *= -1;
        coB *= -1;
    }

    mat3 worldToTangent = transpose(mat3(coT, coB, N));
    vec3 ray = (worldToTangent * -fsPos).xyz;

    const vec3 directionNormalized = normalize(ray);
    const vec3 step = vec3(directionNormalized.xy / directionNormalized.z, -1.0) / mapping.raymarchSteps;
    vec3 currentPos = vec3(uv, mapping.heightScale);
    float lastHeightAbove = 0.0;

    for (int i = 0; i < mapping.raymarchSteps; i++) {
        if (currentPos.s < 0 || currentPos.t < 0 || currentPos.s > 1 || currentPos.t > 1) {
            // We left the object
            // TODO: optimize the check: we can compute maximal number of samples
            discard;
        }

        float depth;
        if (mapping.useInvertedFormat > 0) {
            depth = (1.0 - pow(texture(heightMap, currentPos.st).r, 1.0/2.2)) * mapping.heightScale;
        } else {
            depth = texture(heightMap, currentPos.st).r * mapping.heightScale;
        }

        const float heightAbove = currentPos.z - depth;
        if (heightAbove <= 0) {
            // Found a hit, linear interpolation
            if (heightAbove < 0 && mapping.enableLinearApprox > 0) {
                currentPos -= (-heightAbove) / (-heightAbove + lastHeightAbove) * step;
            }

            outColor = texture(albedo, currentPos.st);
            readConvertNormal(T, B, N, currentPos.st);
            return;
        } else {
            currentPos += step;
            lastHeightAbove = heightAbove;
        }
    }

    discard;
}
