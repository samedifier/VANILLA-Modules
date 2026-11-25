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

#ifndef VANILLA_WINDOW_VULKAN_UTILS_H
#define VANILLA_WINDOW_VULKAN_UTILS_H

#include "vanilla_window_vulkan.h"
#include <stdio.h>
#include <string.h>
#include <stddef.h>

uint32_t VANILLA_FindMemoryType(VkPhysicalDevice physicalDevice, uint32_t typeFilter, VkMemoryPropertyFlags properties);
VkDeviceMemory VANILLA_gmmap(const VkMemoryRequirements* requirements, VkMemoryPropertyFlags properties);
void VANILLA_gmunmap(VkDeviceMemory memory);

int VANILLA_VK_createInstance(void);
void VANILLA_VK_destroyInstance(void);

int VANILLA_VK_createSurface(void);
void VANILLA_VK_destroySurface(void);

int VANILLA_VK_pickPhysicalDevice(void);
int VANILLA_VK_createLogicalDevice(void);
void VANILLA_VK_destroyLogicalDevice(void);

int VANILLA_VK_createSwapchain(VkSwapchainKHR oldSwapchain);
int VANILLA_VK_recreateSwapchain(void);
extern bool VANILLA_VK_swapchainOutOfDate;
void VANILLA_VK_destroySwapchain(void);

int VANILLA_VK_createImageViews(void);
void VANILLA_VK_destroyImageViews(void);

int VANILLA_VK_createPipelineLayout(void);
void VANILLA_VK_destroyPipelineLayout(void);

int VANILLA_VK_createPostProcessingPipelineLayout(void);
void VANILLA_VK_destroyPostProcessingPipelineLayout(void);

VkPipelineVertexInputStateCreateInfo VANILLA_makePipelineVertexInput(VkVertexInputBindingDescription* outBindingDesc, VkVertexInputAttributeDescription* outAttrDescs);

int VANILLA_VK_createPipeline(void);
void VANILLA_VK_destroyPipeline(void);

int VANILLA_VK_createPostProcessingPipeline(void);
void VANILLA_VK_destroyPostProcessingPipeline(void);

int  VANILLA_VK_createPipelineImages(void);
void VANILLA_VK_destroyPipelineImages(void);

int VANILLA_VK_createPostProcessingImages(void);
void VANILLA_VK_destroyPostProcessingImages(void);

int VANILLA_VK_createCommandPool(void);
void VANILLA_VK_destroyCommandPool(void);

int VANILLA_VK_createCommandBuffers(void);
int VANILLA_VK_recordCommandBuffer(uint32_t imageIndex, uint32_t activeEntityCount);
void VANILLA_VK_destroyCommandBuffers(void);

VkCommandBuffer VANILLA_VK_beginSingleTimeCommands(void);
int VANILLA_VK_endSingleTimeCommands(VkCommandBuffer commandBuffer);

int VANILLA_VK_createSyncObjects(void);
void VANILLA_VK_destroySyncObjects(void);

int VANILLA_VK_drawFrame(void);

int VANILLA_InitializePersistentBuffers(VkDevice device, VkPhysicalDevice physicalDevice);
void VANILLA_CleanupPersistentBuffers(VkDevice device);
void VANILLA_ExtractAndPushModel(uint32_t modelIndex, void* anycoreModelBuffer, uint32_t vertexCount, void* anycoreIndexBuffer, uint32_t indexCount, uint32_t maxModelIndex, VANILLA_VertexLayout* layout);
void VANILLA_redoModel(uint32_t modelIndex, void* anycoreModelBuffer, uint32_t vertexCount, void* anycoreIndexBuffer, uint32_t indexCount, VANILLA_VertexLayout* layout);

int VANILLA_CreateDescriptorSetLayout(void);
int VANILLA_CreateDescriptor(void);
void VANILLA_CleanupDescriptor(void);

int VANILLA_CreatePostProcessingDescriptorSetLayout(void);
int VANILLA_CreatePostProcessingDescriptor(void);
void VANILLA_UpdatePostProcessingDescriptor(void);
void VANILLA_CleanupPostProcessingDescriptor(void);

int VANILLA_VK_initIndirectBuffer(void);
void VANILLA_VK_cleanupIndirectBuffer(void);
void VANILLA_VK_indirectSet(uint32_t slot, int32_t pipeline, const VkDrawIndexedIndirectCommand* cmd);
void VANILLA_VK_indirectBuildCommand(uint32_t slot, uint32_t modelIndex, VkDrawIndexedIndirectCommand* out);
void VANILLA_VK_indirectRefresh(uint32_t slot);
uint32_t VANILLA_VK_indirectCount(uint16_t pipeline, uint32_t mode);       /* mode: VANILLA_AlphaMode list */
VkDeviceSize VANILLA_VK_indirectOffset(uint16_t pipeline, uint32_t mode);
void VANILLA_VK_indirectSetAlphaMode(uint32_t slot, uint32_t alphaMode);
void VANILLA_VK_recordIndirectUpload(VkCommandBuffer commandBuffer);

int VANILLA_VK_createDepthResources(void);
void VANILLA_VK_destroyDepthResources(void);

///

int createDefaultTexture(void);
int VANILLA_VK_textureinit(void);
void VANILLA_VK_texturefree(void);

int createDefaultSampler(void);
int VANILLA_initSamplers(void);
void VANILLA_destroySamplers(void);

//

int VANILLA_VK_createMaterialBuffer(void);
void VANILLA_VK_destroyMaterialBuffer(void);

//

int VANILLA_VK_createAliveFlagsBuffer(void);
void VANILLA_VK_destroyAliveFlagsBuffer(void);

#define VANILLA_DIRECTIONAL_LIGHT_MAX_LIMIT 8
#define VANILLA_SHADOW_MAX_CASCADES         4
#define VANILLA_LOCAL_SHADOW_MAX            8   /* shadowed point / spot lights */
#define VANILLA_LOCAL_SHADOW_FACES          6   /* layers reserved per local light */

/* GPULight.valid: bit 0 = valid, bits 1+ = local shadow index + 1 (0 = baked, no shadow) */
#define VANILLA_LIGHT_VALID_BIT        1u
#define VANILLA_LIGHT_SHADOW_SHIFT     1u

/*
 * std430 mirror of LightingInfo in lightbake.comp / sceneshader.frag.
 * The bake shader only declares the first part (up to directional[]).
 */
typedef struct {
    Vec3f    volumeMin;     uint32_t lightCount;
    Vec3f    volumeMax;     uint32_t directionalCount;
    uint32_t resolution[3]; uint32_t dimension;
    Vec3f    ambient;       uint32_t flags;   /* VANILLA_LIGHTINGFLAG_* (0 = unlit, buffer starts zeroed) */
    VANILLA_GPULight directional[VANILLA_DIRECTIONAL_LIGHT_MAX_LIMIT];

    /* Shadows (directional, cascaded) */
    mat4     cascadeViewProj[VANILLA_SHADOW_MAX_CASCADES];
    float    cascadeSplits[VANILLA_SHADOW_MAX_CASCADES];  /* view depth where each cascade ends */
    uint32_t shadowDirectional;                           /* index into directional[], 0xFFFFFFFF = none */
    uint32_t cascadeCount;
    uint32_t shadowFilter;                                /* VANILLA_ShadowFilter */
    float    shadowTexel;                                 /* 1 / shadow map resolution */
    float    cascadeRadius[VANILLA_SHADOW_MAX_CASCADES];  /* world radius of each cascade */
    Vec3f    cameraForward; float shadowDistance;
    float    lightSize;                                   /* tan(sourceSize / 2) of the shadow light */
    float    casterMargin;                                /* world distance kept in front of each cascade */
    float    blendRange;                                  /* fraction of a cascade blended into the next */
    float    _padding2;

    /* Shadowed point / spot lights (not baked, evaluated per pixel) */
    uint32_t localShadowCount;
    uint32_t _padding3[3];
    VANILLA_GPULight localLights[VANILLA_LOCAL_SHADOW_MAX];
    float    localShadowParams[VANILLA_LOCAL_SHADOW_MAX][4];  /* near, far, tan(fov/2), first layer */
    float    localShadowExtra[VANILLA_LOCAL_SHADOW_MAX][4];   /* sourceSize (world radius), face count, -, - */
    mat4     localShadowMatrices[VANILLA_LOCAL_SHADOW_MAX * VANILLA_LOCAL_SHADOW_FACES];

    /* Environment lighting, CPU part (VANILLA_VK_fillEnvironmentInfo) */
    float    envIntensity;
    float    envMaxLod;                                   /* last mip of the prefiltered cube */
    uint32_t sunIndex;                                    /* index into directional[], 0xFFFFFFFF = none */
    float    aerialKmPerUnit;                             /* world unit -> km of air (aerial perspective) */
    uint32_t _paddingAlign[20];                           /* GPU part starts on a 256-byte boundary */

    /*
     * Environment lighting, GPU part: written by env_sh.comp (bound with an offset).
     * The CPU upload stops at VANILLA_LIGHTINGINFO_CPU_SIZE and never touches it.
     */
    float    envSH[9][4];                                 /* L2 SH of irradiance / PI (rgb, -) */
    float    sunTransmittance[4];
} VANILLA_GPULightingInfo;

#define VANILLA_LIGHTINGINFO_CPU_SIZE   offsetof(VANILLA_GPULightingInfo, envSH)
#define VANILLA_LIGHTINGINFO_GPU_SIZE   (sizeof(VANILLA_GPULightingInfo) - VANILLA_LIGHTINGINFO_CPU_SIZE)

_Static_assert(offsetof(VANILLA_GPULightingInfo, envSH) % 256 == 0,
               "LightingInfo GPU part must start at a 256-byte boundary (storage buffer offset alignment)");

#define VANILLA_LIGHTINGFLAG_LIGHTING    1u
#define VANILLA_LIGHTINGFLAG_SHADOWS     2u
#define VANILLA_LIGHTINGFLAG_ENVIRONMENT 4u
#define VANILLA_LIGHTINGFLAG_AERIAL      8u

bool VANILLA_VK_isLightingSupported(void);
void VANILLA_VK_clearShadowPasses(void);

int  VANILLA_VK_initHdr(void);
void VANILLA_VK_destroyHdr(void);
bool VANILLA_VK_isHdrPipeline(uint16_t pipeline);
VkFormat VANILLA_VK_pipelineColorFormat(uint16_t pipeline);
int32_t VANILLA_VK_firstHdrPipeline(void);
VkImage VANILLA_VK_hdrImage(uint16_t pipeline);
VkImageView VANILLA_VK_hdrView(uint16_t pipeline);
void VANILLA_VK_hdrBarrier(VkCommandBuffer commandBuffer, uint16_t pipeline, bool toAttachment);
void VANILLA_VK_recordToneMap(VkCommandBuffer commandBuffer, uint16_t pipeline, bool direct);
void VANILLA_VK_recordFxaa(VkCommandBuffer commandBuffer, uint16_t pipeline, bool direct);
VkImage VANILLA_VK_fxaaInputImage(uint16_t pipeline);
VkImageView VANILLA_VK_fxaaInputView(uint16_t pipeline);
void VANILLA_VK_colorBarrier(VkCommandBuffer commandBuffer, VkImage image, bool toAttachment);
void VANILLA_VK_beginFullscreen(VkCommandBuffer commandBuffer, VkImageView view);

void VANILLA_VK_initAntiAliasing(void);
int  VANILLA_VK_applyAntiAliasingChanges(void);
int  VANILLA_VK_createMsaaTargets(void);
void VANILLA_VK_destroyMsaaTargets(void);
VkSampleCountFlagBits VANILLA_VK_msaaSamples(void);
bool VANILLA_VK_fxaaEnabled(void);
VkImageView VANILLA_VK_msaaDepthView(void);
VkImage VANILLA_VK_msaaDepthImage(void);
VkImage VANILLA_VK_msaaSwapchainImage(void);
VkImageView VANILLA_VK_msaaSwapchainView(void);
VkImage VANILLA_VK_msaaColorImage(uint16_t pipeline);
VkImageView VANILLA_VK_msaaColorView(uint16_t pipeline);

int VANILLA_VK_initLightning(void);
void VANILLA_VK_destroyLightning(void);
void VANILLA_VK_recordLighting(VkCommandBuffer commandBuffer);
VkBuffer VANILLA_VK_getLightBuffer(void);
VkBuffer VANILLA_VK_getLightingInfoBuffer(void);
void VANILLA_VK_getLightVolumeImageInfos(VkDescriptorImageInfo out[3]);
bool VANILLA_VK_getShadowLight(VANILLA_GPULight* outLight, float* outSourceSize);

int  VANILLA_VK_initShadows(void);
void VANILLA_VK_destroyShadows(void);
void VANILLA_VK_fillShadowInfo(VANILLA_GPULightingInfo* info, const VANILLA_GPULight* shadowLight, uint32_t shadowDirectional, float sourceSize);
void VANILLA_VK_recordShadowPass(VkCommandBuffer commandBuffer);
void VANILLA_VK_getShadowImageInfos(VkDescriptorImageInfo* outCompare, VkDescriptorImageInfo* outDepth);
void VANILLA_VK_fillLocalShadowInfo(VANILLA_GPULightingInfo* info);
void VANILLA_VK_getLocalShadowImageInfos(VkDescriptorImageInfo* outCompare, VkDescriptorImageInfo* outDepth);
uint32_t VANILLA_VK_getLocalShadowLimit(void);

bool VANILLA_VK_getSunLight(VANILLA_GPULight* outLight, float* outSourceSize);

/* std430 mirror of AtmosphereParams in shaders/atmosphere.glsl. Lengths in km. */
typedef struct {
    Vec3f rayleighScattering; float rayleighScaleHeight;
    Vec3f mieScattering;      float mieScaleHeight;
    Vec3f mieExtinction;      float mieAnisotropy;
    Vec3f ozoneAbsorption;    float bottomRadius;
    Vec3f groundAlbedo;       float topRadius;
    Vec3f sunDirection;       float cameraHeight;     /* toward the sun; from the planet center */
    Vec3f sunIlluminance;     float sunCosHalfAngle;
    Vec3f nightColor;         float metersPerUnit;
    float groundHeight;       float aerialScale;      float _padding[2];
} VANILLA_GPUAtmosphere;

void VANILLA_VK_markLightingInfoDirty(void);
void VANILLA_VK_fillEnvironmentInfo(VANILLA_GPULightingInfo* info, uint32_t sunIndex);
void VANILLA_VK_getEnvironmentImageInfos(VkDescriptorImageInfo* outSpecular, VkDescriptorImageInfo* outBrdf, VkDescriptorImageInfo* outAerial);

int  VANILLA_VK_initEnvironment(void);
void VANILLA_VK_destroyEnvironment(void);
void VANILLA_VK_recordEnvironment(VkCommandBuffer commandBuffer);
int  VANILLA_VK_createSkyPipeline(void);
void VANILLA_VK_destroySkyPipeline(void);
void VANILLA_VK_recordSky(VkCommandBuffer commandBuffer, uint16_t pipelineIndex);

/* Compute skinning (vanilla_window_vulkan_skin.c) */
extern uint32_t skinnedVertexLimit;   /* deformed vertices, stored above vertexLimit in the global vertex buffer */
int      VANILLA_VK_initSkinning(void);
void     VANILLA_VK_destroySkinning(void);
void     VANILLA_VK_recordSkinning(VkCommandBuffer commandBuffer);
uint32_t VANILLA_VK_skinnedVertexOffset(uint32_t slot);   /* UINT32_MAX = not skinned */

/* GPU frustum culling (vanilla_window_vulkan_cull.c) */
int  VANILLA_VK_initCulling(void);
void VANILLA_VK_destroyCulling(void);
void VANILLA_VK_cullSetModelBounds(uint32_t model, const void* anycoreVertices, uint32_t vertexCount);
void VANILLA_VK_cullSetSlotModel(uint32_t slot, uint32_t model);
void VANILLA_VK_recordCullUploads(VkCommandBuffer commandBuffer);
void VANILLA_VK_cullPass(VkCommandBuffer commandBuffer, const mat4* viewProj, bool shadowPass);
void VANILLA_VK_recreateHiZ(void);                                        /* swapchain size / MSAA changed */
bool VANILLA_VK_hiZWanted(void);                                          /* occlusion on: keep the scene depth */
void VANILLA_VK_buildHiZ(VkCommandBuffer commandBuffer, const mat4* viewProj);   /* after the scene's solids */
void VANILLA_VK_drawList(VkCommandBuffer commandBuffer, uint16_t pipeline, uint32_t mode);   /* pipeline bound */
VkDeviceSize VANILLA_VK_indirectBufferSize(void);

int  VANILLA_VK_initGrid(void);
void VANILLA_VK_destroyGrid(void);
int  VANILLA_VK_createGridPipeline(void);
void VANILLA_VK_destroyGridPipeline(void);
void VANILLA_VK_recordGrid(VkCommandBuffer commandBuffer, uint16_t pipelineIndex, const VANILLA_CameraPush* campush);

#define VANILLA_USER_BUFFER_LIMIT 64

/*
 * Shared binding numbers of the graphics and compute sets.
 * Module resources first, user buffers last.
 */
#define VANILLA_BINDING_ALIVE_FLAGS   7
#define VANILLA_BINDING_LIGHTS        8   /* Light[]                    */
#define VANILLA_BINDING_LIGHTING_INFO 9   /* LightingInfo               */
#define VANILLA_BINDING_LIGHT_VOLUME  10  /* sampler3D lightVolume[3]   */
#define VANILLA_BINDING_SHADOW_MAP    11  /* sampler2DArrayShadow       */
#define VANILLA_BINDING_SHADOW_DEPTH  12  /* sampler2DArray (raw depth) */
#define VANILLA_BINDING_LOCAL_SHADOW_MAP   13  /* sampler2DArrayShadow, point/spot */
#define VANILLA_BINDING_LOCAL_SHADOW_DEPTH 14  /* sampler2DArray, point/spot raw   */
#define VANILLA_BINDING_ENV_SPECULAR  15  /* samplerCube, prefiltered environment */
#define VANILLA_BINDING_BRDF_LUT      16  /* sampler2D, split-sum BRDF            */
#define VANILLA_BINDING_AERIAL        17  /* sampler3D, aerial perspective froxels */
#define VANILLA_BINDING_USER_BUFFERS  18  /* user buffer i = 18 + i     */

int VANILLA_VK_createComputeBuffers(void);
void VANILLA_VK_destroyComputeBuffers(void);
void VANILLA_VK_recordUserBufferWrites(VkCommandBuffer commandBuffer);
void VANILLA_VK_recordComputeDispatches(VkCommandBuffer commandBuffer);

int VANILLA_VK_createComputeDescriptorSetLayout(void);
int VANILLA_VK_createComputeDescriptor(void);
void VANILLA_VK_destroyComputeDescriptor(void);

int VANILLA_VK_createComputePipelineLayout(void);
int VANILLA_VK_createComputePipelines(void);
void VANILLA_VK_destroyComputePipelines(void);

static inline int VANILLA_VK_init() {
    if (VANILLA_VK_createInstance()) { return 1; }
    if (VANILLA_VK_createSurface()) { return 1; }
    if (VANILLA_VK_pickPhysicalDevice()) { return 1; }
    if (VANILLA_VK_createLogicalDevice()) { return 1; }

    if (VANILLA_VK_textureinit()) { return 1; }
    if (VANILLA_initSamplers()) { return 1; }

    if (VANILLA_VK_createMaterialBuffer()) { return 1; }
    if (VANILLA_VK_createSwapchain(VK_NULL_HANDLE)) { return 1; }
    if (VANILLA_VK_createImageViews()) { return 1; }
    if (VANILLA_InitializePersistentBuffers(vulkan.device, vulkan.physicalDevice)) { return 1; }

    if (VANILLA_CreateDescriptorSetLayout()) { return 1; }

    if (VANILLA_VK_createPipelineLayout()) { return 1; }
    VANILLA_VK_initAntiAliasing();
    if (VANILLA_VK_createPipeline()) { return 1; }
    if (VANILLA_VK_initHdr()) { return 1; }
    if (VANILLA_VK_createMsaaTargets()) { return 1; }

    if (VANILLA_VK_createCommandPool()) { return 1; }      /* before depth: it records a layout transition */
    if (VANILLA_VK_createDepthResources()) { return 1; }
    if (VANILLA_VK_initIndirectBuffer()) { return 1; }
    if (VANILLA_VK_initSkinning()) { return 1; }
    if (VANILLA_VK_initCulling()) { return 1; }
    if (VANILLA_VK_createAliveFlagsBuffer()) { return 1; }
    if (VANILLA_VK_initLightning()) { return 1; }
    if (VANILLA_VK_initShadows()) { return 1; }
    if (VANILLA_VK_initEnvironment()) { return 1; }
    if (VANILLA_VK_initGrid()) { return 1; }
    if (VANILLA_VK_createCommandBuffers()) { return 1; }

    if (createDefaultTexture() != 0) {
        printf("[VANILLA WINDOW] default texture create error\n");
        return 1;
    }
    if (createDefaultSampler() != 0) {
        printf("[VANILLA WINDOW] default sampler create error\n");
        return 1;
    }

    if (VANILLA_VK_createSyncObjects()) { return 1; }

    if (vulkan.computePipelineCount > 0) {
        if (VANILLA_VK_createComputeBuffers()) { return 1; }

        if (VANILLA_VK_createComputeDescriptorSetLayout()) { return 1; }

        if (VANILLA_VK_createComputePipelineLayout()) { return 1; }
        if (VANILLA_VK_createComputePipelines()) { return 1; }
    }

    if (enablePostProcessing) {
        if (VANILLA_VK_createPipelineImages()) { return 1; }
        if (VANILLA_VK_createPostProcessingImages()) { return 1; }

        if (VANILLA_CreatePostProcessingDescriptorSetLayout()) { return 1; }

        if (vulkan.postProcessingDescriptorSetLayout == VK_NULL_HANDLE) {
            return 1;
        }

        if (VANILLA_VK_createPostProcessingPipelineLayout()) { return 1; }
        if (VANILLA_VK_createPostProcessingPipeline()) { return 1; }
    }

    return 0;
}
    
#endif // VANILLA_WINDOW_VULKAN_UTILS_H