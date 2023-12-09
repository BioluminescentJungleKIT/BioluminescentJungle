#version 450

layout(points) in;
layout(triangle_strip, max_vertices = 4) out;

layout(set = 0, binding = 0) uniform UniformBufferObject {
    mat4 modl;// global
    mat4 view;
    mat4 proj;
    vec2 jitt;
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

float max3 (vec3 v) {
    return max (max (v.x, v.y), v.z);
}

void main() {
    fPosition = position[0];
    fIntensity = intensity[0];
    const float radius = debug.radius * sqrt(max3(intensity[0]));

    mat4 VP = ubo.proj * ubo.view;
    vec4 light_pos_screen = VP * vec4(position[0], 1.0);
    light_pos_screen /= light_pos_screen.w;
    vec2 pp = (light_pos_screen.xy * 0.5 + 0.5) * vec2(info.viewportWidth, info.viewportHeight);

    // Project all 8 corners of the bbox of the sphere to screen space
    // TODO: can we do that more efficiently?
    float radius_screen_x = 0;
    float radius_screen_y = 0;
    for (int dx = -1; dx <= 1; dx += 2) {
        for (int dy = -1; dy <= 1; dy += 2) {
            for (int dz = -1; dz <= 1; dz += 2) {
                vec4 corner = VP * vec4(position[0] + vec3(dx, dy, dz) * radius, 1.0);
                corner /= corner.w;

                radius_screen_x = max(radius_screen_x, abs(corner.x - light_pos_screen.x));
                radius_screen_y = max(radius_screen_y, abs(corner.y - light_pos_screen.y));
            }
        }
    }

    fPosition = position[0];
    fIntensity = intensity[0];
    projPosition = pp;
    gl_Position = light_pos_screen + vec4(-radius_screen_x, radius_screen_y, 0, 0); EmitVertex();
    fPosition = position[0];
    fIntensity = intensity[0];
    projPosition = pp;
    gl_Position = light_pos_screen + vec4(radius_screen_x, radius_screen_y, 0, 0); EmitVertex();
    fPosition = position[0];
    fIntensity = intensity[0];
    projPosition = pp;
    gl_Position = light_pos_screen + vec4(-radius_screen_x, -radius_screen_y, 0, 0); EmitVertex();
    fPosition = position[0];
    fIntensity = intensity[0];
    projPosition = pp;
    gl_Position = light_pos_screen + vec4(radius_screen_x, -radius_screen_y, 0, 0); EmitVertex();
}
