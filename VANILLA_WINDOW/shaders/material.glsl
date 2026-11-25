// Material layout shared by every shader that reads the material buffer (set 0, binding 4):
// sceneshader.frag, uishader.frag, shadowmask.frag. Mirror of VANILLA_GPUMaterial (288 bytes) and the
// VANILLA_MaterialFlags1 / VANILLA_EntityFlags1 enums in main.h. Change it here only.

#ifndef VANILLA_MATERIAL_GLSL
#define VANILLA_MATERIAL_GLSL

struct Material {
    uint albedoSlot;
    uint normalSlot;
    uint metallicRoughnessSlot;
    uint occlusionSlot;

    uint emissiveSlot;
    uint samplerSlot;

    uint _padding1;
    uint _padding2;

    uint flags1;
    uint flags2;
    uint flags3;

    float alphaCutoff;

    vec4 baseColor;
    vec4 emissiveFactor;

    float metallic;
    float roughness;
    float normalScale;
    float occlusionStrength;

    // glTF 2.0
    uint samplerSlots[5];     // per texture: albedo, normal, metallicRoughness, occlusion, emissive
    uint texCoordBits;        // bit i: texture i samples UV1
    uint alphaMode;           // 0 opaque, 1 mask, 2 blend
    uint _padding3;
    vec4 uvTransforms[10];    // per texture 2x3 rows (KHR_texture_transform)
};

// Material flags1 (VANILLA_GPUMaterial.flags1)
const uint MATERIAL_VALID                  = 1u << 0;
const uint MATERIAL_USE_ALBEDO             = 1u << 1;  // set automatically for a valid albedo texture
const uint MATERIAL_USE_NORMAL_MAP         = 1u << 2;
const uint MATERIAL_USE_METALLIC_ROUGHNESS = 1u << 3;
const uint MATERIAL_USE_OCCLUSION          = 1u << 4;
const uint MATERIAL_USE_EMISSIVE           = 1u << 5;
const uint MATERIAL_DOUBLE_SIDED           = 1u << 6;
const uint MATERIAL_UNLIT                  = 1u << 7;
const uint MATERIAL_USE_VERTEX_COLOR       = 1u << 8;

// Entity flags1 (setMaterialComponent entityFlags1, per entity)
const uint ENTITY_NO_RENDER                = 1u << 0;
const uint ENTITY_NO_CAST_SHADOW           = 1u << 1;
const uint ENTITY_NO_RECEIVE_SHADOW        = 1u << 2;

// Texture order inside a material
const uint TEXTURE_ALBEDO             = 0u;
const uint TEXTURE_NORMAL             = 1u;
const uint TEXTURE_METALLIC_ROUGHNESS = 2u;
const uint TEXTURE_OCCLUSION          = 3u;
const uint TEXTURE_EMISSIVE           = 4u;

const uint ALPHA_OPAQUE = 0u;
const uint ALPHA_MASK   = 1u;
const uint ALPHA_BLEND  = 2u;

// UV of material texture `index`: its UV set, then KHR_texture_transform.
vec2 materialTextureUV(Material m, uint index, vec2 uv0, vec2 uv1) {
    vec2 uv = ((m.texCoordBits >> index) & 1u) != 0u ? uv1 : uv0;
    vec4 r0 = m.uvTransforms[index * 2u];
    vec4 r1 = m.uvTransforms[index * 2u + 1u];
    return vec2(dot(r0.xy, uv) + r0.z, dot(r1.xy, uv) + r1.z);
}

#endif
