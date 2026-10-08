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
    VANILLA_SUCCESS = 0,

    VANILLA_ERR_NULL_POINTER,
    VANILLA_ERR_ALLOC_FAILED,
    VANILLA_ERR_ALREADY_STARTED,
    VANILLA_ERR_DIDNT_START,
    VANILLA_ERR_CAPACITY_REACHED,

    VANILLA_ERR_TEXTURE_SIZE_LIMIT,
    VANILLA_ERR_CREATE_TEXTURE,
    VANILLA_ERR_INVALID_TEXTURE,

    VANILLA_ERR_CREATE_SAMPLER,
    VANILLA_ERR_INVALID_SAMPLER,

    VANILLA_ERR_INVALID_MATERIAL,
    VANILLA_ERR_HAS_NO_MATERIAL,
    VANILLA_ERR_ADD_MATERIAL_COMPONENT,
    VANILLA_ERR_REMOVE_MATERIAL_COMPONENT,
    VANILLA_ERR_GET_MATERIAL_COMPONENT
} VANILLA_RESULT;

typedef enum {
    VANILLA_MEMORY_HOST_VISIBLE,
    VANILLA_MEMORY_HYBRID,
    VANILLA_MEMORY_DEVICE_LOCAL
} VANILLA_MemoryMode;

typedef struct {
    float px, py, pz;
    float nx, ny, nz;
    float u, v;
} Vertex;

typedef enum {
    VANILLA_VERTEXFORMAT_R32_FLOAT,
    VANILLA_VERTEXFORMAT_R32G32_FLOAT,
    VANILLA_VERTEXFORMAT_R32G32B32_FLOAT,
    VANILLA_VERTEXFORMAT_R32G32B32A32_FLOAT
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
} VANILLA_CameraPush;

typedef struct { ID32 slot; ID16 generation; } TextureID;
typedef struct { ID32 slot; ID16 generation; } SamplerID;
typedef struct { ID32 slot; ID16 generation; } MaterialID;

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
    VANILLA_MIPMAPFILTER_LINEAR
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

typedef struct {
    TextureID albedo;
    TextureID normal;
    SamplerID sampler;

    uint32_t flags;

    Vec4f baseColor;

    float metallic;
    float roughness;
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
    uint32_t samplerSlot;
    
    uint32_t flags;

    Vec4f baseColor;

    float metallic;
    float roughness;

    uint32_t _padding2;
    uint32_t _padding1;
} VANILLA_GPUMaterial;

typedef enum {
    VANILLA_ENTITYFLAGS1_NO_RENDER    = 1u << 2,
    VANILLA_ENTITYFLAGS1_MAKE_UI      = 1u << 1,
    VANILLA_ENTITYFLAGS1_USE_MATERIAL = 1u << 0
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

    VANILLA_SampleCount samples;
    bool sampleShadingEnable;
    float minSampleShading;
} VANILLA_PipelineCreateInfo;

///

typedef struct {
    const uint32_t* vertexShader;
    size_t vertexShaderSize;

    const uint32_t* fragmentShader;
    size_t fragmentShaderSize;
} VANILLA_PostProcessingCreateInfo;

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

#endif // MAIN_H