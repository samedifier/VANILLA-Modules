/*
 * VANILLA Modules
 * Copyright (C) 2026 Samed
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License, version 2 only,
 * as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 *
 * See the LICENSE file for the full license text.
 */

#ifndef VANILLA_WINDOW_WRAPPER_H
#define VANILLA_WINDOW_WRAPPER_H

#include "primitives.h"
#include <stdint.h>
#include <stdio.h>

#include <dlfcn.h>
#ifndef __VANILLA_WRAPPER_REDEF
#define __VANILLA_WRAPPER_REDEF
    #define VANILLA_DLOPEN(path)          dlopen((path), RTLD_NOW)
    #define VANILLA_DLSYM(h, name, field) *(void**)(&(field)) = dlsym((h), (name))
    #define VANILLA_DLCLOSE(h)            dlclose(h)
#endif

typedef enum {
    VANILLA_WINDOW_SUCCESS = 0,

    VANILLA_WINDOW_ERR_NULL_POINTER,
    VANILLA_WINDOW_ERR_ALLOC_FAILED,
    VANILLA_WINDOW_ERR_ALREADY_STARTED,
    VANILLA_WINDOW_ERR_DIDNT_START,
    VANILLA_WINDOW_ERR_CAPACITY_REACHED,

    VANILLA_WINDOW_ERR_TEXTURE_SIZE_LIMIT,
    VANILLA_WINDOW_ERR_CREATE_TEXTURE,
    VANILLA_WINDOW_ERR_INVALID_TEXTURE,

    VANILLA_WINDOW_ERR_CREATE_SAMPLER,
    VANILLA_WINDOW_ERR_INVALID_SAMPLER,

    VANILLA_WINDOW_ERR_INVALID_MATERIAL,
    VANILLA_WINDOW_ERR_HAS_NO_MATERIAL,
    VANILLA_WINDOW_ERR_ADD_MATERIAL_COMPONENT,
    VANILLA_WINDOW_ERR_REMOVE_MATERIAL_COMPONENT,
    VANILLA_WINDOW_ERR_GET_MATERIAL_COMPONENT,

    VANILLA_WINDOW_ERR_INVALID_LIGHT,

    VANILLA_WINDOW_ERR_INVALID_USER_BUFFER,
    VANILLA_WINDOW_ERR_USER_BUFFER_NO_ACCESS,
    VANILLA_WINDOW_ERR_USER_BUFFER_RANGE,
    VANILLA_WINDOW_ERR_USER_BUFFER_TRANSFER,

    VANILLA_WINDOW_ERR_INVALID_COMPUTE_GROUP_SIZE,

    VANILLA_WINDOW_ERR_LIGHTING_UNSUPPORTED,   /* VANILLA_setLightingSupport(false) was called */

    VANILLA_WINDOW_ERR_INVALID_SKIN,           /* unknown / destroyed SkinID */
    VANILLA_WINDOW_ERR_INVALID_ARGUMENT,       /* bad counts, joint index out of range, ... */
    VANILLA_WINDOW_ERR_INVALID_MORPH,          /* unknown / destroyed MorphID */
} VANILLA_WINDOW_RESULT;

typedef enum {
    VANILLA_MEMORY_HOST_VISIBLE,
    VANILLA_MEMORY_HYBRID,
    VANILLA_MEMORY_DEVICE_LOCAL
} VANILLA_MemoryMode;

typedef enum {
    VANILLA_VERTEXFORMAT_R32_FLOAT,
    VANILLA_VERTEXFORMAT_R32G32_FLOAT,
    VANILLA_VERTEXFORMAT_R32G32B32_FLOAT,
    VANILLA_VERTEXFORMAT_R32G32B32A32_FLOAT,
    VANILLA_VERTEXFORMAT_R8G8B8A8_UNORM,      /* packed color, read as vec4 0..1 */
    VANILLA_VERTEXFORMAT_R32G32B32A32_UINT    /* integers (e.g. joint indices) */
} VANILLA_VertexFormat;

typedef struct {
    uint32_t location;
    VANILLA_VertexFormat format;
    uint32_t anycoreOffset;
    uint32_t vanillaOffset;
    uint32_t size;
} VANILLA_VertexAttribute;

typedef struct {
    const VANILLA_VertexAttribute* attributes;
    uint32_t attributeCount;
} VANILLA_VertexLayout;

typedef struct { float m[16]; } mat4;

typedef struct { ID32 slot; ID16 generation; } TextureID;
typedef struct { ID32 slot; ID16 generation; } SamplerID;
typedef struct { ID32 slot; ID16 generation; } MaterialID;
typedef struct { ID32 slot; ID16 generation; } LightID;
typedef struct { ID32 slot; ID16 generation; } SkinID;
typedef struct { ID32 slot; ID16 generation; } MorphID;

/* glTF JOINTS_0 / WEIGHTS_0 of one model vertex. Joints index the skin's joint list; weights are normalized. */
typedef struct {
    uint16_t joints[4];
    float    weights[4];
} VANILLA_SkinVertex;

/*
 * glTF mesh.targets of one model (registerMorph). Each array holds targetCount x vertexCount deltas, target-major:
 * delta of vertex v in target t = array[t * vertexCount + v]. NULL = attribute not morphed (at least one is needed).
 * Sparse glTF accessors must be expanded. Tangent deltas are xyz (w is never morphed).
 */
typedef struct {
    uint32_t     vertexCount;      /* = the model's vertex count */
    uint32_t     targetCount;
    const Vec3f* positions;
    const Vec3f* normals;
    const Vec3f* tangents;
    const float* defaultWeights;   /* glTF mesh.weights (targetCount values), NULL = all 0; copied */
} VANILLA_MorphCreateInfo;

typedef enum {
    VANILLA_LIGHTTYPE_DIRECTIONAL,
    VANILLA_LIGHTTYPE_POINT,
    VANILLA_LIGHTTYPE_SPOT
} VANILLA_LightType;

typedef struct {
    VANILLA_LightType type;
    Vec3f position;    /* 2D: z = height above the sprite plane */
    Vec3f direction;
    Vec3f color;
    float intensity;
    float range;
    float innerCone;   /* cosine, innerCone > outerCone */
    float outerCone;
    bool  castShadows; /* directional: one light; point/spot: up to setLocalShadowLimit (beyond it: baked, no shadow) */
    float sourceSize;  /* PCSS: directional = angular diameter (radians, sun ~0.0093); point/spot = radius (world units) */
} VANILLA_LightCreateInfo;

typedef enum {
    VANILLA_SHADOWFILTER_PCF,  /* fixed-width soft edge */
    VANILLA_SHADOWFILTER_PCSS  /* contact-hardening: sharp near the caster, softer farther away */
} VANILLA_ShadowFilter;

typedef enum {
    VANILLA_TEXTUREFORMAT_R8_UNORM,
    VANILLA_TEXTUREFORMAT_RG8_UNORM,
    VANILLA_TEXTUREFORMAT_RGB8_UNORM,
    VANILLA_TEXTUREFORMAT_RGBA8_UNORM,

    VANILLA_TEXTUREFORMAT_R8_SRGB,
    VANILLA_TEXTUREFORMAT_RG8_SRGB,
    VANILLA_TEXTUREFORMAT_RGB8_SRGB,
    VANILLA_TEXTUREFORMAT_RGBA8_SRGB,

    VANILLA_TEXTUREFORMAT_R16_FLOAT,
    VANILLA_TEXTUREFORMAT_RG16_FLOAT,
    VANILLA_TEXTUREFORMAT_RGBA16_FLOAT,

    VANILLA_TEXTUREFORMAT_R32_FLOAT,
    VANILLA_TEXTUREFORMAT_RG32_FLOAT,
    VANILLA_TEXTUREFORMAT_RGBA32_FLOAT,

    VANILLA_TEXTUREFORMAT_DEPTH32_FLOAT,
    VANILLA_TEXTUREFORMAT_DEPTH24_STENCIL8
} VANILLA_TextureFormat;

typedef enum {
    VANILLA_MIPMAPMODE_NONE,
    VANILLA_MIPMAPMODE_GENERATE,
    VANILLA_MIPMAPMODE_PROVIDED
} VANILLA_MipmapMode;

typedef struct {
    uint32_t width;
    uint32_t height;

    VANILLA_TextureFormat format;
    
    VANILLA_MipmapMode mipmapMode;
    uint32_t mipLevels;

    const void* data;
    uint64_t dataSize;
} VANILLA_TextureCreateInfo;

typedef enum {
    VANILLA_FILTER_NEAREST,
    VANILLA_FILTER_LINEAR
} VANILLA_Filter;

typedef enum {
    VANILLA_MIPMAPFILTER_NEAREST,
    VANILLA_MIPMAPFILTER_LINEAR,
    VANILLA_MIPMAPFILTER_NONE      /* base level only (glTF minFilter NEAREST / LINEAR without mipmap) */
} VANILLA_MipmapFilter;

typedef enum {
    VANILLA_ADDRESS_REPEAT,
    VANILLA_ADDRESS_CLAMP_TO_EDGE,
    VANILLA_ADDRESS_CLAMP_TO_BORDER,
    VANILLA_ADDRESS_MIRRORED_REPEAT
} VANILLA_AddressMode;

typedef enum {
    VANILLA_BORDER_FLOAT_TRANSPARENT_BLACK,
    VANILLA_BORDER_INT_TRANSPARENT_BLACK,

    VANILLA_BORDER_FLOAT_OPAQUE_BLACK,
    VANILLA_BORDER_INT_OPAQUE_BLACK,

    VANILLA_BORDER_FLOAT_OPAQUE_WHITE,
    VANILLA_BORDER_INT_OPAQUE_WHITE
} VANILLA_BorderColor;

typedef struct {
    VANILLA_Filter magFilter;
    VANILLA_Filter minFilter;

    VANILLA_MipmapFilter mipmapFilter;

    VANILLA_AddressMode addressU;
    VANILLA_AddressMode addressV;
    VANILLA_AddressMode addressW;

    uint32_t maxAnisotropy;

    VANILLA_BorderColor borderColor;
} VANILLA_SamplerCreateInfo;

typedef enum {
    VANILLA_ALPHAMODE_OPAQUE = 0,
    VANILLA_ALPHAMODE_MASK,
    VANILLA_ALPHAMODE_BLEND
} VANILLA_AlphaMode;

/* Texture order inside a material (textureSamplers[] / textureTransforms[]). */
typedef enum {
    VANILLA_MATERIAL_TEXTURE_ALBEDO = 0,
    VANILLA_MATERIAL_TEXTURE_NORMAL,
    VANILLA_MATERIAL_TEXTURE_METALLIC_ROUGHNESS,
    VANILLA_MATERIAL_TEXTURE_OCCLUSION,
    VANILLA_MATERIAL_TEXTURE_EMISSIVE,
    VANILLA_MATERIAL_TEXTURE_COUNT
} VANILLA_MaterialTexture;

/*
 * glTF textureInfo.texCoord + KHR_texture_transform: uv' = translate(offset) * rotate(rotation) * scale(scale) * uv.
 * All zero = identity on UV0 (a zero scale counts as 1).
 */
typedef struct {
    float    offset[2];
    float    scale[2];
    float    rotation;   /* radians, counter-clockwise in UV space */
    uint32_t texCoord;   /* 0 = UV0, 1 = UV1 */
} VANILLA_TextureTransform;

typedef struct {
    TextureID albedo;
    TextureID normal;
    TextureID metallicRoughness;
    TextureID occlusion;
    TextureID emissive;
    SamplerID sampler;

    uint32_t flags1;
    uint32_t flags2;
    uint32_t flags3;

    float alphaCutoff;

    Vec4f baseColor;
    Vec4f emissiveFactor;

    float metallic;
    float roughness;
    float normalScale;
    float occlusionStrength;

    /* ---- glTF 2.0 (zero-initialized = old behaviour: opaque, single-sided, lit, shared sampler, UV0) ---- */
    VANILLA_AlphaMode alphaMode;   /* OPAQUE: alpha ignored; MASK: discard below alphaCutoff; BLEND: transparent pass */
    bool doubleSided;              /* false: back faces are discarded (the pipeline should use CULL_NONE) */
    bool unlit;                    /* KHR_materials_unlit: base color only, no lighting */
    bool vertexColor;              /* multiply base color by the vertex color (glTF COLOR_0 present) */

    /* Per texture, indexed by VANILLA_MaterialTexture. Invalid sampler (generation 0) = `sampler` above. */
    SamplerID textureSamplers[VANILLA_MATERIAL_TEXTURE_COUNT];
    VANILLA_TextureTransform textureTransforms[VANILLA_MATERIAL_TEXTURE_COUNT];
} VANILLA_MaterialCreateInfo;

typedef enum {
    VANILLA_MATERIALFLAGS1_VALID                   = 1u << 0,
    VANILLA_MATERIALFLAGS1_USE_ALBEDO              = 1u << 1,
    VANILLA_MATERIALFLAGS1_USE_NORMAL_MAP          = 1u << 2,
    VANILLA_MATERIALFLAGS1_USE_METALLIC_ROUGHNESS  = 1u << 3,
    VANILLA_MATERIALFLAGS1_USE_OCCLUSION           = 1u << 4,
    VANILLA_MATERIALFLAGS1_USE_EMISSIVE            = 1u << 5,
    VANILLA_MATERIALFLAGS1_DOUBLE_SIDED            = 1u << 6,   /* set from MaterialCreateInfo.doubleSided */
    VANILLA_MATERIALFLAGS1_UNLIT                   = 1u << 7,   /* set from MaterialCreateInfo.unlit */
    VANILLA_MATERIALFLAGS1_USE_VERTEX_COLOR        = 1u << 8    /* set from MaterialCreateInfo.vertexColor */
} VANILLA_MaterialFlags1;

typedef enum {
    VANILLA_ENTITYFLAGS1_NO_RENDER         = 1u << 0,
    VANILLA_ENTITYFLAGS1_NO_CAST_SHADOW    = 1u << 1,
    VANILLA_ENTITYFLAGS1_NO_RECEIVE_SHADOW = 1u << 2,
} VANILLA_EntityFlags1;

typedef enum {
    VANILLA_PROJECTION_PERSPECTIVE,
    VANILLA_PROJECTION_ORTHOGRAPHIC
} VANILLA_ProjectionType;

typedef enum {
    VANILLA_CURSORMODE_NORMAL,
    VANILLA_CURSORMODE_HIDDEN,
    VANILLA_CURSORMODE_DISABLED
} VANILLA_CursorMode;

typedef struct { ID32 slot; ID16 generation; } EventBindID;

typedef enum {
    VANILLA_INPUTEVENT_KEY_A,
    VANILLA_INPUTEVENT_KEY_B,
    VANILLA_INPUTEVENT_KEY_C,
    VANILLA_INPUTEVENT_KEY_D,
    VANILLA_INPUTEVENT_KEY_E,
    VANILLA_INPUTEVENT_KEY_F,
    VANILLA_INPUTEVENT_KEY_G,
    VANILLA_INPUTEVENT_KEY_H,
    VANILLA_INPUTEVENT_KEY_I,
    VANILLA_INPUTEVENT_KEY_J,
    VANILLA_INPUTEVENT_KEY_K,
    VANILLA_INPUTEVENT_KEY_L,
    VANILLA_INPUTEVENT_KEY_M,
    VANILLA_INPUTEVENT_KEY_N,
    VANILLA_INPUTEVENT_KEY_O,
    VANILLA_INPUTEVENT_KEY_P,
    VANILLA_INPUTEVENT_KEY_Q,
    VANILLA_INPUTEVENT_KEY_R,
    VANILLA_INPUTEVENT_KEY_S,
    VANILLA_INPUTEVENT_KEY_T,
    VANILLA_INPUTEVENT_KEY_U,
    VANILLA_INPUTEVENT_KEY_V,
    VANILLA_INPUTEVENT_KEY_W,
    VANILLA_INPUTEVENT_KEY_X,
    VANILLA_INPUTEVENT_KEY_Y,
    VANILLA_INPUTEVENT_KEY_Z,

    VANILLA_INPUTEVENT_KEY_0,
    VANILLA_INPUTEVENT_KEY_1,
    VANILLA_INPUTEVENT_KEY_2,
    VANILLA_INPUTEVENT_KEY_3,
    VANILLA_INPUTEVENT_KEY_4,
    VANILLA_INPUTEVENT_KEY_5,
    VANILLA_INPUTEVENT_KEY_6,
    VANILLA_INPUTEVENT_KEY_7,
    VANILLA_INPUTEVENT_KEY_8,
    VANILLA_INPUTEVENT_KEY_9,

    VANILLA_INPUTEVENT_KEY_SPACE,
    VANILLA_INPUTEVENT_KEY_ENTER,
    VANILLA_INPUTEVENT_KEY_ESCAPE,
    VANILLA_INPUTEVENT_KEY_TAB,
    VANILLA_INPUTEVENT_KEY_BACKSPACE,

    VANILLA_INPUTEVENT_KEY_LEFT,
    VANILLA_INPUTEVENT_KEY_RIGHT,
    VANILLA_INPUTEVENT_KEY_UP,
    VANILLA_INPUTEVENT_KEY_DOWN,

    VANILLA_INPUTEVENT_KEY_LEFT_SHIFT,
    VANILLA_INPUTEVENT_KEY_RIGHT_SHIFT,
    VANILLA_INPUTEVENT_KEY_LEFT_CONTROL,
    VANILLA_INPUTEVENT_KEY_RIGHT_CONTROL,
    VANILLA_INPUTEVENT_KEY_LEFT_ALT,
    VANILLA_INPUTEVENT_KEY_RIGHT_ALT,
    VANILLA_INPUTEVENT_KEY_LEFT_SUPER,
    VANILLA_INPUTEVENT_KEY_RIGHT_SUPER,

    VANILLA_INPUTEVENT_KEY_F1,
    VANILLA_INPUTEVENT_KEY_F2,
    VANILLA_INPUTEVENT_KEY_F3,
    VANILLA_INPUTEVENT_KEY_F4,
    VANILLA_INPUTEVENT_KEY_F5,
    VANILLA_INPUTEVENT_KEY_F6,
    VANILLA_INPUTEVENT_KEY_F7,
    VANILLA_INPUTEVENT_KEY_F8,
    VANILLA_INPUTEVENT_KEY_F9,
    VANILLA_INPUTEVENT_KEY_F10,
    VANILLA_INPUTEVENT_KEY_F11,
    VANILLA_INPUTEVENT_KEY_F12,

    VANILLA_INPUTEVENT_KEY_INSERT,
    VANILLA_INPUTEVENT_KEY_DELETE,
    VANILLA_INPUTEVENT_KEY_HOME,
    VANILLA_INPUTEVENT_KEY_END,
    VANILLA_INPUTEVENT_KEY_PAGE_UP,
    VANILLA_INPUTEVENT_KEY_PAGE_DOWN,

    VANILLA_INPUTEVENT_KEY_CAPS_LOCK,
    VANILLA_INPUTEVENT_KEY_NUM_LOCK,
    VANILLA_INPUTEVENT_KEY_SCROLL_LOCK,

    VANILLA_INPUTEVENT_KEY_PRINT_SCREEN,
    VANILLA_INPUTEVENT_KEY_PAUSE,

    VANILLA_INPUTEVENT_KEY_KP_0,
    VANILLA_INPUTEVENT_KEY_KP_1,
    VANILLA_INPUTEVENT_KEY_KP_2,
    VANILLA_INPUTEVENT_KEY_KP_3,
    VANILLA_INPUTEVENT_KEY_KP_4,
    VANILLA_INPUTEVENT_KEY_KP_5,
    VANILLA_INPUTEVENT_KEY_KP_6,
    VANILLA_INPUTEVENT_KEY_KP_7,
    VANILLA_INPUTEVENT_KEY_KP_8,
    VANILLA_INPUTEVENT_KEY_KP_9,

    VANILLA_INPUTEVENT_KEY_KP_DECIMAL,
    VANILLA_INPUTEVENT_KEY_KP_DIVIDE,
    VANILLA_INPUTEVENT_KEY_KP_MULTIPLY,
    VANILLA_INPUTEVENT_KEY_KP_SUBTRACT,
    VANILLA_INPUTEVENT_KEY_KP_ADD,
    VANILLA_INPUTEVENT_KEY_KP_ENTER,
    VANILLA_INPUTEVENT_KEY_KP_EQUAL,

    VANILLA_INPUTEVENT_MOUSE_LEFT,
    VANILLA_INPUTEVENT_MOUSE_RIGHT,
    VANILLA_INPUTEVENT_MOUSE_MIDDLE,

    VANILLA_INPUTEVENT_MOUSE_BUTTON_4,
    VANILLA_INPUTEVENT_MOUSE_BUTTON_5,
    VANILLA_INPUTEVENT_MOUSE_BUTTON_6,
    VANILLA_INPUTEVENT_MOUSE_BUTTON_7,
    VANILLA_INPUTEVENT_MOUSE_BUTTON_8,

    VANILLA_INPUTEVENT_MOUSE_MOVE,
    VANILLA_INPUTEVENT_MOUSE_SCROLL,

    VANILLA_INPUTEVENT_WINDOW_CLOSE,
    VANILLA_INPUTEVENT_WINDOW_FOCUS_GAINED,
    VANILLA_INPUTEVENT_WINDOW_FOCUS_LOST,

    VANILLA_INPUTEVENT_WINDOW_MINIMIZED,
    VANILLA_INPUTEVENT_WINDOW_MAXIMIZED,
    VANILLA_INPUTEVENT_WINDOW_RESTORED,

    VANILLA_INPUTEVENT_WINDOW_RESIZED,

    VANILLA_INPUTEVENT_LIMIT
} VANILLA_InputEvent;

typedef enum {
    VANILLA_INPUTSTATE_NONE     = 0,
    VANILLA_INPUTSTATE_ACTIVE   = 1 << 0,
    VANILLA_INPUTSTATE_PRESSED  = 1 << 1,
    VANILLA_INPUTSTATE_RELEASED = 1 << 2
} VANILLA_InputState;

typedef enum {
    VANILLA_PRIMITIVE_TOPOLOGY_POINT_LIST,
    VANILLA_PRIMITIVE_TOPOLOGY_LINE_LIST,
    VANILLA_PRIMITIVE_TOPOLOGY_LINE_STRIP,
    VANILLA_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST,
    VANILLA_PRIMITIVE_TOPOLOGY_TRIANGLE_STRIP,
    VANILLA_PRIMITIVE_TOPOLOGY_TRIANGLE_FAN
} VANILLA_PrimitiveTopology;

typedef enum {
    VANILLA_POLYGON_MODE_FILL,
    VANILLA_POLYGON_MODE_LINE,
    VANILLA_POLYGON_MODE_POINT
} VANILLA_PolygonMode;

typedef enum {
    VANILLA_CULL_MODE_NONE,
    VANILLA_CULL_MODE_FRONT,
    VANILLA_CULL_MODE_BACK,
    VANILLA_CULL_MODE_FRONT_AND_BACK
} VANILLA_CullMode;

typedef enum {
    VANILLA_FRONT_FACE_COUNTER_CLOCKWISE,
    VANILLA_FRONT_FACE_CLOCKWISE
} VANILLA_FrontFace;

typedef enum {
    VANILLA_COMPARE_NEVER,
    VANILLA_COMPARE_LESS,
    VANILLA_COMPARE_EQUAL,
    VANILLA_COMPARE_LESS_OR_EQUAL,
    VANILLA_COMPARE_GREATER,
    VANILLA_COMPARE_NOT_EQUAL,
    VANILLA_COMPARE_GREATER_OR_EQUAL,
    VANILLA_COMPARE_ALWAYS
} VANILLA_CompareOp;

typedef enum {
    VANILLA_BLEND_FACTOR_ZERO,
    VANILLA_BLEND_FACTOR_ONE,
    VANILLA_BLEND_FACTOR_SRC_COLOR,
    VANILLA_BLEND_FACTOR_ONE_MINUS_SRC_COLOR,
    VANILLA_BLEND_FACTOR_DST_COLOR,
    VANILLA_BLEND_FACTOR_ONE_MINUS_DST_COLOR,
    VANILLA_BLEND_FACTOR_SRC_ALPHA,
    VANILLA_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA,
    VANILLA_BLEND_FACTOR_DST_ALPHA,
    VANILLA_BLEND_FACTOR_ONE_MINUS_DST_ALPHA
} VANILLA_BlendFactor;

typedef enum {
    VANILLA_BLEND_OP_ADD,
    VANILLA_BLEND_OP_SUBTRACT,
    VANILLA_BLEND_OP_REVERSE_SUBTRACT,
    VANILLA_BLEND_OP_MIN,
    VANILLA_BLEND_OP_MAX
} VANILLA_BlendOp;

typedef enum {
    VANILLA_STENCIL_KEEP,
    VANILLA_STENCIL_ZERO,
    VANILLA_STENCIL_REPLACE,
    VANILLA_STENCIL_INCREMENT_AND_CLAMP,
    VANILLA_STENCIL_DECREMENT_AND_CLAMP,
    VANILLA_STENCIL_INVERT,
    VANILLA_STENCIL_INCREMENT_AND_WRAP,
    VANILLA_STENCIL_DECREMENT_AND_WRAP
} VANILLA_StencilOp;

typedef struct {
    VANILLA_StencilOp failOp;
    VANILLA_StencilOp passOp;
    VANILLA_StencilOp depthFailOp;
    VANILLA_CompareOp compareOp;
    uint32_t compareMask;
    uint32_t writeMask;
    uint32_t reference;
} VANILLA_StencilState;

typedef enum {
    VANILLA_SAMPLE_COUNT_1 = 1,
    VANILLA_SAMPLE_COUNT_2 = 2,
    VANILLA_SAMPLE_COUNT_4 = 4,
    VANILLA_SAMPLE_COUNT_8 = 8,
    VANILLA_SAMPLE_COUNT_16 = 16,
    VANILLA_SAMPLE_COUNT_32 = 32,
    VANILLA_SAMPLE_COUNT_64 = 64
} VANILLA_SampleCount;

typedef struct {
    const uint32_t* vertexShader;
    size_t vertexShaderSize;

    const uint32_t* fragmentShader;
    size_t fragmentShaderSize;

    VANILLA_PrimitiveTopology topology;

    VANILLA_PolygonMode polygonMode;
    VANILLA_CullMode cullMode;
    VANILLA_FrontFace frontFace;

    bool depthTestEnable;
    bool depthWriteEnable;
    VANILLA_CompareOp depthCompareOp;

    bool stencilTestEnable;
    VANILLA_StencilState stencilFront;
    VANILLA_StencilState stencilBack;

    bool blendEnable;
    VANILLA_BlendFactor srcColorBlendFactor;
    VANILLA_BlendFactor dstColorBlendFactor;
    VANILLA_BlendOp colorBlendOp;
    VANILLA_BlendFactor srcAlphaBlendFactor;
    VANILLA_BlendFactor dstAlphaBlendFactor;
    VANILLA_BlendOp alphaBlendOp;

    uint8_t colorWriteMask;

    /* Multisampling is global: see VANILLA_setAntiAliasing. */

    bool castShadows; /* entities of this pipeline are drawn into the shadow map */

    /*
     * Keep entities drawn in slot order (e.g. overlapping transparent UI).
     * Costs O(n) per add/remove in this pipeline: use it only for pipelines with
     * few entities. false = fastest (order may change when entities are removed).
     */
    bool preserveDrawOrder;

    /*
     * Render this pipeline in HDR (RGBA16F). The module tone maps it (exposure +
     * setToneMapping) before post-processing sees it or before it reaches the screen.
     * Meant for the lit scene; leave UI pipelines false.
     */
    bool hdr;

    /*
     * Leave this pipeline out of GPU culling (VANILLA_setCulling). Set it for pipelines whose vertex shader does
     * not place entities with transform x camera viewProjection (UI, screen-space overlays): their entities would
     * be culled against the wrong frustum. preserveDrawOrder pipelines are never culled either.
     */
    bool skipCulling;
} VANILLA_PipelineCreateInfo;

typedef enum {
    VANILLA_CULLING_NONE,               /* draw every entity of every list */
    VANILLA_CULLING_FRUSTUM,            /* GPU: entities outside the camera / shadow frusta are skipped (default) */
    VANILLA_CULLING_FRUSTUM_OCCLUSION   /* + entities hidden behind others (main camera, previous frame depth) */
} VANILLA_Culling;

typedef enum {
    VANILLA_AA_NONE,        /* off (default) */
    VANILLA_AA_MSAA,        /* multisampling: sharp geometry edges, every pipeline */
    VANILLA_AA_FXAA,        /* screen-space, cheap; HDR (scene) pipelines only, not UI */
    VANILLA_AA_MSAA_FXAA    /* both */
} VANILLA_AntiAliasing;

typedef enum {
    VANILLA_TONEMAPPING_NONE,   /* exposure + clamp */
    VANILLA_TONEMAPPING_ACES,   /* filmic, contrasty (default) */
    VANILLA_TONEMAPPING_AGX     /* neutral, keeps hues in bright areas */
} VANILLA_ToneMapping;

/*
 * Physical atmosphere (3D only). Start from getDefaultAtmosphere() (Earth) and change fields.
 * The sun is the shadow-casting directional light (else the first enabled directional light):
 * its direction places the sun, color * intensity is the light above the atmosphere,
 * sourceSize is the sun disc's angular diameter.
 */
typedef struct {
    float metersPerUnit;        /* world unit -> meters (default 1) */
    float groundHeight;         /* world Y of the ground / sea level (default 0), +Y is up */

    float planetRadius;         /* km (default 6360) */
    float atmosphereHeight;     /* km above the ground (default 100) */

    Vec3f rayleighScattering;   /* 1/km at ground level (default 0.005802, 0.013558, 0.033100) */
    float rayleighScaleHeight;  /* km (default 8) */

    float mieScattering;        /* 1/km at ground level (default 0.003996); haze */
    float mieAbsorption;        /* 1/km (default 0.000444) */
    float mieScaleHeight;       /* km (default 1.2) */
    float mieAnisotropy;        /* 0..0.999, glow around the sun (default 0.8) */

    Vec3f ozoneAbsorption;      /* 1/km at peak (default 0.000650, 0.001881, 0.000085) */
    Vec3f groundAlbedo;         /* light bounced by the ground into the sky (default 0.3) */

    Vec3f nightColor;           /* lowest sky / ambient light when the sun is down (default 0.004, 0.006, 0.012) */

    /* Aerial perspective (haze over distance): distance multiplier. 1 = physical (only visible over
       kilometers); 10-50 exaggerates it for small scenes; 0 = off (default 1). */
    float aerialPerspectiveScale;
} VANILLA_Atmosphere;

/*
 * Editor grid (3D only, hidden by default): lines on the plane y = height, drawn per pixel
 * (constant pixel width, anti-aliased), transparent, fading with distance.
 * X axis (z = 0) red, Z axis (x = 0) blue.
 */
typedef struct {
    float    height;        /* world Y of the plane (default 0) */
    float    cellSize;      /* world units between minor lines (default 1) */
    uint32_t majorEvery;    /* a major line every N cells (default 10) */
    float    fadeDistance;  /* fully faded at this distance from the camera (default 150) */
    Vec4f    minorColor;    /* rgb linear (HDR), a opacity (default 0.55, 0.55, 0.55, 0.25) */
    Vec4f    majorColor;    /* (default 0.75, 0.75, 0.75, 0.5) */
} VANILLA_GridInfo;

typedef struct {
    const uint32_t* vertexShader;
    size_t vertexShaderSize;

    const uint32_t* fragmentShader;
    size_t fragmentShaderSize;
} VANILLA_PostProcessingCreateInfo;

typedef enum {
    VANILLA_USER_BUFFER_HOST_VISIBLE = 0,
    VANILLA_USER_BUFFER_DEVICE_LOCAL
} VANILLA_UserBufferMemory;

typedef struct {
    uint32_t size;
    const void *initialData;
    VANILLA_UserBufferMemory memoryMode;
    bool runtimeAccess;
} VANILLA_UserBufferInfo;

typedef struct {
    const uint32_t* computeShader;
    size_t computeShaderSize;
    uint32_t groupSize;
} VANILLA_ComputeShaderInfo;

///

typedef struct {
    /* Returns the VANILLA WINDOW version. */
    uint32_t (*getVersion) (void);

    /* Sets the application name. */
    VANILLA_WINDOW_RESULT (*setAppName) (const char* appName);

    /* Sets the window title. */
    VANILLA_WINDOW_RESULT (*setWindowTitle) (const char* title);

    /* Sets the application version. */
    VANILLA_WINDOW_RESULT (*setAppVersion) (uint32_t major, uint32_t minor, uint32_t patch);

    /* Sets the window size. */
    void (*setWindowSize) (unsigned int width, unsigned int height);

    /* Sets the window position. */
    void (*setWindowPosition) (unsigned int x, unsigned int y);

    /* Sets the cursor position within the window. */
    void (*setCursorPosition) (unsigned int x, unsigned int y);

    /* Shows the window. */
    void (*showWindow) (void);

    /* Hides the window. */
    void (*hideWindow) (void);

    /* Sets the maximum number of vertices. */
    VANILLA_WINDOW_RESULT (*setVertexLimit) (uint64_t limit);

    /* Sets the maximum number of indices. */
    VANILLA_WINDOW_RESULT (*setIndexLimit) (uint64_t limit);

    /* Sets the memory allocation mode used by VANILLA. */
    VANILLA_WINDOW_RESULT (*setMemoryMode) (VANILLA_MemoryMode memoryMode);

    /* Sets the vertex buffer layout. */
    VANILLA_WINDOW_RESULT (*setVertexLayout) (uint32_t stride, const VANILLA_VertexAttribute* attributes, uint32_t attributeCount);

    /* Sets the maximum number of materials. */
    void (*setMaterialLimit) (uint32_t limit);

    /* Sets the maximum number of textures. */
    void (*setTextureLimit) (uint32_t limit);

    /* Sets the maximum size of a texture upload. */
    void (*setTextureSizeLimit) (uint32_t limit);

    /* Sets the maximum number of samplers. */
    void (*setSamplerLimit) (uint32_t limit);

    /* Enables vertical synchronization. */
    void (*enableVSync) (void);

    /* Disables vertical synchronization. */
    void (*disableVSync) (void);

    /* Sets the sky color. */
    void (*setSkyColor) (Vec3f color);

    /* Sets the camera position. */
    void (*setCameraPosition) (Vec3f position);

    /* Sets the camera UI offset. */
    void (*setCameraUIOffset) (float offsetx, float offsety);

    /* Sets the camera target. */
    void (*setCameraTarget) (Vec3f target);

    /* Sets the camera up direction. */
    void (*setCameraUp) (Vec3f up);

    /* Sets the camera field of view in degrees. */
    void (*setCameraFOV) (float degree);

    /* Sets the camera near clipping plane. */
    void (*setCameraNear) (float nearPlane);

    /* Sets the camera far clipping plane. */
    void (*setCameraFar) (float farPlane);

    /* Sets the camera projection type and orthographic size. */
    void (*setCameraProjection) (VANILLA_ProjectionType projection, float orthographicSize);

    /* Notifies VANILLA that a new ANYCORE chunk was allocated. */
    VANILLA_WINDOW_RESULT (*notifyNewChunkAlloc) (void);

    /* Notifies VANILLA that the last ANYCORE chunk was freed. */
    VANILLA_WINDOW_RESULT (*notifyLastChunkFree) (void);

    /* Dispatches pending input events to their bound functions. */
    void (*inputDispatch) (void);

    /* Binds a function to an input event. */
    bool (*bindInputEvent) (VANILLA_InputEvent event, void (*function) (VANILLA_InputState), EventBindID* outBindID);

    /* Removes a previously created input event binding. */
    void (*unbindInputEvent) (VANILLA_InputEvent event, EventBindID id);

    /* Retrieves the mouse scroll offsets and resets them after reading. */
    void (*getScrollOffset) (double* xOffset, double* yOffset);

    /* Creates a material and returns its identifier. */
    VANILLA_WINDOW_RESULT (*createMaterial) (VANILLA_MaterialCreateInfo materialCreateInfo, MaterialID* outMaterialID);

    /* Destroys a material. */
    VANILLA_WINDOW_RESULT (*destroyMaterial) (MaterialID materialID);

    /* Assigns a material and its flags to an entity. */
    VANILLA_WINDOW_RESULT (*setMaterialComponent) (EntityID entityID, MaterialID materialID, uint32_t entityFlags1, uint32_t entityFlags2, uint32_t entityFlags3);

    /* Removes the material component from an entity. */
    VANILLA_WINDOW_RESULT (*removeMaterialComponent) (EntityID entityID);

    /* Retrieves the material assigned to an entity. */
    VANILLA_WINDOW_RESULT (*getMaterialComponent) (EntityID entityID, MaterialID* outMaterialID);

    /* Checks whether an entity has a material component. */
    bool (*hasMaterialComponent) (EntityID entityID);

    /* Creates a sampler and returns its identifier. */
    VANILLA_WINDOW_RESULT (*createSampler) (const VANILLA_SamplerCreateInfo* createInfo, SamplerID* outSamplerID);

    /* Destroys a sampler. */
    VANILLA_WINDOW_RESULT (*destroySampler) (SamplerID samplerID);

    /* Creates a texture and returns its identifier. */
    VANILLA_WINDOW_RESULT (*createTexture) (const VANILLA_TextureCreateInfo* createInfo, TextureID* outTextureID);

    /* Destroys a texture. */
    VANILLA_WINDOW_RESULT (*destroyTexture) (TextureID textureID);

    /* Returns the current window title. */
    const char* (*getWindowTitle) (void);

    /* Returns all characters received during the current frame. */
    const uint16_t* (*getTextInput) (uint32_t* outCount);

    /* Retrieves the current window size. */
    void (*getWindowSize) (unsigned int* width, unsigned int* height);

    /* Retrieves the current window position. */
    void (*getWindowPosition) (unsigned int* x, unsigned int* y);

    /* Retrieves the current cursor position within the window. */
    void (*getCursorPosition) (unsigned int* x, unsigned int* y);

    /* Returns whether the window is currently visible. */
    bool (*isWindowVisible) (void);

    /* Retrieves the primary monitor size. */
    void (*getMonitorSize) (unsigned int* width, unsigned int* height);

    /* Returns the current vertex limit. */
    uint64_t (*getVertexLimit) (void);

    /* Returns the current index limit. */
    uint64_t (*getIndexLimit) (void);

    /* Returns the current memory allocation mode. */
    VANILLA_MemoryMode (*getMemoryMode) (void);

    /* Returns whether vertical synchronization is enabled. */
    bool (*isVSyncEnabled) (void);

    VANILLA_WINDOW_RESULT (*configurePipelines) (const VANILLA_PipelineCreateInfo* infos, uint16_t count);
    void (*setPipelineIndex) (EntityID entityID, uint16_t pipelineidx, bool didcreate);

    void (*enablePostProcessing) (void);
    void (*disablePostProcessing) (void);
    VANILLA_WINDOW_RESULT (*configurePostProcessing) (const VANILLA_PostProcessingCreateInfo* infos, uint16_t count);

    void (*setWorkerCount) (uint16_t workerCount);

    /* Configures the compute pipelines and user buffers. Must be called before start. */
    VANILLA_WINDOW_RESULT (*configureCompute) (const VANILLA_ComputeShaderInfo* shaderInfos, uint16_t shaderCount, const VANILLA_UserBufferInfo* bufferInfos, uint16_t bufferCount);

    /* Queues a write to a user buffer. Applied on the next frame; data must stay valid until then. */
    VANILLA_WINDOW_RESULT (*writeUserBuffer) (uint32_t index, uint32_t offset, const void* data, uint32_t size);

    /* Reads from a user buffer. Waits for the GPU to become idle. */
    VANILLA_WINDOW_RESULT (*readUserBuffer) (uint32_t index, uint32_t offset, void* out, uint32_t size);

    /* Light volume: world box + voxel resolution (rounded up to multiples of 8). Before start, once. */
    void (*setLightVolumeBounds) (Vec3f min, Vec3f max, const uint32_t resolution[3]);

    /* Light limits. Before start, once. Directional limit: 1..8 (default 1). */
    void (*setLightLimit) (uint32_t limit);
    void (*setDirectionalLightLimit) (uint32_t limit);

    /* Replaces the embedded light bake shader (SPIR-V, size in bytes). Before start, once. Code is copied. */
    void (*setLightBakeShader) (const uint32_t* code, size_t size);

    VANILLA_WINDOW_RESULT (*createLight) (const VANILLA_LightCreateInfo* createInfo, LightID* outID);
    VANILLA_WINDOW_RESULT (*destroyLight) (LightID id);

    /* Light setters. Invalid IDs / values are ignored. */
    void (*setLightPosition) (LightID id, Vec3f position);
    void (*setLightRange) (LightID id, float range);
    void (*setLightDirection) (LightID id, Vec3f direction);
    void (*setLightCone) (LightID id, float innerCone, float outerCone);
    void (*setLightColor) (LightID id, Vec3f color);
    void (*setLightIntensity) (LightID id, float intensity);
    void (*setLightEnabled) (LightID id, bool enabled);

    /* Constant light added everywhere (default 0.1, 0.1, 0.1). */
    void (*setAmbientLight) (Vec3f color);

    /* Module-level: false = no lighting/shadow resources, nothing runs, scene drawn unlit. Before start, once. */
    void (*setLightingSupport) (bool supported);

    /* Runtime: lighting off = unlit and shadows off; shadows off = lights keep working. */
    void (*setLightingEnabled) (bool enabled);
    void (*setShadowsEnabled) (bool enabled);
    bool (*isLightingEnabled) (void);
    bool (*isShadowsEnabled) (void);

    /* HDR pipelines (PipelineCreateInfo.hdr): tone mapping operator and exposure, runtime. */
    void (*setToneMapping) (VANILLA_ToneMapping mode);   /* default ACES */
    void (*setExposure) (float exposure);                /* > 0, default 1 */
    VANILLA_ToneMapping (*getToneMapping) (void);
    float (*getExposure) (void);

    /* Physical atmosphere (3D, runtime). Changing it rebuilds the atmosphere tables (well under 1 ms). */
    VANILLA_Atmosphere (*getDefaultAtmosphere) (void);
    void (*setAtmosphere) (const VANILLA_Atmosphere* atmosphere);
    VANILLA_Atmosphere (*getAtmosphere) (void);

    /* Sky background (3D, first HDR pipeline, runtime, default on). Fills pixels no entity covers. */
    void (*setSkyVisible) (bool visible);
    bool (*isSkyVisible) (void);

    /* Environment lighting from the sky (3D, runtime, default on): ambient + reflections, sun dimmed
       by the atmosphere. Off = flat setAmbientLight. Intensity scales the sky light (default 1). */
    void (*setEnvironmentLighting) (bool enabled);
    bool (*isEnvironmentLighting) (void);
    void (*setEnvironmentIntensity) (float intensity);
    float (*getEnvironmentIntensity) (void);

    /*
     * Compute skinning (glTF skins). registerSkin: per-vertex joints / weights of a model (vertexCount = the
     * model's vertex count) + one inverse bind matrix per joint. setEntitySkin: deform an entity of that model
     * every frame by jointEntities (the skin's jointCount entities, glTF joint order; usually NO_RENDER entities
     * moved by baked clips). The entity's own transform still places the mesh. Default 64-byte vertex layout
     * (setSkinningShader for another). destroySkin fails while entities still use it.
     */
    VANILLA_WINDOW_RESULT (*registerSkin) (ModelID model, const VANILLA_SkinVertex* vertices, uint32_t vertexCount,
                                           const mat4* inverseBindMatrices, uint32_t jointCount, SkinID* outSkin);
    VANILLA_WINDOW_RESULT (*destroySkin) (SkinID skin);
    VANILLA_WINDOW_RESULT (*setEntitySkin) (EntityID entity, SkinID skin, const EntityID* jointEntities);
    VANILLA_WINDOW_RESULT (*removeEntitySkin) (EntityID entity);
    /* Before start, once (defaults 256 skins, 1024 skinned entities (max 2048), 262144 deformed vertices = 16 MB). 0 skins = off. */
    void (*setSkinningLimits) (uint32_t skins, uint32_t skinnedEntities, uint32_t skinnedVertices);
    void (*setSkinningShader) (const uint32_t* code, size_t size);

    /*
     * Morph targets (glTF mesh.targets / weights). registerMorph: per-target vertex deltas of a model.
     * setEntityMorph: deform an entity of that model by its own weights (start: the morph's default weights);
     * with a skin on the same entity, morphing runs first. Weights are set per entity (setEntityMorphWeights:
     * the first `count` weights, copied); only changed weights re-deform, only non-zero targets cost GPU time.
     * Morphed entities are never GPU-culled. destroyMorph fails while entities still use it.
     */
    VANILLA_WINDOW_RESULT (*registerMorph) (ModelID model, const VANILLA_MorphCreateInfo* info, MorphID* outMorph);
    VANILLA_WINDOW_RESULT (*destroyMorph) (MorphID morph);
    VANILLA_WINDOW_RESULT (*setEntityMorph) (EntityID entity, MorphID morph);
    VANILLA_WINDOW_RESULT (*setEntityMorphWeights) (EntityID entity, const float* weights, uint32_t count);
    VANILLA_WINDOW_RESULT (*getEntityMorphWeights) (EntityID entity, float* outWeights, uint32_t count);
    VANILLA_WINDOW_RESULT (*removeEntityMorph) (EntityID entity);
    /* Before start, once (defaults 256 morphs, 1M vec3 deltas = 12 MB VRAM, 65536 weights). Deformed entities and
       vertices are shared with skinning (setSkinningLimits). 0 morphs = off. */
    void (*setMorphLimits) (uint32_t morphs, uint32_t deltas, uint32_t weights);

    /* GPU culling, runtime (default FRUSTUM). Needs drawIndirectCount (Vulkan 1.2); without it culling stays NONE. */
    void (*setCulling) (VANILLA_Culling mode);
    VANILLA_Culling (*getCulling) (void);

    /* Editor grid (3D, runtime, default hidden). */
    VANILLA_GridInfo (*getDefaultGrid) (void);
    void (*setGrid) (const VANILLA_GridInfo* info);
    VANILLA_GridInfo (*getGrid) (void);
    void (*setGridVisible) (bool visible);
    bool (*isGridVisible) (void);

    /* Anti-aliasing for every pipeline, before or after start. samples: MSAA types only.
       A new MSAA sample count rebuilds pipelines at the next frame (short hitch). */
    void (*setAntiAliasing) (VANILLA_AntiAliasing type, VANILLA_SampleCount samples);
    VANILLA_AntiAliasing (*getAntiAliasing) (void);
    VANILLA_SampleCount (*getAntiAliasingSamples) (void);   /* applied (clamped to the device) */

    /* Shadows (3D only). Before start, once: */
    void (*setShadowMapResolution) (uint32_t resolution);           /* 256..8192, power of two, default 2048 */
    void (*setShadowCascadeCount) (uint32_t count);                 /* 1..4, default 4 */
    void (*setShadowDepthShader) (const uint32_t* code, size_t size); /* replaces the embedded depth shader; copied */
    /* Alpha-tested (glTF MASK) casters: replaces the embedded shadowmask.vert/.frag for a custom vertex layout; copied. */
    void (*setShadowMaskShader) (const uint32_t* vertexCode, size_t vertexSize, const uint32_t* fragmentCode, size_t fragmentSize);

    /* glTF alpha modes: per pipeline draw lists for MASK / BLEND materials (opaque lists hold every entity).
       Before start, once. Defaults 65536 / 16384; beyond them entities are drawn as opaque. */
    void (*setAlphaListCapacity) (uint32_t maskCapacity, uint32_t blendCapacity);
    void (*setLocalShadowLimit) (uint32_t limit);                   /* shadowed point/spot lights at once, 1..8, default 4 */
    void (*setLocalShadowResolution) (uint32_t resolution);         /* per face, 256..4096, power of two, default 1024 */

    /* Shadows, runtime: */
    void (*setShadowDistance) (float distance);                     /* default 100 */
    void (*setShadowFilter) (VANILLA_ShadowFilter filter);          /* default PCSS */
    void (*setLightCastShadows) (LightID id, bool cast);
    void (*setLightSourceSize) (LightID id, float sourceSize);      /* PCSS softness */
} VANILLA_WINDOW_Wrapper;

static inline bool VANILLA_WINDOW_Wrapper_init(VANILLA_WINDOW_Wrapper* w, void* libHandle) {
    if (!w || !libHandle) { return false; }

    *w = (VANILLA_WINDOW_Wrapper){0};

    VANILLA_DLSYM(libHandle, "VANILLA_setAppName", w->setAppName);
    VANILLA_DLSYM(libHandle, "VANILLA_setWindowTitle", w->setWindowTitle);
    VANILLA_DLSYM(libHandle, "VANILLA_setAppVersion", w->setAppVersion);

    VANILLA_DLSYM(libHandle, "VANILLA_setWindowSize", w->setWindowSize);
    VANILLA_DLSYM(libHandle, "VANILLA_setWindowPosition", w->setWindowPosition);

    VANILLA_DLSYM(libHandle, "VANILLA_setCursorPosition", w->setCursorPosition);

    VANILLA_DLSYM(libHandle, "VANILLA_showWindow", w->showWindow);
    VANILLA_DLSYM(libHandle, "VANILLA_hideWindow", w->hideWindow);

    VANILLA_DLSYM(libHandle, "VANILLA_setVertexLimit", w->setVertexLimit);
    VANILLA_DLSYM(libHandle, "VANILLA_setIndexLimit", w->setIndexLimit);

    VANILLA_DLSYM(libHandle, "VANILLA_setMemoryMode", w->setMemoryMode);
    VANILLA_DLSYM(libHandle, "VANILLA_setVertexLayout", w->setVertexLayout);

    VANILLA_DLSYM(libHandle, "VANILLA_setMaterialLimit", w->setMaterialLimit);
    VANILLA_DLSYM(libHandle, "VANILLA_setTextureLimit", w->setTextureLimit);
    VANILLA_DLSYM(libHandle, "VANILLA_setTextureSizeLimit", w->setTextureSizeLimit);
    VANILLA_DLSYM(libHandle, "VANILLA_setSamplerLimit", w->setSamplerLimit);

    VANILLA_DLSYM(libHandle, "VANILLA_enableVSync", w->enableVSync);
    VANILLA_DLSYM(libHandle, "VANILLA_disableVSync", w->disableVSync);

    VANILLA_DLSYM(libHandle, "VANILLA_setSkyColor", w->setSkyColor);

    VANILLA_DLSYM(libHandle, "VANILLA_setCameraPosition", w->setCameraPosition);
    VANILLA_DLSYM(libHandle, "VANILLA_setCameraUIOffset", w->setCameraUIOffset);
    VANILLA_DLSYM(libHandle, "VANILLA_setCameraTarget", w->setCameraTarget);
    VANILLA_DLSYM(libHandle, "VANILLA_setCameraUp", w->setCameraUp);

    VANILLA_DLSYM(libHandle, "VANILLA_setCameraFOV", w->setCameraFOV);
    VANILLA_DLSYM(libHandle, "VANILLA_setCameraNear", w->setCameraNear);
    VANILLA_DLSYM(libHandle, "VANILLA_setCameraFar", w->setCameraFar);

    VANILLA_DLSYM(libHandle, "VANILLA_setCameraProjection", w->setCameraProjection);

    VANILLA_DLSYM(libHandle, "VANILLA_notifyNewChunkAlloc", w->notifyNewChunkAlloc);
    VANILLA_DLSYM(libHandle, "VANILLA_notifyLastChunkFree", w->notifyLastChunkFree);

    VANILLA_DLSYM(libHandle, "VANILLA_inputDispatch", w->inputDispatch);
    VANILLA_DLSYM(libHandle, "VANILLA_bindInputEvent", w->bindInputEvent);
    VANILLA_DLSYM(libHandle, "VANILLA_unbindInputEvent", w->unbindInputEvent);

    VANILLA_DLSYM(libHandle, "VANILLA_getScrollOffset", w->getScrollOffset);

    VANILLA_DLSYM(libHandle, "VANILLA_createMaterial", w->createMaterial);
    VANILLA_DLSYM(libHandle, "VANILLA_destroyMaterial", w->destroyMaterial);
    VANILLA_DLSYM(libHandle, "VANILLA_setMaterialComponent", w->setMaterialComponent);
    VANILLA_DLSYM(libHandle, "VANILLA_removeMaterialComponent", w->removeMaterialComponent);
    VANILLA_DLSYM(libHandle, "VANILLA_getMaterialComponent", w->getMaterialComponent);
    VANILLA_DLSYM(libHandle, "VANILLA_hasMaterialComponent", w->hasMaterialComponent);

    VANILLA_DLSYM(libHandle, "VANILLA_createSampler", w->createSampler);
    VANILLA_DLSYM(libHandle, "VANILLA_destroySampler", w->destroySampler);

    VANILLA_DLSYM(libHandle, "VANILLA_createTexture", w->createTexture);
    VANILLA_DLSYM(libHandle, "VANILLA_destroyTexture", w->destroyTexture);

    VANILLA_DLSYM(libHandle, "VANILLA_getVersion", w->getVersion);

    VANILLA_DLSYM(libHandle, "VANILLA_getWindowTitle", w->getWindowTitle);
    VANILLA_DLSYM(libHandle, "VANILLA_getWindowSize", w->getWindowSize);
    VANILLA_DLSYM(libHandle, "VANILLA_getWindowPosition", w->getWindowPosition);

    VANILLA_DLSYM(libHandle, "VANILLA_getTextInput", w->getTextInput);

    VANILLA_DLSYM(libHandle, "VANILLA_getCursorPosition", w->getCursorPosition);
    VANILLA_DLSYM(libHandle, "VANILLA_isWindowVisible", w->isWindowVisible);
    VANILLA_DLSYM(libHandle, "VANILLA_getMonitorSize", w->getMonitorSize);

    VANILLA_DLSYM(libHandle, "VANILLA_getVertexLimit", w->getVertexLimit);
    VANILLA_DLSYM(libHandle, "VANILLA_getIndexLimit", w->getIndexLimit);

    VANILLA_DLSYM(libHandle, "VANILLA_getMemoryMode", w->getMemoryMode);
    VANILLA_DLSYM(libHandle, "VANILLA_isVSyncEnabled", w->isVSyncEnabled);

    VANILLA_DLSYM(libHandle, "VANILLA_configurePipelines", w->configurePipelines);
    VANILLA_DLSYM(libHandle, "VANILLA_setPipelineIndex", w->setPipelineIndex);

    VANILLA_DLSYM(libHandle, "VANILLA_enablePostProcessing", w->enablePostProcessing);
    VANILLA_DLSYM(libHandle, "VANILLA_disablePostProcessing", w->disablePostProcessing);
    VANILLA_DLSYM(libHandle, "VANILLA_configurePostProcessing", w->configurePostProcessing);

    VANILLA_DLSYM(libHandle, "VANILLA_setWorkerCount", w->setWorkerCount);

    VANILLA_DLSYM(libHandle, "VANILLA_configureCompute", w->configureCompute);
    VANILLA_DLSYM(libHandle, "VANILLA_writeUserBuffer", w->writeUserBuffer);
    VANILLA_DLSYM(libHandle, "VANILLA_readUserBuffer", w->readUserBuffer);

    VANILLA_DLSYM(libHandle, "VANILLA_setLightVolumeBounds", w->setLightVolumeBounds);
    VANILLA_DLSYM(libHandle, "VANILLA_setLightLimit", w->setLightLimit);
    VANILLA_DLSYM(libHandle, "VANILLA_setDirectionalLightLimit", w->setDirectionalLightLimit);
    VANILLA_DLSYM(libHandle, "VANILLA_setLightBakeShader", w->setLightBakeShader);
    VANILLA_DLSYM(libHandle, "VANILLA_createLight", w->createLight);
    VANILLA_DLSYM(libHandle, "VANILLA_destroyLight", w->destroyLight);
    VANILLA_DLSYM(libHandle, "VANILLA_setLightPosition", w->setLightPosition);
    VANILLA_DLSYM(libHandle, "VANILLA_setLightRange", w->setLightRange);
    VANILLA_DLSYM(libHandle, "VANILLA_setLightDirection", w->setLightDirection);
    VANILLA_DLSYM(libHandle, "VANILLA_setLightCone", w->setLightCone);
    VANILLA_DLSYM(libHandle, "VANILLA_setLightColor", w->setLightColor);
    VANILLA_DLSYM(libHandle, "VANILLA_setLightIntensity", w->setLightIntensity);
    VANILLA_DLSYM(libHandle, "VANILLA_setLightEnabled", w->setLightEnabled);
    VANILLA_DLSYM(libHandle, "VANILLA_setAmbientLight", w->setAmbientLight);
    VANILLA_DLSYM(libHandle, "VANILLA_setLightingSupport", w->setLightingSupport);
    VANILLA_DLSYM(libHandle, "VANILLA_setLightingEnabled", w->setLightingEnabled);
    VANILLA_DLSYM(libHandle, "VANILLA_setShadowsEnabled", w->setShadowsEnabled);
    VANILLA_DLSYM(libHandle, "VANILLA_isLightingEnabled", w->isLightingEnabled);
    VANILLA_DLSYM(libHandle, "VANILLA_isShadowsEnabled", w->isShadowsEnabled);
    VANILLA_DLSYM(libHandle, "VANILLA_setToneMapping", w->setToneMapping);
    VANILLA_DLSYM(libHandle, "VANILLA_setExposure", w->setExposure);
    VANILLA_DLSYM(libHandle, "VANILLA_getToneMapping", w->getToneMapping);
    VANILLA_DLSYM(libHandle, "VANILLA_getExposure", w->getExposure);
    VANILLA_DLSYM(libHandle, "VANILLA_getDefaultAtmosphere", w->getDefaultAtmosphere);
    VANILLA_DLSYM(libHandle, "VANILLA_setAtmosphere", w->setAtmosphere);
    VANILLA_DLSYM(libHandle, "VANILLA_getAtmosphere", w->getAtmosphere);
    VANILLA_DLSYM(libHandle, "VANILLA_setSkyVisible", w->setSkyVisible);
    VANILLA_DLSYM(libHandle, "VANILLA_isSkyVisible", w->isSkyVisible);
    VANILLA_DLSYM(libHandle, "VANILLA_setEnvironmentLighting", w->setEnvironmentLighting);
    VANILLA_DLSYM(libHandle, "VANILLA_isEnvironmentLighting", w->isEnvironmentLighting);
    VANILLA_DLSYM(libHandle, "VANILLA_setEnvironmentIntensity", w->setEnvironmentIntensity);
    VANILLA_DLSYM(libHandle, "VANILLA_getEnvironmentIntensity", w->getEnvironmentIntensity);
    VANILLA_DLSYM(libHandle, "VANILLA_registerSkin", w->registerSkin);
    VANILLA_DLSYM(libHandle, "VANILLA_destroySkin", w->destroySkin);
    VANILLA_DLSYM(libHandle, "VANILLA_setEntitySkin", w->setEntitySkin);
    VANILLA_DLSYM(libHandle, "VANILLA_removeEntitySkin", w->removeEntitySkin);
    VANILLA_DLSYM(libHandle, "VANILLA_setSkinningLimits", w->setSkinningLimits);
    VANILLA_DLSYM(libHandle, "VANILLA_setSkinningShader", w->setSkinningShader);
    VANILLA_DLSYM(libHandle, "VANILLA_registerMorph", w->registerMorph);
    VANILLA_DLSYM(libHandle, "VANILLA_destroyMorph", w->destroyMorph);
    VANILLA_DLSYM(libHandle, "VANILLA_setEntityMorph", w->setEntityMorph);
    VANILLA_DLSYM(libHandle, "VANILLA_setEntityMorphWeights", w->setEntityMorphWeights);
    VANILLA_DLSYM(libHandle, "VANILLA_getEntityMorphWeights", w->getEntityMorphWeights);
    VANILLA_DLSYM(libHandle, "VANILLA_removeEntityMorph", w->removeEntityMorph);
    VANILLA_DLSYM(libHandle, "VANILLA_setMorphLimits", w->setMorphLimits);
    VANILLA_DLSYM(libHandle, "VANILLA_setCulling", w->setCulling);
    VANILLA_DLSYM(libHandle, "VANILLA_getCulling", w->getCulling);
    VANILLA_DLSYM(libHandle, "VANILLA_getDefaultGrid", w->getDefaultGrid);
    VANILLA_DLSYM(libHandle, "VANILLA_setGrid", w->setGrid);
    VANILLA_DLSYM(libHandle, "VANILLA_getGrid", w->getGrid);
    VANILLA_DLSYM(libHandle, "VANILLA_setGridVisible", w->setGridVisible);
    VANILLA_DLSYM(libHandle, "VANILLA_isGridVisible", w->isGridVisible);
    VANILLA_DLSYM(libHandle, "VANILLA_setAntiAliasing", w->setAntiAliasing);
    VANILLA_DLSYM(libHandle, "VANILLA_getAntiAliasing", w->getAntiAliasing);
    VANILLA_DLSYM(libHandle, "VANILLA_getAntiAliasingSamples", w->getAntiAliasingSamples);

    VANILLA_DLSYM(libHandle, "VANILLA_setShadowMapResolution", w->setShadowMapResolution);
    VANILLA_DLSYM(libHandle, "VANILLA_setShadowCascadeCount", w->setShadowCascadeCount);
    VANILLA_DLSYM(libHandle, "VANILLA_setShadowDepthShader", w->setShadowDepthShader);
    VANILLA_DLSYM(libHandle, "VANILLA_setShadowMaskShader", w->setShadowMaskShader);
    VANILLA_DLSYM(libHandle, "VANILLA_setAlphaListCapacity", w->setAlphaListCapacity);
    VANILLA_DLSYM(libHandle, "VANILLA_setLocalShadowLimit", w->setLocalShadowLimit);
    VANILLA_DLSYM(libHandle, "VANILLA_setLocalShadowResolution", w->setLocalShadowResolution);
    VANILLA_DLSYM(libHandle, "VANILLA_setShadowDistance", w->setShadowDistance);
    VANILLA_DLSYM(libHandle, "VANILLA_setShadowFilter", w->setShadowFilter);
    VANILLA_DLSYM(libHandle, "VANILLA_setLightCastShadows", w->setLightCastShadows);
    VANILLA_DLSYM(libHandle, "VANILLA_setLightSourceSize", w->setLightSourceSize);

    return
        w->setAppName != NULL &&
        w->setWindowTitle != NULL &&
        w->setAppVersion != NULL &&

        w->setWindowSize != NULL &&
        w->setWindowPosition != NULL &&
        w->setCursorPosition != NULL &&

        w->showWindow != NULL &&
        w->hideWindow != NULL &&

        w->setVertexLimit != NULL &&
        w->setIndexLimit != NULL &&

        w->setMemoryMode != NULL &&
        w->setVertexLayout != NULL &&

        w->setMaterialLimit != NULL &&
        w->setTextureLimit != NULL &&
        w->setTextureSizeLimit != NULL &&
        w->setSamplerLimit != NULL &&

        w->enableVSync != NULL &&
        w->disableVSync != NULL &&

        w->setSkyColor != NULL &&

        w->setCameraPosition != NULL &&
        w->setCameraUIOffset != NULL &&
        w->setCameraTarget != NULL &&
        w->setCameraUp != NULL &&

        w->setCameraFOV != NULL &&
        w->setCameraNear != NULL &&
        w->setCameraFar != NULL &&
        w->setCameraProjection != NULL &&

        w->notifyNewChunkAlloc != NULL &&
        w->notifyLastChunkFree != NULL &&

        w->inputDispatch != NULL &&
        w->bindInputEvent != NULL &&
        w->unbindInputEvent != NULL &&

        w->getScrollOffset != NULL &&

        w->createMaterial != NULL &&
        w->destroyMaterial != NULL &&
        w->setMaterialComponent != NULL &&
        w->removeMaterialComponent != NULL &&
        w->getMaterialComponent != NULL &&
        w->hasMaterialComponent != NULL &&

        w->createSampler != NULL &&
        w->destroySampler != NULL &&

        w->createTexture != NULL &&
        w->destroyTexture != NULL &&

        w->getVersion != NULL &&

        w->getWindowTitle != NULL &&
        w->getWindowSize != NULL &&
        w->getWindowPosition != NULL &&

        w->getTextInput != NULL &&

        w->getCursorPosition != NULL &&
        w->isWindowVisible != NULL &&
        w->getMonitorSize != NULL &&

        w->getVertexLimit != NULL &&
        w->getIndexLimit != NULL &&

        w->getMemoryMode != NULL &&
        w->isVSyncEnabled != NULL &&

        w->configurePipelines != NULL &&
        w->setPipelineIndex != NULL &&

        w->enablePostProcessing != NULL &&
        w->disablePostProcessing != NULL &&
        w->configurePostProcessing != NULL &&
        
        w->setWorkerCount != NULL &&

        w->configureCompute != NULL &&
        w->writeUserBuffer != NULL &&
        w->readUserBuffer != NULL &&

        w->setLightVolumeBounds != NULL &&
        w->setLightLimit != NULL &&
        w->setDirectionalLightLimit != NULL &&
        w->setLightBakeShader != NULL &&
        w->createLight != NULL &&
        w->destroyLight != NULL &&
        w->setLightPosition != NULL &&
        w->setLightRange != NULL &&
        w->setLightDirection != NULL &&
        w->setLightCone != NULL &&
        w->setLightColor != NULL &&
        w->setLightIntensity != NULL &&
        w->setLightEnabled != NULL &&
        w->setAmbientLight != NULL &&
        w->setLightingSupport != NULL &&
        w->setLightingEnabled != NULL &&
        w->setShadowsEnabled != NULL &&
        w->isLightingEnabled != NULL &&
        w->isShadowsEnabled != NULL &&
        w->setToneMapping != NULL &&
        w->setExposure != NULL &&
        w->getToneMapping != NULL &&
        w->getExposure != NULL &&
        w->getDefaultAtmosphere != NULL &&
        w->setAtmosphere != NULL &&
        w->getAtmosphere != NULL &&
        w->setSkyVisible != NULL &&
        w->isSkyVisible != NULL &&
        w->setEnvironmentLighting != NULL &&
        w->isEnvironmentLighting != NULL &&
        w->setEnvironmentIntensity != NULL &&
        w->getEnvironmentIntensity != NULL &&
        w->registerSkin != NULL &&
        w->destroySkin != NULL &&
        w->setEntitySkin != NULL &&
        w->removeEntitySkin != NULL &&
        w->setSkinningLimits != NULL &&
        w->setSkinningShader != NULL &&
        w->registerMorph != NULL &&
        w->destroyMorph != NULL &&
        w->setEntityMorph != NULL &&
        w->setEntityMorphWeights != NULL &&
        w->getEntityMorphWeights != NULL &&
        w->removeEntityMorph != NULL &&
        w->setMorphLimits != NULL &&
        w->setCulling != NULL &&
        w->getCulling != NULL &&
        w->getDefaultGrid != NULL &&
        w->setGrid != NULL &&
        w->getGrid != NULL &&
        w->setGridVisible != NULL &&
        w->isGridVisible != NULL &&
        w->setAntiAliasing != NULL &&
        w->getAntiAliasing != NULL &&
        w->getAntiAliasingSamples != NULL &&

        w->setShadowMapResolution != NULL &&
        w->setShadowCascadeCount != NULL &&
        w->setShadowDepthShader != NULL &&
        w->setShadowMaskShader != NULL &&
        w->setAlphaListCapacity != NULL &&
        w->setLocalShadowLimit != NULL &&
        w->setLocalShadowResolution != NULL &&
        w->setShadowDistance != NULL &&
        w->setShadowFilter != NULL &&
        w->setLightCastShadows != NULL &&
        w->setLightSourceSize != NULL;
}

static inline void VANILLA_WINDOW_injectWrapper(VANILLA_WINDOW_Wrapper* w, void* behaviourHandle) {
    if (!w || !behaviourHandle) { return; }
    void (*injectWrapper) (VANILLA_WINDOW_Wrapper*) = NULL;
    VANILLA_DLSYM(behaviourHandle, "injectVANILLAWINDOWWrapper", injectWrapper);
    if (injectWrapper) { injectWrapper(w); }
}

#endif // VANILLA_WINDOW_WRAPPER_H
