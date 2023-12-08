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

float rand(vec2 co){
    return fract(sin(dot(co, vec2(12.9898, 78.233))) * 43758.5453);
}

void computeTangentSpace(vec3 N, out vec3 T, out vec3 B, out vec2 dUVx, out vec2 dUVy, out float scaling) {
    vec3 Q1 = dFdx(fsPos);
    vec3 Q2 = dFdy(fsPos);
    dUVx = dFdx(uv);
    dUVy = dFdy(uv);
    mat2 iv = inverse(transpose(mat2(dUVx, dUVy)));
    mat3x2 tb = iv * transpose(mat2x3(Q1, Q2));
    T = vec3(tb[0][0], tb[1][0], tb[2][0]);
    B = vec3(tb[0][1], tb[1][1], tb[2][1]);

    //T = cross(Bx, N);
    //B = cross(N, Tx);

    scaling = sqrt( max( dot(T,T), dot(B,B) ) );
    //T /= scaling;
    //B /= scaling;
}

// Thanks InCG exercise => the above works as well, but is a bit slower
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
    vec2 dUVx, dUVy;
    float scaling;
   // computeTangentSpace(N, T, B, dUVx, dUVy, scaling);
    computeCotangentSpace(N, T, B);

    // TODO: investigate whether we want normalization or not.
    // If yes: we can use transpose() instead of inverse()
    // If no: we might still be able to use a few tricks to avoid inversion
    mat3 worldToTangent = transpose(mat3(T, B, N));
    vec3 ray = (worldToTangent * -fsPos).xyz;

    float heightScale = 0.05;


    const int numSamples = 500;
    const float stepSize = 1.0 / numSamples;

    const vec3 directionNormalized = normalize(ray);
    const vec3 step = vec3(directionNormalized.xy / directionNormalized.z, -1.0) * stepSize;
    vec3 currentPos = vec3(uv, heightScale);



    for (int i = 0; i < numSamples; i++) {
        float depth = texture(displacement, currentPos.xy).r * heightScale;

        if (currentPos.x < 0 || currentPos.y < 0 || currentPos.x > 1 || currentPos.y > 1) {
            discard;
        }

        if (depth >= currentPos.z) {
            // Found a hit
            outColor = texture(texSampler, currentPos.xy);
            // Convert normal to world space, because our lighting uses it

            outNormal = 1.0 * i / numSamples * vec3(1.0, 1.0, 1.0);
            outNormal = depth * vec3(1.0, 1.0, 1.0);

            //outNormal = (transpose(ubo.view) * vec4(N, 0.0)).xyz;
            return;
        } else {
            currentPos += step;
        }
    }

    discard;

    //    vec3 normalViewSpace = mat3(T, B, N) * texture(displacement, uv).xyz;
}
