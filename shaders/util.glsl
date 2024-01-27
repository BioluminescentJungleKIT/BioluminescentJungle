struct SceneLightInfo {
    mat4 inverseMVP;
    vec3 cameraPos;
    vec3 cameraUp;
    int viewportWidth;
    int viewportHeight;
    float fogAbsorption;
    float scatterStrength;
    float bleed;
    int lightAlgo;
    int randomSeed;
    float restirTemporalFactor;
    int restirSpatialRadius;
    int restirSpatialNeighbors;
    int restirInitialSamples;
    float restirLightGridRadius;
    float restirLightGridSearchAlpha;
    int restirSamplingMode;
    float restirPointLightImportance;
};

vec3 calculatePositionFromUV(float depth, vec2 uv, mat4 inverseVP) {
    // Convert screen coordinates to normalized device coordinates (NDC)
    vec4 ndc = vec4((uv - 0.5) * 2.0, depth, 1.0);

    // Convert NDC throuch inverse clip coordinates to view coordinates
    vec4 clip = inverseVP * ndc;
    return (clip / clip.w).xyz;
}

vec3 calculatePosition(float depth, vec2 fragCoord, in SceneLightInfo info) {
    return calculatePositionFromUV(depth, fragCoord / vec2(info.viewportWidth, info.viewportHeight), info.inverseMVP);
}

struct SurfacePoint {
    vec3 worldPos;
    vec3 albedo;
    vec3 N;
};

struct PointLight {
    vec4 position; // just xyz
    vec4 color; // just xyz
    vec4 intensity; // just x
    vec4 wind; // just x
    vec4 velocity; // just xyz
};

struct EmissiveTriangle {
    vec4 x; // just xyz
    vec4 y; // just xyz
    vec4 z; // just xyz
    vec4 emission;
};

struct PointLightParams {
    vec3 pos;
    vec3 color;
    float intensity;
    float r;
};

PointLightParams computeLightParams(PointLight light) {
    PointLightParams params;
    params.pos = light.position.xyz;
    params.color = light.color.rgb;
    params.intensity = light.intensity.x / 55;
    params.r = exp(100);
    return params;
}

float max3 (vec3 v) {
    return max (max (v.x, v.y), v.z);
}

float evalPseudoGeometryTerm(vec3 Pa, vec3 Na, vec3 Pb, out vec3 L, out float dist) {
    L = Pb - Pa;
    dist = length(L);
    L /= dist;
    return max(0.0, dot(L, Na)) / (dist * dist);
}

float evalEmittingPoint(in SurfacePoint point, vec3 emitterPoint, vec3 emitterN) {
    vec3 L;
    float dist;
    float g = evalPseudoGeometryTerm(point.worldPos, point.N, emitterPoint, L, dist);
    return g * max(0.0, dot(emitterN, -L));
}

float evalPointLightStrength(in SurfacePoint point, in PointLightParams light) {
    vec3 L;
    float dist;
    return evalPseudoGeometryTerm(point.worldPos, point.N, light.pos, L, dist) * light.intensity;
}

float evalFogAbsorption(in SurfacePoint point, in SceneLightInfo info) {
    vec3 lightray = info.cameraPos - point.worldPos;
    float d = length(lightray);
    return exp(-info.fogAbsorption*d);
}

// scattering + absorption in w component
vec4 evalFog(in SurfacePoint point, in PointLightParams light, in SceneLightInfo info) {
    // Fog
    vec3 lightray = info.cameraPos - point.worldPos;
    float d = length(lightray);

    vec4 contribution = vec4(0);
    contribution.w = exp(-info.fogAbsorption*d);

    //scattering
    float b = max3(light.color) * light.intensity;
    float lightdist = distance(light.pos, info.cameraPos);
    float h = length(cross(lightray, (point.worldPos - light.pos)))/d;
    if (h*h < light.r*light.r) {
        contribution.rgb = smoothstep(lightdist - info.bleed * h * h * 100, lightdist + info.bleed * h * h * 100, d) * exp(-info.fogAbsorption*lightdist) *
                max(min(1.0 - pow(h / (light.r * sqrt(b)),1), 1), 0) * (atan(d/h)/h) * light.color * light.intensity * info.scatterStrength;
    }

    return contribution;
}

vec3 evalPointLight(in SurfacePoint point, in PointLightParams light, in SceneLightInfo info) {
    vec3 L;
    float dist;
    float f = evalPseudoGeometryTerm(point.worldPos, point.N, light.pos, L, dist);
    vec4 fog = evalFog(point, light, info);

    float b = max3(light.color) * light.intensity;
    float cutoff = max(min(1.0 - pow(dist / (light.r * sqrt(b)), 4), 1), 0);
    return f * fog.w * cutoff * light.color * light.intensity * point.albedo + fog.rgb;
}
