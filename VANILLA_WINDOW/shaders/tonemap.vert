#version 450

// VANILLA WINDOW internal tone mapping pass: fullscreen triangle.
// Embedded into the module (include/vanilla_window_tonemap_spv.h). Same UV convention as pp1.vert.

layout(location = 0) out vec2 uv;

void main() {
    const vec2 positions[3] = vec2[](vec2(-1.0, -1.0), vec2(3.0, -1.0), vec2(-1.0, 3.0));
    const vec2 texCoords[3] = vec2[](vec2(0.0, 0.0),   vec2(2.0, 0.0),  vec2(0.0, 2.0));

    gl_Position = vec4(positions[gl_VertexIndex], 0.0, 1.0);
    uv = texCoords[gl_VertexIndex];
}
