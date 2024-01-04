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
};

struct PointLightParams {
    vec3 pos;
    vec3 intensity;
    float r;
};

PointLightParams computeLightParams(PointLight light) {
    PointLightParams params;
    params.pos = light.position.xyz;
    params.intensity = light.color.rgb * light.intensity.x/55;
    params.r = exp(100);
    return params;
}


float max3 (vec3 v) {
    return max (max (v.x, v.y), v.z);
}

float evalPointLightStrength(in SurfacePoint point, in PointLightParams light) {
    vec3 L = light.pos - point.worldPos;
    float dist = length(L);
    L /= dist;

    float b = max3(light.intensity);
    float f = max(min(1.0 - pow(dist / (light.r * sqrt(b)), 4), 1), 0) / (dist * dist);
    f = max(f, 0);

    return f * max(0.0, dot(L, point.N));
}

// rgb scattering, a fog absorption
vec4 evalFog(in SurfacePoint point, in PointLightParams light, in SceneLightInfo info) {
    // Fog
    vec3 lightray = info.cameraPos - point.worldPos;
    float lightdist = distance(light.pos, info.cameraPos);
    float d = length(lightray);
    float b = max3(light.intensity);

    vec4 contribution = vec4(0);
    contribution.w = exp(-info.fogAbsorption*d);

    //scattering
    float h = length(cross(lightray, (point.worldPos - light.pos)))/d;
    if (h*h < light.r*light.r) {
        contribution.rgb = smoothstep(lightdist - info.bleed, lightdist + info.bleed, d) * exp(-info.fogAbsorption*lightdist) *
                max(min(1.0 - pow(h / (light.r * sqrt(b)),1), 1), 0) * (atan(d/h)/h) * light.intensity * info.scatterStrength;
    }

    return contribution;
}

vec3 evalPointLight(in SurfacePoint point, in PointLightParams light, in SceneLightInfo info) {
    float f = evalPointLightStrength(point, light);
    vec4 fog = evalFog(point, light, info);
    return f * fog.w * light.intensity * point.albedo + fog.rgb;
}
