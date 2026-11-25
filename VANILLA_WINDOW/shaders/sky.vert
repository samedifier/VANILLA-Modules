#version 460

// VANILLA WINDOW internal sky pass: fullscreen triangle at the far plane (depth 1).
// Drawn after the HDR scene with depth test LESS_OR_EQUAL, so only empty pixels are shaded.
// Embedded into the module (include/vanilla_window_sky_spv.h).

layout(location = 0) out vec2 ndc;

void main() {
    const vec2 positions[3] = vec2[](vec2(-1.0, -1.0), vec2(3.0, -1.0), vec2(-1.0, 3.0));
    ndc = positions[gl_VertexIndex];
    gl_Position = vec4(ndc, 1.0, 1.0);
}
