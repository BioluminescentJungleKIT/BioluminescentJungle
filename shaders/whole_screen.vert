#version 450

vec2 positions[6] = vec2[](
    vec2(-1.0, 1.0), // bottom left
    vec2(1.0, 1.0), // bottom right
    vec2(-1.0, -1.0), // top left

    vec2(-1.0, -1.0), // top left
    vec2(1.0, 1.0), // bottom right
    vec2(1.0, -1.0) // top right
);

void main() {
    gl_Position = vec4(positions[gl_VertexIndex], 0.0, 1.0);
}
