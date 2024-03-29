// The content of this file is licensed under MIT.
// Copyright (c) 2024 Ilia Bozhinov, Lars Erber.

#version 450
#include "util.glsl"

layout(points) in;
layout(triangle_strip, max_vertices = 4) out;

layout(set = 0, binding = 0) uniform UniformBufferObject {
    mat4 modl;// global
    mat4 view;
    mat4 proj;
    vec2 jitt;
    float time;
} ubo;

layout(set = 2, binding = 0, std140) uniform DebugOptions {
    int showLightBoxes;
    int compositionMode;
    float radius;
} debug;

layout(set = 2, binding = 1, std140) uniform LightInfoBlock {
    SceneLightInfo info;
};

layout(location = 0) in vec3 position[];
layout(location = 1) in vec3 intensity[];

layout(location = 0) flat out vec3 fPosition;
layout(location = 1) flat out vec3 fIntensity;
layout(location = 2) flat out vec2 projPosition;

void main() {
    fPosition = position[0];
    fIntensity = intensity[0];
    const float radius = debug.radius * sqrt(max3(intensity[0]));

    mat4 VP = ubo.proj * ubo.view;
    vec4 light_pos_screen = VP * vec4(position[0], 1.0);
    light_pos_screen /= light_pos_screen.w;
    vec2 pp = (light_pos_screen.xy * 0.5 + 0.5) * vec2(info.viewportWidth, info.viewportHeight);

    vec3 up = normalize(transpose(ubo.view)[1].xyz);
    vec3 forward = normalize(transpose(ubo.view)[2].xyz);
    vec4 radius_pos_screen = VP * vec4(position[0] + up * radius, 1.0);
    vec4 radius_pos_z = VP * vec4(position[0] + forward * radius, 1.0);
    radius_pos_screen /= radius_pos_screen.w;
    radius_pos_z /= radius_pos_z.w;

    float aspect = float(info.viewportHeight)/float(info.viewportWidth);
    float radius_screen_y = length(light_pos_screen - radius_pos_screen);
    float radius_screen_x = radius_screen_y * aspect;
    float radius_screen_z = length(light_pos_screen - radius_pos_z);

    int wouldBecomeNearClipping = int((light_pos_screen.z - radius_screen_z < 0) && (light_pos_screen.z + radius_screen_z > 0));
    int isNearClipping = int(light_pos_screen.z >= 1);

    radius_screen_z = radius_screen_z * (1.0 - wouldBecomeNearClipping);
    light_pos_screen.z = light_pos_screen.z * (1.0 - wouldBecomeNearClipping) + 0.1 * wouldBecomeNearClipping;
    radius_screen_z = radius_screen_z * (1.0 - isNearClipping);

    fPosition = position[0];
    fIntensity = intensity[0];
    projPosition = pp;
    gl_Position = light_pos_screen + vec4(-radius_screen_x, radius_screen_y, -radius_screen_z, 0); EmitVertex();
    fPosition = position[0];
    fIntensity = intensity[0];
    projPosition = pp;
    gl_Position = light_pos_screen + vec4(radius_screen_x, radius_screen_y, -radius_screen_z, 0); EmitVertex();
    fPosition = position[0];
    fIntensity = intensity[0];
    projPosition = pp;
    gl_Position = light_pos_screen + vec4(-radius_screen_x, -radius_screen_y, -radius_screen_z, 0); EmitVertex();
    fPosition = position[0];
    fIntensity = intensity[0];
    projPosition = pp;
    gl_Position = light_pos_screen + vec4(radius_screen_x, -radius_screen_y, -radius_screen_z, 0); EmitVertex();
}
