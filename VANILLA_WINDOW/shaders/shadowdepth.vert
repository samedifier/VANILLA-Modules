#version 450

// VANILLA WINDOW internal shadow depth shader (vertex only).
// Embedded into the module (include/vanilla_window_shadowdepth_spv.h).
// Replace with VANILLA_setShadowDepthShader when using a custom vertex layout:
// the replacement must keep this interface (set 0 bindings 0 and 3, push constant).

struct MaterialSlot {
    uint materialSlot;
    uint flags1;
    uint flags2;
    uint flags3;
};

layout(set = 0, binding = 0, std430) readonly buffer TransformBuffer {
    mat4 transforms[];
} transformBuffer;

layout(set = 0, binding = 3, std430) readonly buffer MaterialSlotBuffer {
    MaterialSlot materialSlots[];
} materialSlotBuffer;

// Same block as the scene shaders; viewProjection = cascade light matrix here.
layout(push_constant, std430) uniform Camera {
    mat4 viewProjection;
    float aspectRatio;
    float uioffsetx;
    float uioffsety;
    float _padding1;
    vec3 cameraPos;
    float padding2;
} camera;

layout(location = 0) in vec4 posNx;

// Entity flags1 (mirror of VANILLA_EntityFlags1 in main.h)
const uint ENTITY_NO_RENDER         = 1u << 0;
const uint ENTITY_NO_CAST_SHADOW    = 1u << 1;
const uint ENTITY_NO_RECEIVE_SHADOW = 1u << 2;

void main() {
    if ((materialSlotBuffer.materialSlots[gl_InstanceIndex].flags1 & ENTITY_NO_CAST_SHADOW) != 0u) {
        // Every vertex at the same point outside the clip volume: the triangle is dropped.
        gl_Position = vec4(2.0, 2.0, 2.0, 1.0);
        return;
    }

    mat4 model = transformBuffer.transforms[gl_InstanceIndex];
    gl_Position = camera.viewProjection * (model * vec4(posNx.xyz, 1.0));
}
