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

layout(set = 3, binding = 0, std140) uniform MaterialSettings {
    float heightScale;
    int raymarchSteps;
    int enableInverseDisplacement;
    int enableLinearApprox;
} mapping;

// Thanks InCG exercise
void computeCotangentSpace(vec3 N, out vec3 T, out vec3 B) {
    vec3 Q1 = dFdx(fsPos);
    vec3 Q2 = dFdy(fsPos);
    vec2 st1 = dFdx(uv);
    vec2 st2 = dFdy(uv);

    vec3 dp2perp = cross( Q2, N );
    vec3 dp1perp = cross( N, Q1 );
    T = dp2perp * st1.x + dp1perp * st2.x;
    B = dp2perp * st1.y + dp1perp * st2.y;

    // construct a scale-invariant frame
    float invmax = inversesqrt( max( dot(T,T), dot(B,B) ) );
    T *= invmax;
    B *= invmax;
}

void main() {
    outMotion = (fsOldPosClipSpace/fsOldPosClipSpace.w - fsPosClipSpace/fsPosClipSpace.w).xy;

    vec3 N = normalize(normal);
    vec3 T, B;
    computeCotangentSpace(N, T, B);
    if (mapping.enableInverseDisplacement == 0) {
        outColor = texture(albedo, uv);

        // Convert normal to world space, because our lighting uses it
        outNormal = mat3(T, B, N) * texture(normalMap, uv).xyz;
        outNormal = (transpose(ubo.view) * vec4(outNormal, 0.0)).xyz;
        return;
    }

    // TODO: investigate whether we want normalization or not.
    // If yes: we can use transpose() instead of inverse()
    // If no: we might still be able to use a few tricks to avoid inversion
    mat3 worldToTangent = transpose(mat3(T, B, N));
    vec3 ray = (worldToTangent * -fsPos).xyz;

    const vec3 directionNormalized = normalize(ray);
    const vec3 step = vec3(directionNormalized.xy / directionNormalized.z, -1.0) / mapping.raymarchSteps;
    vec3 currentPos = vec3(uv, mapping.heightScale);

    vec2 duvdx = dFdx(uv);
    vec2 duvdy = dFdy(uv);


    float lastHeightAbove = 0.0;

    for (int i = 0; i < mapping.raymarchSteps; i++) {
        if (currentPos.s < 0 || currentPos.t < 0 || currentPos.s > 1 || currentPos.t > 1) {
            // We left the object
            // TODO: optimize the check: we can compute maximal number of samples
            discard;
        }

        const float depth = texture(heightMap, currentPos.st).r * mapping.heightScale;
        const float heightAbove = currentPos.z - depth;

        if (heightAbove <= 0) {
            // Found a hit, linear interpolation
            if (heightAbove < 0 && mapping.enableLinearApprox > 0) {
                currentPos -= (-heightAbove) / (-heightAbove + lastHeightAbove) * step;
            }

            outColor = texture(albedo, currentPos.st);

            // Convert normal to world space, because our lighting uses it
            outNormal = mat3(T, B, N) * texture(normalMap, currentPos.st).xyz;
            outNormal = (transpose(ubo.view) * vec4(outNormal, 0.0)).xyz;
            return;
        } else {
            currentPos += step;
            lastHeightAbove = heightAbove;
        }
    }

    discard;
}
