#version 450

layout(points) in;
layout(triangle_strip, max_vertices = 4) out;

layout(set = 0, binding = 0) uniform UniformBufferObject {
    mat4 model;  // global
    mat4 view;
    mat4 proj;
} ubo;

layout(set = 2, binding = 0, std140) uniform DebugOptions {
    int showLightBoxes;
    int compositionMode;
    float radius;
} debug;

layout(set = 2, binding = 1, std140) uniform LightInfo {
    mat4 inverseMVP;
    vec3 cameraPos;
    vec3 cameraUp;
    float viewportWidth;
    float viewportHeight;
} info;

layout(location = 0) in vec3 position[];
layout(location = 1) in vec3 intensity[];

layout(location = 0) out vec3 fPosition;
layout(location = 1) out vec3 fIntensity;

void main() {
    fPosition = position[0];
    fIntensity = intensity[0];
    const float radius = debug.radius;

    fPosition = position[0];
    fIntensity = intensity[0];

    // Idea taken from InCG exercise 4
    mat4 VP = ubo.proj * ubo.view;
    vec4 center = VP * vec4(position[0], 1.0);
    vec4 edge = VP * vec4(position[0] + radius * info.cameraUp, 1.0);
    center /= center.w;
    edge /= edge.w;

    float off_y = abs(edge.y - center.y);
    float off_x = off_y * info.viewportHeight / info.viewportWidth;
    off_x *= 1.5;
    off_y *= 1.5;

    gl_Position = vec4(center.x - off_x,  center.y - off_y, 0, 1); EmitVertex();
    gl_Position = vec4(center.x - off_x,  center.y + off_y, 0, 1); EmitVertex();
    gl_Position = vec4(center.x + off_x,  center.y - off_y, 0, 1); EmitVertex();
    gl_Position = vec4(center.x + off_x,  center.y + off_y, 0, 1); EmitVertex();
}
