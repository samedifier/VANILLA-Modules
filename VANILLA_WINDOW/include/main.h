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

#ifndef MAIN_H
#define MAIN_H

#include "configuration.h"
#include "primitives.h"
#include "standardcomponent.h"

#include <vulkan/vulkan.h>

#define VANILLA_PI 3.14159265358979323846

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

typedef struct {
    float px, py, pz;
    float nx, ny, nz;
    float u, v;
    float tx, ty, tz, tw; /* tangent, tw = bitangent sign (+1 / -1) */

    /* glTF 2.0 (appended: older 12-value initializers leave them zero) */
    float    u1, v1;      /* TEXCOORD_1 */
    uint32_t color;       /* COLOR_0, RGBA8 (R in the lowest byte); used when the material has vertexColor */
    uint32_t _padding;    /* 64 bytes */
} Vertex;

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

typedef struct {
    uint32_t firstVertex;
    uint32_t vertexCount;
    uint32_t firstIndex;
    uint32_t indexCount;

    bool inuse;
} ModelLookup;

typedef struct {
    mat4 viewProjection;
    float aspectRatio;
    float uioffset[2];
    float _padding1;
    float cameraPosition[3];
    float _padding2;
} VANILLA_CameraPush;

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
    Vec3f position;
    Vec3f direction;
    Vec3f color;
    float intensity;
    float range;
    float innerCone; /* cosine, innerCone > outerCone */
    float outerCone;
    bool  castShadows; /* directional: one light; point/spot: up to setLocalShadowLimit (beyond it: baked, no shadow) */
    float sourceSize;  /* PCSS: directional = angular diameter (radians, sun ~0.0093); point/spot = radius (world units) */
} VANILLA_LightCreateInfo;

typedef enum {
    VANILLA_SHADOWFILTER_PCF,  /* fixed-width soft edge */
    VANILLA_SHADOWFILTER_PCSS  /* contact-hardening: sharp near the caster, softer farther away */
} VANILLA_ShadowFilter;

/* std430 mirror of the shader's Light struct (64 bytes) */
typedef struct {
    Vec3f    position;  float    range;
    Vec3f    direction; float    intensity;
    Vec3f    color;     uint32_t type;
    float    innerCone; float    outerCone;
    uint32_t enabled;   uint32_t valid;
} VANILLA_GPULight;

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

typedef struct {
    VkImage image;
    VkDeviceMemory memory;
    VkImageView imageView;

    uint32_t width;
    uint32_t height;

    VANILLA_TextureFormat format;

    uint32_t mipLevels;
    
    bool isValid;
} VANILLA_Texture;

typedef struct {
    uint32_t albedoSlot;
    uint32_t normalSlot;
    uint32_t metallicRoughnessSlot;
    uint32_t occlusionSlot;

    uint32_t emissiveSlot;
    uint32_t samplerSlot;

    uint32_t _padding1;
    uint32_t _padding2;

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

    /* glTF 2.0 (mirror in the scene shaders' Material struct) */
    uint32_t samplerSlots[VANILLA_MATERIAL_TEXTURE_COUNT];   /* per texture */
    uint32_t texCoordBits;                                   /* bit i: texture i samples UV1 */
    uint32_t alphaMode;                                      /* VANILLA_AlphaMode */
    uint32_t _padding3;
    Vec4f    uvTransforms[VANILLA_MATERIAL_TEXTURE_COUNT * 2]; /* per texture 2x3 rows: (m00, m01, tx, -), (m10, m11, ty, -) */
} VANILLA_GPUMaterial;

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

typedef struct {
    uint32_t materialSlot;
    uint32_t flags1;
    uint32_t flags2;
    uint32_t flags3;
} VANILLA_GPUMaterialSlot;

typedef enum {
    VANILLA_PROJECTION_PERSPECTIVE,
    VANILLA_PROJECTION_ORTHOGRAPHIC
} VANILLA_ProjectionType;

typedef enum {
    VANILLA_CURSORMODE_NORMAL,
    VANILLA_CURSORMODE_HIDDEN,
    VANILLA_CURSORMODE_DISABLED
} VANILLA_CursorMode;

///

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
 * Physical atmosphere (3D only). Start from VANILLA_getDefaultAtmosphere() (Earth) and change fields.
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

///

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

extern uint32_t ANYCOREBuildID;
extern ANYCORE_Stream* stream;
extern bool didstart;

extern char* applicationName;
extern char* windowTitle;
extern bool didWindowTitleSet;

extern uint32_t majorVersion;
extern uint32_t minorVersion;
extern uint32_t patchVersion;

extern unsigned int windowWidth;
extern unsigned int windowHeight;
extern bool didWindowSizeSet;

extern unsigned int windowPositionX;
extern unsigned int windowPositionY;
extern bool didWindowPositionSet;

extern unsigned int cursorPositionX;
extern unsigned int cursorPositionY;
extern bool didCursorPositionSet;

extern VANILLA_CursorMode cursorMode;
extern bool didCursowModeSet;

extern bool showWindow;
extern bool didShowWindowSet;

extern bool enableVSync;
extern bool didVSyncSet;

extern VANILLA_PipelineCreateInfo* pipelineInfos;

extern VANILLA_PostProcessingCreateInfo* postProcessingInfos;
extern bool enablePostProcessing;

extern uint32_t stride;
extern uint64_t vertexLimit;
extern uint64_t indexLimit;

extern uint32_t textureLimit;
extern uint64_t textureSizeLimit;
extern uint32_t samplerLimit;

extern uint32_t materialLimit;

extern VANILLA_VertexLayout vertexLayout;
extern VANILLA_MemoryMode memoryMode;
extern VANILLA_ProjectionType projectionType;

extern float orthographicSize;

extern StandardComponent materialComponent;

extern uint32_t eventFunctionLimit;

extern double scrollOffsetX;
extern double scrollOffsetY;

extern pthread_t* VANILLA_workers;
extern uint16_t* VANILLA_workerIndices;
extern uint16_t VANILLA_workerCount;

extern uint32_t lightLimit;

#endif // MAIN_H