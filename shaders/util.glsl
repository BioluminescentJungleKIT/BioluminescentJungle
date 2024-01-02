struct SceneLightInfo {
    mat4 inverseMVP;
    vec3 cameraPos;
    vec3 cameraUp;
    float viewportWidth;
    float viewportHeight;
    float fogAbsorption;
    float scatterStrength;
    float bleed;
    int lightAlgo;
};

vec3 calculatePosition(float depth, vec2 fragCoord, in SceneLightInfo info) {
    // Convert screen coordinates to normalized device coordinates (NDC)
    vec4 ndc = vec4((fragCoord / vec2(info.viewportWidth, info.viewportHeight) - 0.5) * 2.0, depth, 1.0);

    // Convert NDC throuch inverse clip coordinates to view coordinates
    vec4 clip = info.inverseMVP * ndc;
    return (clip / clip.w).xyz;
}

struct SurfacePoint {
    vec3 worldPos;
    vec3 albedo;
    vec3 N;
};

struct PointLightParams {
    vec3 pos;
    vec3 intensity;
    float r;
};

float max3 (vec3 v) {
    return max (max (v.x, v.y), v.z);
}

vec3 evalPointLight(in SurfacePoint point, in PointLightParams light, in SceneLightInfo info) {
    vec3 L = light.pos - point.worldPos;
    float dist = length(L);
    L /= dist;

    float b = max3(light.intensity);
    float f = max(min(1.0 - pow(dist / (light.r * sqrt(b)), 4), 1), 0) / (dist * dist);
    f = max(f, 0);

    vec3 contribution = light.intensity * f * point.albedo * max(0.0, dot(L, point.N));

    // Fog
    vec3 lightray = info.cameraPos - point.worldPos;
    float lightdist = distance(light.pos, info.cameraPos);
    float d = length(lightray);
    contribution *= exp(-info.fogAbsorption*d);

    //scattering
    float h = length(cross(lightray, (point.worldPos - light.pos)))/d;
    if (h*h < light.r*light.r) {
        contribution += smoothstep(lightdist - info.bleed, lightdist + info.bleed, d) * exp(-info.fogAbsorption*lightdist) *
                max(min(1.0 - pow(h / (light.r * sqrt(b)),1), 1), 0) * (atan(d/h)/h) * light.intensity * info.scatterStrength;
    }

    return contribution;
}
