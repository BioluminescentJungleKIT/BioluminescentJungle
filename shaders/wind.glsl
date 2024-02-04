// The content of this file is licensed under MIT.
// Copyright (c) 2024 Ilia Bozhinov, Lars Erber.

#include "noise3D.glsl"

vec3 noise3D(vec3 position) {
    return vec3(snoise(position), snoise(position + vec3(314, 145, 457)), snoise(position + vec3(764, 959, 114)));
}

vec4 windDisplacement(vec4 position, float time, vec3 origin, float strength) {
    vec3 diff = position.xyz - origin;
    float distSqr = min(dot(diff, diff) * 0.5, 0.1);
    return position + vec4(noise3D(position.xyz / 5 + vec3(time * 0.4, 0, 0)) * distSqr, 0) * strength;
}