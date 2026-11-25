#version 450

// VANILLA WINDOW internal shadow shader for alpha-tested (glTF MASK) casters: like shadowdepth.vert,
// plus the UV and material slot for shadowmask.frag. Embedded (include/vanilla_window_shadowdepth_spv.h).
// Assumes the module's default vertex layout (UV in location 1 .zw, UV1 in location 3); replace with
// VANILLA_setShadowMaskShader for a custom layout, keeping this interface.

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
layout(location = 1) in vec4 nyNzUv;
layout(location = 3) in vec2 uv1;

layout(location = 0) out vec2 outUV;
layout(location = 1) flat out uint outMaterialSlot;
layout(location = 2) out vec2 outUV1;

const uint ENTITY_NO_CAST_SHADOW = 1u << 1;

void main() {
    MaterialSlot slot = materialSlotBuffer.materialSlots[gl_InstanceIndex];

    if ((slot.flags1 & ENTITY_NO_CAST_SHADOW) != 0u) {
        gl_Position = vec4(2.0, 2.0, 2.0, 1.0);   // dropped
        return;
    }

    mat4 model = transformBuffer.transforms[gl_InstanceIndex];
    gl_Position = camera.viewProjection * (model * vec4(posNx.xyz, 1.0));

    outUV = nyNzUv.zw;
    outUV1 = uv1;
    outMaterialSlot = slot.materialSlot;
}
