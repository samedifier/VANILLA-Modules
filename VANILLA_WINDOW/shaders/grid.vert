#version 460

// VANILLA WINDOW internal editor grid: one quad on the plane y = height, centered under the camera.
// Embedded into the module (include/vanilla_window_grid_spv.h).

layout(push_constant, std430) uniform GridPush {
    mat4 viewProjection;
    vec4 cameraHeight;   // xyz camera position, w plane height
    vec4 params;         // x cell size, y major every, z fade distance, w quad half extent
    vec4 minorColor;     // rgb linear HDR, a opacity
    vec4 majorColor;
} grid;

layout(location = 0) out vec3 worldPos;

void main() {
    const vec2 corners[6] = vec2[](
        vec2(-1.0, -1.0), vec2( 1.0, -1.0), vec2( 1.0,  1.0),
        vec2( 1.0,  1.0), vec2(-1.0,  1.0), vec2(-1.0, -1.0));

    vec2 xz = grid.cameraHeight.xz + corners[gl_VertexIndex] * grid.params.w;
    worldPos = vec3(xz.x, grid.cameraHeight.w, xz.y);
    gl_Position = grid.viewProjection * vec4(worldPos, 1.0);
}
