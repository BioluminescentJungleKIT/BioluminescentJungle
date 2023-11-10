#version 450

layout(points) in;
layout(triangle_strip, max_vertices = 4) out;

layout(set = 0, binding = 0) uniform UniformBufferObject {
    mat4 model;  // global
    mat4 view;
    mat4 proj;
} ubo;

layout(location = 0) in vec3 position[];
layout(location = 1) in vec3 intensity[];

layout(location = 0) out vec3 fPosition;
layout(location = 1) out vec3 fIntensity;

void main() {
    const float radius = 0.5;

    fPosition = position[0];
    fIntensity = intensity[0];

    mat4 VP = ubo.proj * ubo.view;
    vec4 center = VP * vec4(position[0], 1.0);
    vec4 edge1 = VP * vec4(position[0] + vec3(radius), 1.0);
    vec4 edge2 = VP * vec4(position[0] - vec3(radius), 1.0);

    center /= center.w;
    edge1 /= edge1.w;
    edge2 /= edge2.w;

    float off_x = max(abs(edge1.x - center.x), abs(edge2.x - center.x));
    float off_y = max(abs(edge1.y - center.y), abs(edge2.y - center.y));

    gl_Position = vec4(center.x - off_x,  center.y - off_y, 0, 1); EmitVertex();
    gl_Position = vec4(center.x - off_x,  center.y + off_y, 0, 1); EmitVertex();
    gl_Position = vec4(center.x + off_x,  center.y - off_y, 0, 1); EmitVertex();
    gl_Position = vec4(center.x + off_x,  center.y + off_y, 0, 1); EmitVertex();
}
