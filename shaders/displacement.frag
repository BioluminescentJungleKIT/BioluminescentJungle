#version 450

layout(location = 0) in vec2 uv;
layout(location = 1) in vec3 normal;
layout(location = 2) in vec3 fsPos;

layout(location = 0) out vec4 outColor;
layout(location = 1) out vec3 outNormal;

layout(set = 0, binding = 0) uniform UniformBufferObject {
    mat4 model;  // global
    mat4 view;
    mat4 proj;
} ubo;

layout(set = 2, binding = 0) uniform sampler2D texSampler;
layout(set = 2, binding = 1) uniform sampler2D displacement;

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

// Find intersection point in direction dir with position pos
vec3 raymarch(vec3 position, vec3 direction, vec2 uv) {
    return vec3(1.0);
}

void main() {
    outColor = texture(texSampler, uv);
    vec3 N = normalize(normal);

    vec3 T, B;
    computeCotangentSpace(N, T, B);

    // TODO: investigate whether we want normalization or not.
    // If yes: we can use transpose() instead of inverse()
    // If no: we might still be able to use a few tricks to avoid inversion
    mat3 worldToTangent = transpose(mat3(T, B, N));
    vec3 ray = (worldToTangent * -fsPos).xyz;

    // TODO: make a parameter
    float heightScale = 0.02;

    // TODO: make a parameter
    const int numSamples = 100;

    const vec3 directionNormalized = normalize(ray);
    const vec3 step = vec3(directionNormalized.xy / directionNormalized.z, -1.0) / numSamples;
    vec3 currentPos = vec3(uv, heightScale);

    float lastHeightAbove = 0.0;

    for (int i = 0; i < numSamples; i++) {
        if (currentPos.s < 0 || currentPos.t < 0 || currentPos.s > 1 || currentPos.t > 1) {
            // We left the object
            // TODO: optimize the check: we can compute maximal number of samples
            discard;
        }

        const float depth = texture(displacement, currentPos.st).r * heightScale;
        const float heightAbove = currentPos.z - depth;

        if (heightAbove <= 0) {
            // Found a hit, linear interpolation
            currentPos -= (-heightAbove) / (-heightAbove + lastHeightAbove) * step;
            outColor = texture(texSampler, currentPos.st);


            // Convert normal to world space, because our lighting uses it

            outNormal = 1.0 * i / numSamples * vec3(1.0, 1.0, 1.0);
            outNormal = depth * vec3(1.0, 1.0, 1.0);

            //outNormal = (transpose(ubo.view) * vec4(N, 0.0)).xyz;
            return;
        } else {
            currentPos += step;
            lastHeightAbove = heightAbove;
        }
    }

    discard;

    //    vec3 normalViewSpace = mat3(T, B, N) * texture(displacement, uv).xyz;
}
