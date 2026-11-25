#version 450
#extension GL_EXT_nonuniform_qualifier : require
#extension GL_GOOGLE_include_directive : require

// VANILLA WINDOW internal shadow shader for alpha-tested (glTF MASK) casters: drops texels below
// alphaCutoff so leaves, fences etc. cast their real shape. Depth only, no color output.

#include "material.glsl"

layout(set = 0, binding = 4, std430) readonly buffer MaterialBuffer {
    Material materials[];
} materialBuffer;

layout(set = 0, binding = 5) uniform texture2D textures[];
layout(set = 0, binding = 6) uniform sampler samplers[];

layout(location = 0) in vec2 inUV;
layout(location = 1) flat in uint inMaterialSlot;
layout(location = 2) in vec2 inUV1;

void main() {
    Material m = materialBuffer.materials[inMaterialSlot];

    float alpha = m.baseColor.a;
    if ((m.flags1 & MATERIAL_USE_ALBEDO) != 0u) {
        vec2 uv = materialTextureUV(m, TEXTURE_ALBEDO, inUV, inUV1);
        alpha *= texture(
            sampler2D(textures[nonuniformEXT(m.albedoSlot)], samplers[nonuniformEXT(m.samplerSlots[TEXTURE_ALBEDO])]),
            uv).a;
    }

    if (alpha < m.alphaCutoff) { discard; }
}
