// The content of this file is licensed under MIT.
// Copyright (c) 2024 Ilia Bozhinov, Lars Erber.

#version 450

vec2 positions[6] = vec2[](
    vec2(-1.0, 1.0), // bottom left
    vec2(1.0, 1.0), // bottom right
    vec2(-1.0, -1.0), // top left

    vec2(-1.0, -1.0), // top left
    vec2(1.0, 1.0), // bottom right
    vec2(1.0, -1.0) // top right
);

// We need some fake variables for point-light.frag so that compilation succeeds
layout(location = 0) flat out vec3 fPosition;
layout(location = 1) flat out vec3 fIntensity;
layout(location = 2) flat out vec2 projPosition;

void main() {
    fPosition = vec3(0.0);
    fIntensity = vec3(0.0);
    projPosition = vec2(-20, -20);
    gl_Position = vec4(positions[gl_VertexIndex], 0.0, 1.0);
}
