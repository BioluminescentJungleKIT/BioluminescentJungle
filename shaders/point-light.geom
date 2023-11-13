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

layout(location = 0) flat out vec3 fPosition;
layout(location = 1) flat out vec3 fIntensity;
layout(location = 2) flat out vec2 projPosition;

void main() {
    fPosition = position[0];
    fIntensity = intensity[0];
    const float radius = debug.radius;

    fPosition = position[0];
    fIntensity = intensity[0];

    mat4 VP = ubo.proj * ubo.view;
    vec4 center = VP * vec4(position[0], 1.0);
    center /= center.w;
    projPosition = (center.xy * 0.5 + 0.5) * vec2(info.viewportWidth, info.viewportHeight);

    float off_x = 0.0;
    float off_y = 0.0;

    // Project all 8 corners of the bbox of the sphere to screen space
    // TODO: can we do that more efficiently?
    for (int dx = -1; dx <= 1; dx += 2) {
        for (int dy = -1; dy <= 1; dy += 2) {
            for (int dz = -1; dz <= 1; dz += 2) {
                vec4 corner = VP * vec4(position[0] + vec3(dx, dy, dz) * radius, 1.0);
                corner /= corner.w;

                off_x = max(off_x, abs(corner.x - center.x));
                off_y = max(off_y, abs(corner.y - center.y));
            }
        }
    }

    gl_Position = vec4(center.x - off_x,  center.y - off_y, 0, 1); EmitVertex();
    gl_Position = vec4(center.x - off_x,  center.y + off_y, 0, 1); EmitVertex();
    gl_Position = vec4(center.x + off_x,  center.y - off_y, 0, 1); EmitVertex();
    gl_Position = vec4(center.x + off_x,  center.y + off_y, 0, 1); EmitVertex();
}
