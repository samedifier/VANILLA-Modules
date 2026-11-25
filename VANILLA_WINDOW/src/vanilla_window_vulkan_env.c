/*
 * VANILLA Modules
 * Copyright (C) 2026 Samed
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License, version 2 only.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 *
 * See the LICENSE file for the full license text.
 */

/*
 * Environment: physical atmosphere (Hillaire 2020).
 *
 * Three small LUTs, built by embedded compute shaders for a sun illuminance of 1:
 *   transmittance  256 x 64   rebuilt when the atmosphere parameters change
 *   multi-scatter   32 x 32   rebuilt when the atmosphere parameters change
 *   sky-view       192 x 108  rebuilt when the sun direction or the camera height changes
 * Images stay in GENERAL. 3D builds with lighting support only; otherwise nothing is created.
 *
 * Environment lighting (IBL), rebuilt when the sun / atmosphere changes or the camera height moves
 * more than VANILLA_IBL_HEIGHT_STEP (not every frame while flying around):
 *   env cube        128^2 x 6  sky-view + lit ground + night floor (no sun disc)
 *   SH + sun        L2 SH of irradiance / PI and the sun's transmittance -> LightingInfo GPU part
 *   prefiltered     128^2 x 6, 6 mips, GGX per roughness -> binding 15
 *   BRDF LUT        128^2, built once -> binding 16
 *   aerial          32^3 froxels (camera cells x depth slices to 32 km) -> binding 17, rebuilt when the
 *                   camera or the sun moves; perspective cameras only, off when aerialPerspectiveScale = 0
 * 2D / no lighting support: 1x1 placeholders keep bindings 15/16/17 valid, nothing runs.
 *
 * Sky pass: a fullscreen triangle at depth 1 drawn into the first HDR pipeline's image right after
 * its entities (depth test LESS_OR_EQUAL, no depth write), so only empty pixels are shaded.
 * Its pipeline follows the MSAA sample count (rebuilt by VANILLA_VK_applyAntiAliasingChanges).
 */

#include "vanilla_window_vulkan_utils.h"
#include "vanilla_window_atmosphere_spv.h"
#include "vanilla_window_sky_spv.h"
#include "vanilla_window_camera.h"
#include <stdint.h>
#include <stdbool.h>
#include <math.h>

#define VANILLA_TRANSMITTANCE_W  256
#define VANILLA_TRANSMITTANCE_H  64
#define VANILLA_MULTISCATTER_N   32
#define VANILLA_SKYVIEW_W        192
#define VANILLA_SKYVIEW_H        108

#define VANILLA_DEFAULT_SUN_SIZE 0.00930f   /* angular diameter (radians) when the light's sourceSize is 0 */

#define VANILLA_ENV_CUBE_SIZE        128   /* must match ENV_CUBE_SIZE in env.glsl */
#define VANILLA_ENV_PREFILTER_MIPS   6     /* must match ENV_PREFILTER_MIPS in env.glsl */
#define VANILLA_BRDF_LUT_SIZE        128   /* must match LUT_SIZE in brdf_lut.comp */
#define VANILLA_IBL_HEIGHT_STEP      0.1f  /* km of camera height change before the IBL is rebuilt */
#define VANILLA_AERIAL_SIZE          32    /* must match AERIAL_SIZE in atmosphere.glsl */

enum {
    VANILLA_ENV_PIPE_CUBE,
    VANILLA_ENV_PIPE_SH,
    VANILLA_ENV_PIPE_PREFILTER,
    VANILLA_ENV_PIPE_BRDF,
    VANILLA_ENV_PIPE_AERIAL,
    VANILLA_ENV_PIPE_COUNT
};

enum {
    VANILLA_ENV_LUT_TRANSMITTANCE,
    VANILLA_ENV_LUT_MULTISCATTER,
    VANILLA_ENV_LUT_SKYVIEW,
    VANILLA_ENV_LUT_COUNT
};

/* --------------------------------------------------------- */
/* Parameters                                                 */
/* --------------------------------------------------------- */

static VANILLA_Atmosphere VANILLA_VK_defaultAtmosphere(void) {
    return (VANILLA_Atmosphere){
        .metersPerUnit       = 1.0f,
        .groundHeight        = 0.0f,
        .planetRadius        = 6360.0f,
        .atmosphereHeight    = 100.0f,
        .rayleighScattering  = { 0.005802f, 0.013558f, 0.033100f },
        .rayleighScaleHeight = 8.0f,
        .mieScattering       = 0.003996f,
        .mieAbsorption       = 0.000444f,
        .mieScaleHeight      = 1.2f,
        .mieAnisotropy       = 0.8f,
        .ozoneAbsorption     = { 0.000650f, 0.001881f, 0.000085f },
        .groundAlbedo        = { 0.3f, 0.3f, 0.3f },
        .nightColor          = { 0.004f, 0.006f, 0.012f },
        .aerialPerspectiveScale = 1.0f
    };
}

static VANILLA_Atmosphere atmosphere;
static bool               atmosphereSet   = false;   /* false: defaults on first use */
static bool               lutsDirty       = true;    /* transmittance + multi-scatter */

static VANILLA_GPUAtmosphere lastUploaded;
static bool                  hasUploaded = false;

/* --------------------------------------------------------- */
/* GPU resources                                              */
/* --------------------------------------------------------- */

static bool environmentActive = false;

static VkBuffer       atmosphereBuffer = VK_NULL_HANDLE;
static VkDeviceMemory atmosphereMemory = VK_NULL_HANDLE;

static VkImage        lutImages[VANILLA_ENV_LUT_COUNT]   = {0};
static VkDeviceMemory lutMemories[VANILLA_ENV_LUT_COUNT] = {0};
static VkImageView    lutViews[VANILLA_ENV_LUT_COUNT]    = {0};
static VkSampler      lutSampler   = VK_NULL_HANDLE;   /* linear, clamp */
static VkSampler      wrapUSampler = VK_NULL_HANDLE;   /* linear, U repeat (sky-view azimuth) */

static VkDescriptorSetLayout atmoSetLayout      = VK_NULL_HANDLE;
static VkDescriptorPool      atmoDescriptorPool = VK_NULL_HANDLE;
static VkDescriptorSet       atmoDescriptorSet  = VK_NULL_HANDLE;
static VkPipelineLayout      atmoPipelineLayout = VK_NULL_HANDLE;
static VkPipeline            atmoPipelines[VANILLA_ENV_LUT_COUNT] = {0};

static bool                  skyVisible         = true;
static VkDescriptorSetLayout skySetLayout       = VK_NULL_HANDLE;
static VkDescriptorPool      skyDescriptorPool  = VK_NULL_HANDLE;
static VkDescriptorSet       skyDescriptorSet   = VK_NULL_HANDLE;
static VkPipelineLayout      skyPipelineLayout  = VK_NULL_HANDLE;
static VkPipeline            skyPipeline        = VK_NULL_HANDLE;

/* std430 mirror of SkyPush in sky.frag */
typedef struct {
    float forward[4];   /* w: 1 perspective, 0 orthographic */
    float right[4];     /* scaled by tan(fov/2) * aspect */
    float up[4];        /* scaled by tan(fov/2) */
} VANILLA_SkyPush;

static const uint32_t lutSizes[VANILLA_ENV_LUT_COUNT][2] = {
    { VANILLA_TRANSMITTANCE_W, VANILLA_TRANSMITTANCE_H },
    { VANILLA_MULTISCATTER_N,  VANILLA_MULTISCATTER_N  },
    { VANILLA_SKYVIEW_W,       VANILLA_SKYVIEW_H       }
};

/* Environment lighting (IBL) */
static bool  environmentLighting = true;   /* API switch */
static float environmentIntensity = 1.0f;
static bool  iblDirty      = true;
static bool  iblBuilt      = false;        /* SH / prefilter hold valid data */
static bool  brdfBuilt     = false;
static VANILLA_GPUAtmosphere lastIblAtmosphere;

typedef struct {
    VkImage        image;
    VkDeviceMemory memory;
    VkImageView    sampledView;   /* cube (or 2D for the LUT) */
    VkImageView    storageView;   /* 2D array, mip 0 (2D for the LUT) */
} VANILLA_EnvImage;

static VANILLA_EnvImage envCube;
static VANILLA_EnvImage prefilterCube;
static VkImageView      prefilterMipViews[VANILLA_ENV_PREFILTER_MIPS] = {0};
static uint32_t         prefilterMipCount = 1;
static VANILLA_EnvImage brdfLut;
static VANILLA_EnvImage aerialVolume;                  /* 3D: sampledView = storageView's image, 3D view */
static VkSampler        envSampler = VK_NULL_HANDLE;   /* trilinear, clamp, all mips */

static bool  aerialActive = false;       /* this frame: perspective camera and scale > 0 */
static bool  aerialBuilt  = false;
static float lastAerialPush[12];
static bool  lastPerspective = true;

static VkDescriptorSetLayout mipSetLayout      = VK_NULL_HANDLE;
static VkDescriptorPool      mipDescriptorPool = VK_NULL_HANDLE;
static VkDescriptorSet       mipSets[VANILLA_ENV_PREFILTER_MIPS] = {0};
static VkPipelineLayout      envPipelineLayout = VK_NULL_HANDLE;
static VkPipeline            envPipelines[VANILLA_ENV_PIPE_COUNT] = {0};

typedef struct {
    float roughness;
    float size;
    float _padding[2];
} VANILLA_PrefilterPush;

static int VANILLA_VK_createEnvView(VkImage image, VkImageViewType type, uint32_t baseMip, uint32_t mipCount, uint32_t layers, VkImageView* outView) {
    VkImageViewCreateInfo viewInfo = {
        .sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO,
        .image = image,
        .viewType = type,
        .format = VK_FORMAT_R16G16B16A16_SFLOAT,
        .subresourceRange = { VK_IMAGE_ASPECT_COLOR_BIT, baseMip, mipCount, 0, layers }
    };
    if (vkCreateImageView(vulkan.device, &viewInfo, NULL, outView) != VK_SUCCESS) {
        *outView = VK_NULL_HANDLE;
        return 1;
    }
    return 0;
}

/* RGBA16F image; cube = 6 layers, cube compatible. Views: sampled (cube / 2D, all mips), storage (mip 0). */
static int VANILLA_VK_createIblImage(uint32_t size, uint32_t mips, bool cube, VkImageUsageFlags usage, VANILLA_EnvImage* out) {
    VkImageCreateInfo imageInfo = {
        .sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO,
        .flags = cube ? VK_IMAGE_CREATE_CUBE_COMPATIBLE_BIT : 0,
        .imageType = VK_IMAGE_TYPE_2D,
        .format = VK_FORMAT_R16G16B16A16_SFLOAT,
        .extent = { size, size, 1 },
        .mipLevels = mips,
        .arrayLayers = cube ? 6 : 1,
        .samples = VK_SAMPLE_COUNT_1_BIT,
        .tiling = VK_IMAGE_TILING_OPTIMAL,
        .usage = usage,
        .sharingMode = VK_SHARING_MODE_EXCLUSIVE,
        .initialLayout = VK_IMAGE_LAYOUT_UNDEFINED
    };

    if (vkCreateImage(vulkan.device, &imageInfo, NULL, &out->image) != VK_SUCCESS) {
        out->image = VK_NULL_HANDLE;
        return 1;
    }

    VkMemoryRequirements requirements;
    vkGetImageMemoryRequirements(vulkan.device, out->image, &requirements);
    out->memory = VANILLA_gmmap(&requirements, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
    if (out->memory == VK_NULL_HANDLE) { return 1; }
    if (vkBindImageMemory(vulkan.device, out->image, out->memory, 0) != VK_SUCCESS) { return 1; }

    const uint32_t layers = cube ? 6 : 1;
    if (VANILLA_VK_createEnvView(out->image, cube ? VK_IMAGE_VIEW_TYPE_CUBE : VK_IMAGE_VIEW_TYPE_2D, 0, mips, layers, &out->sampledView)) { return 1; }

    if (usage & VK_IMAGE_USAGE_STORAGE_BIT) {
        if (VANILLA_VK_createEnvView(out->image, cube ? VK_IMAGE_VIEW_TYPE_2D_ARRAY : VK_IMAGE_VIEW_TYPE_2D, 0, 1, layers, &out->storageView)) { return 1; }
    }
    return 0;
}

/* RGBA16F 3D image, one view used for both sampling and storage. */
static int VANILLA_VK_createVolumeImage(uint32_t size, VkImageUsageFlags usage, VANILLA_EnvImage* out) {
    VkImageCreateInfo imageInfo = {
        .sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO,
        .imageType = VK_IMAGE_TYPE_3D,
        .format = VK_FORMAT_R16G16B16A16_SFLOAT,
        .extent = { size, size, size },
        .mipLevels = 1,
        .arrayLayers = 1,
        .samples = VK_SAMPLE_COUNT_1_BIT,
        .tiling = VK_IMAGE_TILING_OPTIMAL,
        .usage = usage,
        .sharingMode = VK_SHARING_MODE_EXCLUSIVE,
        .initialLayout = VK_IMAGE_LAYOUT_UNDEFINED
    };

    if (vkCreateImage(vulkan.device, &imageInfo, NULL, &out->image) != VK_SUCCESS) {
        out->image = VK_NULL_HANDLE;
        return 1;
    }

    VkMemoryRequirements requirements;
    vkGetImageMemoryRequirements(vulkan.device, out->image, &requirements);
    out->memory = VANILLA_gmmap(&requirements, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
    if (out->memory == VK_NULL_HANDLE) { return 1; }
    if (vkBindImageMemory(vulkan.device, out->image, out->memory, 0) != VK_SUCCESS) { return 1; }

    return VANILLA_VK_createEnvView(out->image, VK_IMAGE_VIEW_TYPE_3D, 0, 1, 1, &out->sampledView);
}

static void VANILLA_VK_destroyIblImage(VANILLA_EnvImage* image) {
    if (image->storageView) { vkDestroyImageView(vulkan.device, image->storageView, NULL); }
    if (image->sampledView) { vkDestroyImageView(vulkan.device, image->sampledView, NULL); }
    if (image->image)       { vkDestroyImage(vulkan.device, image->image, NULL); }
    if (image->memory)      { VANILLA_gmunmap(image->memory); }
    *image = (VANILLA_EnvImage){0};
}

/* Every listed image (all mips / layers) UNDEFINED -> GENERAL, once. */
static int VANILLA_VK_transitionToGeneral(const VkImage* images, uint32_t count) {
    if (count == 0) { return 0; }

    VkCommandBuffer commandBuffer = VANILLA_VK_beginSingleTimeCommands();
    if (commandBuffer == VK_NULL_HANDLE) { return 1; }

    VkImageMemoryBarrier2 barriers[8];
    for (uint32_t i = 0; i < count; i++) {
        barriers[i] = (VkImageMemoryBarrier2){
            .sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2,
            .srcStageMask = VK_PIPELINE_STAGE_2_NONE,
            .srcAccessMask = VK_ACCESS_2_NONE,
            .dstStageMask = VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT | VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT,
            .dstAccessMask = VK_ACCESS_2_SHADER_STORAGE_WRITE_BIT | VK_ACCESS_2_SHADER_SAMPLED_READ_BIT,
            .oldLayout = VK_IMAGE_LAYOUT_UNDEFINED,
            .newLayout = VK_IMAGE_LAYOUT_GENERAL,
            .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
            .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
            .image = images[i],
            .subresourceRange = { VK_IMAGE_ASPECT_COLOR_BIT, 0, VK_REMAINING_MIP_LEVELS, 0, VK_REMAINING_ARRAY_LAYERS }
        };
    }

    VkDependencyInfo dependencyInfo = {
        .sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO,
        .imageMemoryBarrierCount = count,
        .pImageMemoryBarriers = barriers
    };
    vkCmdPipelineBarrier2(commandBuffer, &dependencyInfo);

    return VANILLA_VK_endSingleTimeCommands(commandBuffer);
}

/*
 * IBL images. Active: full size. Inactive (2D / no lighting): 1x1 placeholders for bindings 15/16
 * (never sampled: the environment flag stays off).
 */
static int VANILLA_VK_createIblImages(void) {
    const VkImageUsageFlags usage = environmentActive
        ? (VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_SAMPLED_BIT)
        : VK_IMAGE_USAGE_SAMPLED_BIT;

    prefilterMipCount = environmentActive ? VANILLA_ENV_PREFILTER_MIPS : 1;

    if (VANILLA_VK_createIblImage(environmentActive ? VANILLA_ENV_CUBE_SIZE : 1, prefilterMipCount, true, usage, &prefilterCube)) { return 1; }
    if (VANILLA_VK_createIblImage(environmentActive ? VANILLA_BRDF_LUT_SIZE : 1, 1, false, usage, &brdfLut)) { return 1; }
    if (VANILLA_VK_createVolumeImage(environmentActive ? VANILLA_AERIAL_SIZE : 1, usage, &aerialVolume)) { return 1; }

    if (environmentActive) {
        if (VANILLA_VK_createIblImage(VANILLA_ENV_CUBE_SIZE, 1, true, usage, &envCube)) { return 1; }

        for (uint32_t m = 0; m < prefilterMipCount; m++) {
            if (VANILLA_VK_createEnvView(prefilterCube.image, VK_IMAGE_VIEW_TYPE_2D_ARRAY, m, 1, 6, &prefilterMipViews[m])) { return 1; }
        }
    }

    VkSamplerCreateInfo samplerInfo = {
        .sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO,
        .magFilter = VK_FILTER_LINEAR,
        .minFilter = VK_FILTER_LINEAR,
        .mipmapMode = VK_SAMPLER_MIPMAP_MODE_LINEAR,
        .addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE,
        .addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE,
        .addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE,
        .maxLod = VK_LOD_CLAMP_NONE
    };
    if (vkCreateSampler(vulkan.device, &samplerInfo, NULL, &envSampler) != VK_SUCCESS) {
        envSampler = VK_NULL_HANDLE;
        return 1;
    }

    VkImage images[4] = { prefilterCube.image, brdfLut.image, aerialVolume.image, envCube.image };
    return VANILLA_VK_transitionToGeneral(images, environmentActive ? 4 : 3);
}

static void VANILLA_VK_destroyIblImages(void) {
    for (uint32_t m = 0; m < VANILLA_ENV_PREFILTER_MIPS; m++) {
        if (prefilterMipViews[m] != VK_NULL_HANDLE) {
            vkDestroyImageView(vulkan.device, prefilterMipViews[m], NULL);
            prefilterMipViews[m] = VK_NULL_HANDLE;
        }
    }
    VANILLA_VK_destroyIblImage(&envCube);
    VANILLA_VK_destroyIblImage(&prefilterCube);
    VANILLA_VK_destroyIblImage(&brdfLut);
    VANILLA_VK_destroyIblImage(&aerialVolume);

    if (envSampler != VK_NULL_HANDLE) { vkDestroySampler(vulkan.device, envSampler, NULL); envSampler = VK_NULL_HANDLE; }
}

static int VANILLA_VK_createEnvImage(uint32_t width, uint32_t height, VkImage* outImage, VkDeviceMemory* outMemory, VkImageView* outView) {
    VkImageCreateInfo imageInfo = {
        .sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO,
        .imageType = VK_IMAGE_TYPE_2D,
        .format = VK_FORMAT_R16G16B16A16_SFLOAT,
        .extent = { width, height, 1 },
        .mipLevels = 1,
        .arrayLayers = 1,
        .samples = VK_SAMPLE_COUNT_1_BIT,
        .tiling = VK_IMAGE_TILING_OPTIMAL,
        .usage = VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_SAMPLED_BIT,
        .sharingMode = VK_SHARING_MODE_EXCLUSIVE,
        .initialLayout = VK_IMAGE_LAYOUT_UNDEFINED
    };

    if (vkCreateImage(vulkan.device, &imageInfo, NULL, outImage) != VK_SUCCESS) {
        *outImage = VK_NULL_HANDLE;
        return 1;
    }

    VkMemoryRequirements requirements;
    vkGetImageMemoryRequirements(vulkan.device, *outImage, &requirements);

    *outMemory = VANILLA_gmmap(&requirements, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
    if (*outMemory == VK_NULL_HANDLE) { return 1; }

    if (vkBindImageMemory(vulkan.device, *outImage, *outMemory, 0) != VK_SUCCESS) { return 1; }

    VkImageViewCreateInfo viewInfo = {
        .sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO,
        .image = *outImage,
        .viewType = VK_IMAGE_VIEW_TYPE_2D,
        .format = VK_FORMAT_R16G16B16A16_SFLOAT,
        .subresourceRange = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1 }
    };

    if (vkCreateImageView(vulkan.device, &viewInfo, NULL, outView) != VK_SUCCESS) {
        *outView = VK_NULL_HANDLE;
        return 1;
    }

    return 0;
}

static int VANILLA_VK_createEnvSampler(VkSamplerAddressMode addressU, VkSampler* outSampler) {
    VkSamplerCreateInfo samplerInfo = {
        .sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO,
        .magFilter = VK_FILTER_LINEAR,
        .minFilter = VK_FILTER_LINEAR,
        .mipmapMode = VK_SAMPLER_MIPMAP_MODE_NEAREST,
        .addressModeU = addressU,
        .addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE,
        .addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE,
        .maxLod = 0.0f
    };

    if (vkCreateSampler(vulkan.device, &samplerInfo, NULL, outSampler) != VK_SUCCESS) {
        *outSampler = VK_NULL_HANDLE;
        return 1;
    }
    return 0;
}

/* All LUTs UNDEFINED -> GENERAL, once. */
static int VANILLA_VK_transitionEnvImages(void) {
    VkCommandBuffer commandBuffer = VANILLA_VK_beginSingleTimeCommands();
    if (commandBuffer == VK_NULL_HANDLE) { return 1; }

    VkImageMemoryBarrier2 barriers[VANILLA_ENV_LUT_COUNT];
    for (int i = 0; i < VANILLA_ENV_LUT_COUNT; i++) {
        barriers[i] = (VkImageMemoryBarrier2){
            .sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2,
            .srcStageMask = VK_PIPELINE_STAGE_2_NONE,
            .srcAccessMask = VK_ACCESS_2_NONE,
            .dstStageMask = VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT,
            .dstAccessMask = VK_ACCESS_2_SHADER_STORAGE_WRITE_BIT,
            .oldLayout = VK_IMAGE_LAYOUT_UNDEFINED,
            .newLayout = VK_IMAGE_LAYOUT_GENERAL,
            .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
            .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
            .image = lutImages[i],
            .subresourceRange = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1 }
        };
    }

    VkDependencyInfo dependencyInfo = {
        .sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO,
        .imageMemoryBarrierCount = VANILLA_ENV_LUT_COUNT,
        .pImageMemoryBarriers = barriers
    };
    vkCmdPipelineBarrier2(commandBuffer, &dependencyInfo);

    return VANILLA_VK_endSingleTimeCommands(commandBuffer);
}

/*
 * One set for every environment compute shader:
 *   0 AtmosphereParams (storage)   1-3 LUT storage images   4 transmittance sampled   5 multi-scatter sampled
 *   6 env cube storage (2D array)  7 env cube sampled (cube) 8 LightingInfo GPU part   9 BRDF LUT storage
 *   10 sky-view sampled (U repeat)  11 aerial perspective storage (3D)
 */
#define VANILLA_ENV_SET_BINDINGS 12

static int VANILLA_VK_createAtmospherePipelines(void) {
    const VkShaderStageFlags cs = VK_SHADER_STAGE_COMPUTE_BIT;
    VkDescriptorSetLayoutBinding bindings[VANILLA_ENV_SET_BINDINGS] = {
        { .binding = 0,  .descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,         .descriptorCount = 1, .stageFlags = cs },
        { .binding = 1,  .descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE,          .descriptorCount = 1, .stageFlags = cs },
        { .binding = 2,  .descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE,          .descriptorCount = 1, .stageFlags = cs },
        { .binding = 3,  .descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE,          .descriptorCount = 1, .stageFlags = cs },
        { .binding = 4,  .descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, .descriptorCount = 1, .stageFlags = cs },
        { .binding = 5,  .descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, .descriptorCount = 1, .stageFlags = cs },
        { .binding = 6,  .descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE,          .descriptorCount = 1, .stageFlags = cs },
        { .binding = 7,  .descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, .descriptorCount = 1, .stageFlags = cs },
        { .binding = 8,  .descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,         .descriptorCount = 1, .stageFlags = cs },
        { .binding = 9,  .descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE,          .descriptorCount = 1, .stageFlags = cs },
        { .binding = 10, .descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, .descriptorCount = 1, .stageFlags = cs },
        { .binding = 11, .descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE,          .descriptorCount = 1, .stageFlags = cs }
    };

    VkDescriptorSetLayoutCreateInfo layoutInfo = {
        .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO,
        .bindingCount = VANILLA_ENV_SET_BINDINGS,
        .pBindings = bindings
    };

    if (vkCreateDescriptorSetLayout(vulkan.device, &layoutInfo, NULL, &atmoSetLayout) != VK_SUCCESS) {
        atmoSetLayout = VK_NULL_HANDLE;
        return 1;
    }

    VkDescriptorPoolSize poolSizes[3] = {
        { .type = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,         .descriptorCount = 2 },
        { .type = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE,          .descriptorCount = 6 },
        { .type = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, .descriptorCount = 4 }
    };

    VkDescriptorPoolCreateInfo poolInfo = {
        .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO,
        .maxSets = 1,
        .poolSizeCount = 3,
        .pPoolSizes = poolSizes
    };

    if (vkCreateDescriptorPool(vulkan.device, &poolInfo, NULL, &atmoDescriptorPool) != VK_SUCCESS) {
        atmoDescriptorPool = VK_NULL_HANDLE;
        return 1;
    }

    VkDescriptorSetAllocateInfo allocInfo = {
        .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO,
        .descriptorPool = atmoDescriptorPool,
        .descriptorSetCount = 1,
        .pSetLayouts = &atmoSetLayout
    };

    if (vkAllocateDescriptorSets(vulkan.device, &allocInfo, &atmoDescriptorSet) != VK_SUCCESS) { return 1; }

    VkDescriptorBufferInfo bufferInfo = { .buffer = atmosphereBuffer, .offset = 0, .range = VK_WHOLE_SIZE };

    VkDescriptorImageInfo storageInfos[VANILLA_ENV_LUT_COUNT];
    for (int i = 0; i < VANILLA_ENV_LUT_COUNT; i++) {
        storageInfos[i] = (VkDescriptorImageInfo){ VK_NULL_HANDLE, lutViews[i], VK_IMAGE_LAYOUT_GENERAL };
    }

    VkDescriptorImageInfo sampledInfos[2] = {
        { lutSampler, lutViews[VANILLA_ENV_LUT_TRANSMITTANCE], VK_IMAGE_LAYOUT_GENERAL },
        { lutSampler, lutViews[VANILLA_ENV_LUT_MULTISCATTER],  VK_IMAGE_LAYOUT_GENERAL }
    };

    VkWriteDescriptorSet writes[6] = {0};
    for (int i = 0; i < 6; i++) {
        writes[i].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        writes[i].dstSet = atmoDescriptorSet;
        writes[i].dstBinding = (uint32_t)i;
        writes[i].descriptorCount = 1;
    }

    writes[0].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    writes[0].pBufferInfo = &bufferInfo;

    for (int i = 0; i < VANILLA_ENV_LUT_COUNT; i++) {
        writes[1 + i].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
        writes[1 + i].pImageInfo = &storageInfos[i];
    }

    writes[4].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    writes[4].pImageInfo = &sampledInfos[0];
    writes[5].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    writes[5].pImageInfo = &sampledInfos[1];

    /* Environment lighting */
    VkDescriptorImageInfo envStorageInfo = { VK_NULL_HANDLE, envCube.storageView, VK_IMAGE_LAYOUT_GENERAL };
    VkDescriptorImageInfo envSampledInfo = { envSampler,     envCube.sampledView, VK_IMAGE_LAYOUT_GENERAL };
    VkDescriptorImageInfo brdfStorageInfo = { VK_NULL_HANDLE, brdfLut.storageView, VK_IMAGE_LAYOUT_GENERAL };
    VkDescriptorImageInfo skyViewInfo    = { wrapUSampler,   lutViews[VANILLA_ENV_LUT_SKYVIEW], VK_IMAGE_LAYOUT_GENERAL };
    VkDescriptorBufferInfo tailInfo = {
        .buffer = VANILLA_VK_getLightingInfoBuffer(),
        .offset = VANILLA_LIGHTINGINFO_CPU_SIZE,
        .range  = VANILLA_LIGHTINGINFO_GPU_SIZE
    };

    VkDescriptorImageInfo aerialStorageInfo = { VK_NULL_HANDLE, aerialVolume.sampledView, VK_IMAGE_LAYOUT_GENERAL };

    VkWriteDescriptorSet envWrites[6] = {0};
    for (int i = 0; i < 6; i++) {
        envWrites[i].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        envWrites[i].dstSet = atmoDescriptorSet;
        envWrites[i].dstBinding = 6 + (uint32_t)i;
        envWrites[i].descriptorCount = 1;
    }
    envWrites[0].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;          envWrites[0].pImageInfo  = &envStorageInfo;
    envWrites[1].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER; envWrites[1].pImageInfo  = &envSampledInfo;
    envWrites[2].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;         envWrites[2].pBufferInfo = &tailInfo;
    envWrites[3].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;          envWrites[3].pImageInfo  = &brdfStorageInfo;
    envWrites[4].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER; envWrites[4].pImageInfo  = &skyViewInfo;
    envWrites[5].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;          envWrites[5].pImageInfo  = &aerialStorageInfo;

    vkUpdateDescriptorSets(vulkan.device, 6, writes, 0, NULL);
    vkUpdateDescriptorSets(vulkan.device, 6, envWrites, 0, NULL);

    VkPipelineLayoutCreateInfo pipelineLayoutInfo = {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO,
        .setLayoutCount = 1,
        .pSetLayouts = &atmoSetLayout
    };

    if (vkCreatePipelineLayout(vulkan.device, &pipelineLayoutInfo, NULL, &atmoPipelineLayout) != VK_SUCCESS) {
        atmoPipelineLayout = VK_NULL_HANDLE;
        return 1;
    }

    const uint32_t* codes[VANILLA_ENV_LUT_COUNT] = {
        VANILLA_atmoTransmittanceSpv, VANILLA_atmoMultiScatterSpv, VANILLA_atmoSkyViewSpv
    };
    const size_t lengths[VANILLA_ENV_LUT_COUNT] = {
        VANILLA_atmoTransmittanceSpv_len, VANILLA_atmoMultiScatterSpv_len, VANILLA_atmoSkyViewSpv_len
    };

    for (int i = 0; i < VANILLA_ENV_LUT_COUNT; i++) {
        VkShaderModuleCreateInfo shaderInfo = {
            .sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO,
            .codeSize = lengths[i] * sizeof(uint32_t),
            .pCode = codes[i]
        };

        VkShaderModule shaderModule = VK_NULL_HANDLE;
        if (vkCreateShaderModule(vulkan.device, &shaderInfo, NULL, &shaderModule) != VK_SUCCESS) { return 1; }

        VkComputePipelineCreateInfo pipelineInfo = {
            .sType = VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO,
            .stage = {
                .sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
                .stage = VK_SHADER_STAGE_COMPUTE_BIT,
                .module = shaderModule,
                .pName = "main"
            },
            .layout = atmoPipelineLayout,
            .basePipelineIndex = -1
        };

        VkResult result = vkCreateComputePipelines(vulkan.device, VK_NULL_HANDLE, 1, &pipelineInfo, NULL, &atmoPipelines[i]);
        vkDestroyShaderModule(vulkan.device, shaderModule, NULL);

        if (result != VK_SUCCESS) {
            atmoPipelines[i] = VK_NULL_HANDLE;
            return 1;
        }
    }

    return 0;
}

static VkPipeline VANILLA_VK_createEnvComputePipeline(const uint32_t* code, size_t words, VkPipelineLayout layout) {
    VkShaderModuleCreateInfo shaderInfo = {
        .sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO,
        .codeSize = words * sizeof(uint32_t),
        .pCode = code
    };

    VkShaderModule shaderModule = VK_NULL_HANDLE;
    if (vkCreateShaderModule(vulkan.device, &shaderInfo, NULL, &shaderModule) != VK_SUCCESS) { return VK_NULL_HANDLE; }

    VkComputePipelineCreateInfo pipelineInfo = {
        .sType = VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO,
        .stage = {
            .sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
            .stage = VK_SHADER_STAGE_COMPUTE_BIT,
            .module = shaderModule,
            .pName = "main"
        },
        .layout = layout,
        .basePipelineIndex = -1
    };

    VkPipeline pipeline = VK_NULL_HANDLE;
    if (vkCreateComputePipelines(vulkan.device, VK_NULL_HANDLE, 1, &pipelineInfo, NULL, &pipeline) != VK_SUCCESS) {
        pipeline = VK_NULL_HANDLE;
    }
    vkDestroyShaderModule(vulkan.device, shaderModule, NULL);
    return pipeline;
}

/*
 * IBL pipelines: set 0 = environment set, set 1 = one prefilter mip (storage 2D array), push = prefilter params.
 */
static int VANILLA_VK_createEnvironmentPipelines(void) {
    VkDescriptorSetLayoutBinding mipBinding = {
        .binding = 0,
        .descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE,
        .descriptorCount = 1,
        .stageFlags = VK_SHADER_STAGE_COMPUTE_BIT
    };
    VkDescriptorSetLayoutCreateInfo mipLayoutInfo = {
        .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO,
        .bindingCount = 1,
        .pBindings = &mipBinding
    };
    if (vkCreateDescriptorSetLayout(vulkan.device, &mipLayoutInfo, NULL, &mipSetLayout) != VK_SUCCESS) {
        mipSetLayout = VK_NULL_HANDLE;
        return 1;
    }

    VkDescriptorPoolSize poolSize = { .type = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, .descriptorCount = VANILLA_ENV_PREFILTER_MIPS };
    VkDescriptorPoolCreateInfo poolInfo = {
        .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO,
        .maxSets = VANILLA_ENV_PREFILTER_MIPS,
        .poolSizeCount = 1,
        .pPoolSizes = &poolSize
    };
    if (vkCreateDescriptorPool(vulkan.device, &poolInfo, NULL, &mipDescriptorPool) != VK_SUCCESS) {
        mipDescriptorPool = VK_NULL_HANDLE;
        return 1;
    }

    VkDescriptorSetLayout layouts[VANILLA_ENV_PREFILTER_MIPS];
    for (uint32_t m = 0; m < VANILLA_ENV_PREFILTER_MIPS; m++) { layouts[m] = mipSetLayout; }

    VkDescriptorSetAllocateInfo allocInfo = {
        .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO,
        .descriptorPool = mipDescriptorPool,
        .descriptorSetCount = prefilterMipCount,
        .pSetLayouts = layouts
    };
    if (vkAllocateDescriptorSets(vulkan.device, &allocInfo, mipSets) != VK_SUCCESS) { return 1; }

    VkDescriptorImageInfo mipInfos[VANILLA_ENV_PREFILTER_MIPS];
    VkWriteDescriptorSet  mipWrites[VANILLA_ENV_PREFILTER_MIPS];
    for (uint32_t m = 0; m < prefilterMipCount; m++) {
        mipInfos[m] = (VkDescriptorImageInfo){ VK_NULL_HANDLE, prefilterMipViews[m], VK_IMAGE_LAYOUT_GENERAL };
        mipWrites[m] = (VkWriteDescriptorSet){
            .sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
            .dstSet = mipSets[m],
            .dstBinding = 0,
            .descriptorCount = 1,
            .descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE,
            .pImageInfo = &mipInfos[m]
        };
    }
    vkUpdateDescriptorSets(vulkan.device, prefilterMipCount, mipWrites, 0, NULL);

    VkDescriptorSetLayout setLayouts[2] = { atmoSetLayout, mipSetLayout };
    /* Prefilter (16 B) and aerial (camera basis, 48 B) share the range. */
    VkPushConstantRange pushRange = {
        .stageFlags = VK_SHADER_STAGE_COMPUTE_BIT,
        .offset = 0,
        .size = sizeof(VANILLA_SkyPush)
    };
    VkPipelineLayoutCreateInfo layoutInfo = {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO,
        .setLayoutCount = 2,
        .pSetLayouts = setLayouts,
        .pushConstantRangeCount = 1,
        .pPushConstantRanges = &pushRange
    };
    if (vkCreatePipelineLayout(vulkan.device, &layoutInfo, NULL, &envPipelineLayout) != VK_SUCCESS) {
        envPipelineLayout = VK_NULL_HANDLE;
        return 1;
    }

    const uint32_t* codes[VANILLA_ENV_PIPE_COUNT] = {
        VANILLA_envCubeSpv, VANILLA_envShSpv, VANILLA_envPrefilterSpv, VANILLA_brdfLutSpv, VANILLA_atmoAerialSpv
    };
    const size_t lengths[VANILLA_ENV_PIPE_COUNT] = {
        VANILLA_envCubeSpv_len, VANILLA_envShSpv_len, VANILLA_envPrefilterSpv_len, VANILLA_brdfLutSpv_len, VANILLA_atmoAerialSpv_len
    };

    for (int i = 0; i < VANILLA_ENV_PIPE_COUNT; i++) {
        envPipelines[i] = VANILLA_VK_createEnvComputePipeline(codes[i], lengths[i], envPipelineLayout);
        if (envPipelines[i] == VK_NULL_HANDLE) { return 1; }
    }

    return 0;
}

static void VANILLA_VK_destroyEnvironmentPipelines(void) {
    for (int i = 0; i < VANILLA_ENV_PIPE_COUNT; i++) {
        if (envPipelines[i] != VK_NULL_HANDLE) {
            vkDestroyPipeline(vulkan.device, envPipelines[i], NULL);
            envPipelines[i] = VK_NULL_HANDLE;
        }
    }
    if (envPipelineLayout != VK_NULL_HANDLE) {
        vkDestroyPipelineLayout(vulkan.device, envPipelineLayout, NULL);
        envPipelineLayout = VK_NULL_HANDLE;
    }
    if (mipDescriptorPool != VK_NULL_HANDLE) {
        vkDestroyDescriptorPool(vulkan.device, mipDescriptorPool, NULL);
        mipDescriptorPool = VK_NULL_HANDLE;
        for (uint32_t m = 0; m < VANILLA_ENV_PREFILTER_MIPS; m++) { mipSets[m] = VK_NULL_HANDLE; }
    }
    if (mipSetLayout != VK_NULL_HANDLE) {
        vkDestroyDescriptorSetLayout(vulkan.device, mipSetLayout, NULL);
        mipSetLayout = VK_NULL_HANDLE;
    }
}

/* Sky descriptor set: 0 AtmosphereParams, 1 sky-view LUT (U repeat), 2 transmittance LUT. Fragment only. */
static int VANILLA_VK_createSkySet(void) {
    VkDescriptorSetLayoutBinding bindings[3] = {
        { .binding = 0, .descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,         .descriptorCount = 1, .stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT },
        { .binding = 1, .descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, .descriptorCount = 1, .stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT },
        { .binding = 2, .descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, .descriptorCount = 1, .stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT }
    };

    VkDescriptorSetLayoutCreateInfo layoutInfo = {
        .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO,
        .bindingCount = 3,
        .pBindings = bindings
    };

    if (vkCreateDescriptorSetLayout(vulkan.device, &layoutInfo, NULL, &skySetLayout) != VK_SUCCESS) {
        skySetLayout = VK_NULL_HANDLE;
        return 1;
    }

    VkDescriptorPoolSize poolSizes[2] = {
        { .type = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,         .descriptorCount = 1 },
        { .type = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, .descriptorCount = 2 }
    };

    VkDescriptorPoolCreateInfo poolInfo = {
        .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO,
        .maxSets = 1,
        .poolSizeCount = 2,
        .pPoolSizes = poolSizes
    };

    if (vkCreateDescriptorPool(vulkan.device, &poolInfo, NULL, &skyDescriptorPool) != VK_SUCCESS) {
        skyDescriptorPool = VK_NULL_HANDLE;
        return 1;
    }

    VkDescriptorSetAllocateInfo allocInfo = {
        .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO,
        .descriptorPool = skyDescriptorPool,
        .descriptorSetCount = 1,
        .pSetLayouts = &skySetLayout
    };

    if (vkAllocateDescriptorSets(vulkan.device, &allocInfo, &skyDescriptorSet) != VK_SUCCESS) { return 1; }

    VkDescriptorBufferInfo bufferInfo = { .buffer = atmosphereBuffer, .offset = 0, .range = VK_WHOLE_SIZE };
    VkDescriptorImageInfo imageInfos[2] = {
        { wrapUSampler, lutViews[VANILLA_ENV_LUT_SKYVIEW],       VK_IMAGE_LAYOUT_GENERAL },
        { lutSampler,   lutViews[VANILLA_ENV_LUT_TRANSMITTANCE], VK_IMAGE_LAYOUT_GENERAL }
    };

    VkWriteDescriptorSet writes[3] = {0};
    for (int i = 0; i < 3; i++) {
        writes[i].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        writes[i].dstSet = skyDescriptorSet;
        writes[i].dstBinding = (uint32_t)i;
        writes[i].descriptorCount = 1;
        writes[i].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    }
    writes[0].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    writes[0].pBufferInfo = &bufferInfo;
    writes[1].pImageInfo = &imageInfos[0];
    writes[2].pImageInfo = &imageInfos[1];

    vkUpdateDescriptorSets(vulkan.device, 3, writes, 0, NULL);

    VkPushConstantRange pushRange = {
        .stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT,
        .offset = 0,
        .size = sizeof(VANILLA_SkyPush)
    };

    VkPipelineLayoutCreateInfo pipelineLayoutInfo = {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO,
        .setLayoutCount = 1,
        .pSetLayouts = &skySetLayout,
        .pushConstantRangeCount = 1,
        .pPushConstantRanges = &pushRange
    };

    if (vkCreatePipelineLayout(vulkan.device, &pipelineLayoutInfo, NULL, &skyPipelineLayout) != VK_SUCCESS) {
        skyPipelineLayout = VK_NULL_HANDLE;
        return 1;
    }

    return 0;
}

/* Sample count follows MSAA, so this is rebuilt with the other graphics pipelines. */
int VANILLA_VK_createSkyPipeline(void) {
    if (!environmentActive || skyPipelineLayout == VK_NULL_HANDLE) { return 0; }
    if (VANILLA_VK_firstHdrPipeline() < 0) { return 0; }   /* nothing to draw the sky into */

    VkShaderModuleCreateInfo vertInfo = {
        .sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO,
        .codeSize = VANILLA_skyVertSpv_len * sizeof(uint32_t),
        .pCode = VANILLA_skyVertSpv
    };
    VkShaderModuleCreateInfo fragInfo = {
        .sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO,
        .codeSize = VANILLA_skyFragSpv_len * sizeof(uint32_t),
        .pCode = VANILLA_skyFragSpv
    };

    VkShaderModule vert = VK_NULL_HANDLE, frag = VK_NULL_HANDLE;
    if (vkCreateShaderModule(vulkan.device, &vertInfo, NULL, &vert) != VK_SUCCESS) { return 1; }
    if (vkCreateShaderModule(vulkan.device, &fragInfo, NULL, &frag) != VK_SUCCESS) {
        vkDestroyShaderModule(vulkan.device, vert, NULL);
        return 1;
    }

    VkPipelineShaderStageCreateInfo stages[2] = {
        { .sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO, .stage = VK_SHADER_STAGE_VERTEX_BIT,   .module = vert, .pName = "main" },
        { .sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO, .stage = VK_SHADER_STAGE_FRAGMENT_BIT, .module = frag, .pName = "main" }
    };

    VkPipelineVertexInputStateCreateInfo vertexInput = { .sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO };
    VkPipelineInputAssemblyStateCreateInfo inputAssembly = {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO,
        .topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST
    };
    VkPipelineViewportStateCreateInfo viewportState = {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO,
        .viewportCount = 1,
        .scissorCount = 1
    };
    VkPipelineRasterizationStateCreateInfo rasterizer = {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO,
        .polygonMode = VK_POLYGON_MODE_FILL,
        .cullMode = VK_CULL_MODE_NONE,
        .frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE,
        .lineWidth = 1.0f
    };
    VkPipelineMultisampleStateCreateInfo multisample = {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO,
        .rasterizationSamples = VANILLA_VK_msaaSamples()
    };
    VkPipelineDepthStencilStateCreateInfo depthStencil = {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO,
        .depthTestEnable = VK_TRUE,
        .depthWriteEnable = VK_FALSE,
        .depthCompareOp = VK_COMPARE_OP_LESS_OR_EQUAL   /* cleared depth (1) passes, geometry doesn't */
    };
    VkPipelineColorBlendAttachmentState blend = {
        .blendEnable = VK_FALSE,
        .colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT |
                          VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT
    };
    VkPipelineColorBlendStateCreateInfo colorBlend = {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO,
        .attachmentCount = 1,
        .pAttachments = &blend
    };
    VkDynamicState dynamicStates[] = { VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR };
    VkPipelineDynamicStateCreateInfo dynamicState = {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO,
        .dynamicStateCount = 2,
        .pDynamicStates = dynamicStates
    };

    const VkFormat colorFormat = VANILLA_VK_pipelineColorFormat((uint16_t)VANILLA_VK_firstHdrPipeline());
    VkPipelineRenderingCreateInfo renderingInfo = {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO,
        .colorAttachmentCount = 1,
        .pColorAttachmentFormats = &colorFormat,
        .depthAttachmentFormat = VK_FORMAT_D32_SFLOAT
    };

    VkGraphicsPipelineCreateInfo pipelineInfo = {
        .sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO,
        .pNext = &renderingInfo,
        .stageCount = 2,
        .pStages = stages,
        .pVertexInputState = &vertexInput,
        .pInputAssemblyState = &inputAssembly,
        .pViewportState = &viewportState,
        .pRasterizationState = &rasterizer,
        .pMultisampleState = &multisample,
        .pDepthStencilState = &depthStencil,
        .pColorBlendState = &colorBlend,
        .pDynamicState = &dynamicState,
        .layout = skyPipelineLayout,
        .basePipelineIndex = -1
    };

    VkResult result = vkCreateGraphicsPipelines(vulkan.device, VK_NULL_HANDLE, 1, &pipelineInfo, NULL, &skyPipeline);

    vkDestroyShaderModule(vulkan.device, vert, NULL);
    vkDestroyShaderModule(vulkan.device, frag, NULL);

    if (result != VK_SUCCESS) {
        skyPipeline = VK_NULL_HANDLE;
        return 1;
    }
    return 0;
}

void VANILLA_VK_destroySkyPipeline(void) {
    if (skyPipeline != VK_NULL_HANDLE) {
        vkDestroyPipeline(vulkan.device, skyPipeline, NULL);
        skyPipeline = VK_NULL_HANDLE;
    }
}

/* --------------------------------------------------------- */
/* Initialization / destruction                               */
/* --------------------------------------------------------- */

int VANILLA_VK_initEnvironment(void) {
    environmentActive = (GET_DIMENSION(ANYCOREBuildID) == 3) && VANILLA_VK_isLightingSupported();

    lutsDirty   = true;
    hasUploaded = false;
    iblDirty    = true;
    iblBuilt    = false;
    brdfBuilt   = false;
    aerialBuilt = false;

    /* Bindings 15/16 exist in every build (placeholders when inactive). */
    if (VANILLA_VK_createIblImages()) { goto fail; }
    if (!environmentActive) { return 0; }

    if (!atmosphereSet) {
        atmosphere    = VANILLA_VK_defaultAtmosphere();
        atmosphereSet = true;
    }

    VkBufferCreateInfo bufferInfo = {
        .sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO,
        .size = sizeof(VANILLA_GPUAtmosphere),
        .usage = VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT,
        .sharingMode = VK_SHARING_MODE_EXCLUSIVE
    };

    if (vkCreateBuffer(vulkan.device, &bufferInfo, NULL, &atmosphereBuffer) != VK_SUCCESS) {
        atmosphereBuffer = VK_NULL_HANDLE;
        goto fail;
    }

    VkMemoryRequirements requirements;
    vkGetBufferMemoryRequirements(vulkan.device, atmosphereBuffer, &requirements);
    atmosphereMemory = VANILLA_gmmap(&requirements, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
    if (atmosphereMemory == VK_NULL_HANDLE) { goto fail; }
    if (vkBindBufferMemory(vulkan.device, atmosphereBuffer, atmosphereMemory, 0) != VK_SUCCESS) { goto fail; }

    for (int i = 0; i < VANILLA_ENV_LUT_COUNT; i++) {
        if (VANILLA_VK_createEnvImage(lutSizes[i][0], lutSizes[i][1], &lutImages[i], &lutMemories[i], &lutViews[i])) { goto fail; }
    }

    if (VANILLA_VK_createEnvSampler(VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE, &lutSampler)) { goto fail; }
    if (VANILLA_VK_createEnvSampler(VK_SAMPLER_ADDRESS_MODE_REPEAT, &wrapUSampler))     { goto fail; }

    if (VANILLA_VK_transitionEnvImages())       { goto fail; }
    if (VANILLA_VK_createAtmospherePipelines())  { goto fail; }
    if (VANILLA_VK_createEnvironmentPipelines()) { goto fail; }
    if (VANILLA_VK_createSkySet())               { goto fail; }
    if (VANILLA_VK_createSkyPipeline())         { goto fail; }

    return 0;

fail:
    printf("[VANILLA WINDOW] environment init failed\n");
    VANILLA_VK_destroyEnvironment();
    return 1;
}

void VANILLA_VK_destroyEnvironment(void) {
    VANILLA_VK_destroySkyPipeline();
    if (skyPipelineLayout != VK_NULL_HANDLE) {
        vkDestroyPipelineLayout(vulkan.device, skyPipelineLayout, NULL);
        skyPipelineLayout = VK_NULL_HANDLE;
    }
    if (skyDescriptorPool != VK_NULL_HANDLE) {
        vkDestroyDescriptorPool(vulkan.device, skyDescriptorPool, NULL);
        skyDescriptorPool = VK_NULL_HANDLE;
        skyDescriptorSet  = VK_NULL_HANDLE;
    }
    if (skySetLayout != VK_NULL_HANDLE) {
        vkDestroyDescriptorSetLayout(vulkan.device, skySetLayout, NULL);
        skySetLayout = VK_NULL_HANDLE;
    }

    VANILLA_VK_destroyEnvironmentPipelines();

    for (int i = 0; i < VANILLA_ENV_LUT_COUNT; i++) {
        if (atmoPipelines[i] != VK_NULL_HANDLE) {
            vkDestroyPipeline(vulkan.device, atmoPipelines[i], NULL);
            atmoPipelines[i] = VK_NULL_HANDLE;
        }
    }
    if (atmoPipelineLayout != VK_NULL_HANDLE) {
        vkDestroyPipelineLayout(vulkan.device, atmoPipelineLayout, NULL);
        atmoPipelineLayout = VK_NULL_HANDLE;
    }
    if (atmoDescriptorPool != VK_NULL_HANDLE) {
        vkDestroyDescriptorPool(vulkan.device, atmoDescriptorPool, NULL);
        atmoDescriptorPool = VK_NULL_HANDLE;
        atmoDescriptorSet  = VK_NULL_HANDLE;
    }
    if (atmoSetLayout != VK_NULL_HANDLE) {
        vkDestroyDescriptorSetLayout(vulkan.device, atmoSetLayout, NULL);
        atmoSetLayout = VK_NULL_HANDLE;
    }

    if (lutSampler != VK_NULL_HANDLE)   { vkDestroySampler(vulkan.device, lutSampler, NULL);   lutSampler = VK_NULL_HANDLE; }
    if (wrapUSampler != VK_NULL_HANDLE) { vkDestroySampler(vulkan.device, wrapUSampler, NULL); wrapUSampler = VK_NULL_HANDLE; }

    for (int i = 0; i < VANILLA_ENV_LUT_COUNT; i++) {
        if (lutViews[i] != VK_NULL_HANDLE)    { vkDestroyImageView(vulkan.device, lutViews[i], NULL); lutViews[i] = VK_NULL_HANDLE; }
        if (lutImages[i] != VK_NULL_HANDLE)   { vkDestroyImage(vulkan.device, lutImages[i], NULL);    lutImages[i] = VK_NULL_HANDLE; }
        if (lutMemories[i] != VK_NULL_HANDLE) { VANILLA_gmunmap(lutMemories[i]);                      lutMemories[i] = VK_NULL_HANDLE; }
    }

    if (atmosphereBuffer != VK_NULL_HANDLE) { vkDestroyBuffer(vulkan.device, atmosphereBuffer, NULL); atmosphereBuffer = VK_NULL_HANDLE; }
    if (atmosphereMemory != VK_NULL_HANDLE) { VANILLA_gmunmap(atmosphereMemory);                    atmosphereMemory = VK_NULL_HANDLE; }

    VANILLA_VK_destroyIblImages();

    environmentActive = false;
    hasUploaded = false;
    iblBuilt    = false;
}

/* --------------------------------------------------------- */
/* Per frame                                                  */
/* --------------------------------------------------------- */

static Vec3f VANILLA_VK_envNormalize(Vec3f v) {
    const float len = sqrtf(v.x * v.x + v.y * v.y + v.z * v.z);
    if (len < 1e-8f) { return (Vec3f){ 0.0f, -1.0f, 0.0f }; }
    return (Vec3f){ v.x / len, v.y / len, v.z / len };
}

/* Camera basis for per-pixel / per-cell view rays (same basis as VANILLA_MATH_lookAt + perspective). */
static VANILLA_SkyPush VANILLA_VK_cameraRayPush(void) {
    Vec3f f = VANILLA_VK_envNormalize((Vec3f){ camera.targetx, camera.targety, camera.targetz });
    Vec3f s = { f.y * camera.upz - f.z * camera.upy,
                f.z * camera.upx - f.x * camera.upz,
                f.x * camera.upy - f.y * camera.upx };
    s = VANILLA_VK_envNormalize(s);
    const Vec3f u = { s.y * f.z - s.z * f.y,
                      s.z * f.x - s.x * f.z,
                      s.x * f.y - s.y * f.x };

    const float aspect = (float)vulkan.swapchainExtent.width / (float)vulkan.swapchainExtent.height;
    const bool  perspective = projectionType != VANILLA_PROJECTION_ORTHOGRAPHIC;
    const float tanY = tanf(camera.fov * 0.5f);
    const float tanX = tanY * aspect;

    return (VANILLA_SkyPush){
        .forward = { f.x, f.y, f.z, perspective ? 1.0f : 0.0f },
        .right   = { s.x * tanX, s.y * tanX, s.z * tanX, 0.0f },
        .up      = { u.x * tanY, u.y * tanY, u.z * tanY, 0.0f }
    };
}

static void VANILLA_VK_buildGpuAtmosphere(VANILLA_GPUAtmosphere* out) {
    const VANILLA_Atmosphere* a = &atmosphere;

    memset(out, 0, sizeof(*out));

    out->rayleighScattering  = a->rayleighScattering;
    out->rayleighScaleHeight = a->rayleighScaleHeight;
    out->mieScattering       = (Vec3f){ a->mieScattering, a->mieScattering, a->mieScattering };
    out->mieScaleHeight      = a->mieScaleHeight;
    const float mieExtinction = a->mieScattering + a->mieAbsorption;
    out->mieExtinction       = (Vec3f){ mieExtinction, mieExtinction, mieExtinction };
    out->mieAnisotropy       = a->mieAnisotropy;
    out->ozoneAbsorption     = a->ozoneAbsorption;
    out->bottomRadius        = a->planetRadius;
    out->groundAlbedo        = a->groundAlbedo;
    out->topRadius           = a->planetRadius + a->atmosphereHeight;
    out->nightColor          = a->nightColor;
    out->metersPerUnit       = a->metersPerUnit;
    out->groundHeight        = a->groundHeight;
    out->aerialScale         = a->aerialPerspectiveScale;

    /* Camera height above the planet center (km), kept just inside the atmosphere. */
    float altitude = (camera.posy - a->groundHeight) * a->metersPerUnit * 0.001f;
    if (altitude < 0.001f) { altitude = 0.001f; }
    if (altitude > a->atmosphereHeight - 0.01f) { altitude = a->atmosphereHeight - 0.01f; }
    out->cameraHeight = a->planetRadius + altitude;

    VANILLA_GPULight sun;
    float sourceSize = 0.0f;

    if (VANILLA_VK_getSunLight(&sun, &sourceSize)) {
        const Vec3f d = VANILLA_VK_envNormalize(sun.direction);
        out->sunDirection   = (Vec3f){ -d.x, -d.y, -d.z };   /* light travels along direction */
        out->sunIlluminance = (Vec3f){ sun.color.x * sun.intensity, sun.color.y * sun.intensity, sun.color.z * sun.intensity };
    }
    else {
        out->sunDirection   = (Vec3f){ 0.0f, -1.0f, 0.0f };  /* no sun: below the horizon, night */
        out->sunIlluminance = (Vec3f){ 0.0f, 0.0f, 0.0f };
    }

    if (sourceSize <= 0.0f) { sourceSize = VANILLA_DEFAULT_SUN_SIZE; }
    out->sunCosHalfAngle = cosf(sourceSize * 0.5f);
}

static void VANILLA_VK_envBarrier(VkCommandBuffer commandBuffer,
                                  VkPipelineStageFlags2 srcStage, VkAccessFlags2 srcAccess,
                                  VkPipelineStageFlags2 dstStage, VkAccessFlags2 dstAccess) {
    VkMemoryBarrier2 barrier = {
        .sType = VK_STRUCTURE_TYPE_MEMORY_BARRIER_2,
        .srcStageMask = srcStage,
        .srcAccessMask = srcAccess,
        .dstStageMask = dstStage,
        .dstAccessMask = dstAccess
    };

    VkDependencyInfo dependencyInfo = {
        .sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO,
        .memoryBarrierCount = 1,
        .pMemoryBarriers = &barrier
    };
    vkCmdPipelineBarrier2(commandBuffer, &dependencyInfo);
}

static inline uint32_t VANILLA_VK_envGroups(uint32_t size, uint32_t local) {
    return (size + local - 1) / local;
}

/* Sun / atmosphere changed, or the camera height moved more than VANILLA_IBL_HEIGHT_STEP since the last build. */
static bool VANILLA_VK_iblNeedsRebuild(const VANILLA_GPUAtmosphere* gpu) {
    if (iblDirty || !iblBuilt) { return true; }
    if (fabsf(gpu->cameraHeight - lastIblAtmosphere.cameraHeight) > VANILLA_IBL_HEIGHT_STEP) { return true; }

    VANILLA_GPUAtmosphere a = *gpu;
    a.cameraHeight = lastIblAtmosphere.cameraHeight;
    return memcmp(&a, &lastIblAtmosphere, sizeof(a)) != 0;
}

void VANILLA_VK_recordEnvironment(VkCommandBuffer commandBuffer) {
    if (!environmentActive) { return; }

    VANILLA_GPUAtmosphere gpu;
    VANILLA_VK_buildGpuAtmosphere(&gpu);

    const bool changed     = !hasUploaded || memcmp(&gpu, &lastUploaded, sizeof(gpu)) != 0;
    const bool rebuildLuts = lutsDirty;
    const bool rebuildSky  = changed || rebuildLuts;
    const bool rebuildIbl  = environmentLighting && (rebuildLuts || VANILLA_VK_iblNeedsRebuild(&gpu));

    /* Aerial perspective follows the camera: rebuilt when the view or the sky changes. */
    const bool perspective = projectionType != VANILLA_PROJECTION_ORTHOGRAPHIC;
    const bool aerialNow   = perspective && atmosphere.aerialPerspectiveScale > 0.0f;
    if (aerialNow != aerialActive || perspective != lastPerspective) {
        aerialActive    = aerialNow;
        lastPerspective = perspective;
        VANILLA_VK_markLightingInfoDirty();   /* flag / scale in the lighting info */
    }

    const VANILLA_SkyPush rayPush = VANILLA_VK_cameraRayPush();
    const bool rebuildAerial = aerialActive &&
        (!aerialBuilt || rebuildSky || memcmp(&rayPush, lastAerialPush, sizeof(rayPush)) != 0);

    if (!rebuildSky && !rebuildIbl && !rebuildAerial) { return; }

    const VkPipelineStageFlags2 cs = VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT;

    /* Last frame's readers (sampling / storage) before the new writes. */
    VANILLA_VK_envBarrier(
        commandBuffer,
        cs | VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT, VK_ACCESS_2_NONE,
        VK_PIPELINE_STAGE_2_TRANSFER_BIT | cs,        VK_ACCESS_2_NONE);

    if (rebuildSky) {
        vkCmdUpdateBuffer(commandBuffer, atmosphereBuffer, 0, sizeof(gpu), &gpu);
        lastUploaded = gpu;
        hasUploaded  = true;

        VANILLA_VK_envBarrier(
            commandBuffer,
            VK_PIPELINE_STAGE_2_TRANSFER_BIT, VK_ACCESS_2_TRANSFER_WRITE_BIT,
            cs | VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT, VK_ACCESS_2_SHADER_STORAGE_READ_BIT);

        vkCmdBindDescriptorSets(
            commandBuffer, VK_PIPELINE_BIND_POINT_COMPUTE,
            atmoPipelineLayout, 0, 1, &atmoDescriptorSet, 0, NULL);

        if (rebuildLuts) {
            vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_COMPUTE, atmoPipelines[VANILLA_ENV_LUT_TRANSMITTANCE]);
            vkCmdDispatch(commandBuffer,
                VANILLA_VK_envGroups(VANILLA_TRANSMITTANCE_W, 8),
                VANILLA_VK_envGroups(VANILLA_TRANSMITTANCE_H, 8), 1);

            VANILLA_VK_envBarrier(commandBuffer, cs, VK_ACCESS_2_SHADER_STORAGE_WRITE_BIT, cs, VK_ACCESS_2_SHADER_SAMPLED_READ_BIT);

            /* One workgroup per texel (64 directions in z). */
            vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_COMPUTE, atmoPipelines[VANILLA_ENV_LUT_MULTISCATTER]);
            vkCmdDispatch(commandBuffer, VANILLA_MULTISCATTER_N, VANILLA_MULTISCATTER_N, 1);

            VANILLA_VK_envBarrier(commandBuffer, cs, VK_ACCESS_2_SHADER_STORAGE_WRITE_BIT, cs, VK_ACCESS_2_SHADER_SAMPLED_READ_BIT);

            lutsDirty = false;
        }

        vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_COMPUTE, atmoPipelines[VANILLA_ENV_LUT_SKYVIEW]);
        vkCmdDispatch(commandBuffer,
            VANILLA_VK_envGroups(VANILLA_SKYVIEW_W, 8),
            VANILLA_VK_envGroups(VANILLA_SKYVIEW_H, 8), 1);

        /* LUTs -> environment compute below and fragment sampling (sky pass). */
        VANILLA_VK_envBarrier(
            commandBuffer,
            cs, VK_ACCESS_2_SHADER_STORAGE_WRITE_BIT,
            cs | VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT, VK_ACCESS_2_SHADER_SAMPLED_READ_BIT);
    }

    if (rebuildIbl) {

        /* Layout differs from the atmosphere one (push constants, set 1): bind set 0 again. */
        vkCmdBindDescriptorSets(
            commandBuffer, VK_PIPELINE_BIND_POINT_COMPUTE,
            envPipelineLayout, 0, 1, &atmoDescriptorSet, 0, NULL);

        if (!brdfBuilt) {
            vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_COMPUTE, envPipelines[VANILLA_ENV_PIPE_BRDF]);
            vkCmdDispatch(commandBuffer,
                VANILLA_VK_envGroups(VANILLA_BRDF_LUT_SIZE, 8),
                VANILLA_VK_envGroups(VANILLA_BRDF_LUT_SIZE, 8), 1);
            brdfBuilt = true;
        }

        vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_COMPUTE, envPipelines[VANILLA_ENV_PIPE_CUBE]);
        vkCmdDispatch(commandBuffer,
            VANILLA_VK_envGroups(VANILLA_ENV_CUBE_SIZE, 8),
            VANILLA_VK_envGroups(VANILLA_ENV_CUBE_SIZE, 8), 6);

        VANILLA_VK_envBarrier(
            commandBuffer,
            cs, VK_ACCESS_2_SHADER_STORAGE_WRITE_BIT,
            cs, VK_ACCESS_2_SHADER_STORAGE_READ_BIT | VK_ACCESS_2_SHADER_SAMPLED_READ_BIT);

        /* SH + sun transmittance (one workgroup) */
        vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_COMPUTE, envPipelines[VANILLA_ENV_PIPE_SH]);
        vkCmdDispatch(commandBuffer, 1, 1, 1);

        /* Prefiltered mips: independent of each other, all read the env cube. */
        vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_COMPUTE, envPipelines[VANILLA_ENV_PIPE_PREFILTER]);
        for (uint32_t m = 0; m < prefilterMipCount; m++) {
            const uint32_t size = VANILLA_ENV_CUBE_SIZE >> m;
            const VANILLA_PrefilterPush push = {
                .roughness = (prefilterMipCount > 1) ? (float)m / (float)(prefilterMipCount - 1) : 0.0f,
                .size = (float)size
            };

            vkCmdBindDescriptorSets(
                commandBuffer, VK_PIPELINE_BIND_POINT_COMPUTE,
                envPipelineLayout, 1, 1, &mipSets[m], 0, NULL);
            vkCmdPushConstants(commandBuffer, envPipelineLayout, VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(push), &push);
            vkCmdDispatch(commandBuffer, VANILLA_VK_envGroups(size, 8), VANILLA_VK_envGroups(size, 8), 6);
        }

        /* Prefiltered cube, BRDF LUT (sampled) and LightingInfo GPU part (storage) -> scene and user compute. */
        VANILLA_VK_envBarrier(
            commandBuffer,
            cs, VK_ACCESS_2_SHADER_STORAGE_WRITE_BIT,
            cs | VK_PIPELINE_STAGE_2_VERTEX_SHADER_BIT | VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT,
            VK_ACCESS_2_SHADER_SAMPLED_READ_BIT | VK_ACCESS_2_SHADER_STORAGE_READ_BIT);

        if (!iblBuilt) { VANILLA_VK_markLightingInfoDirty(); }   /* the flag can turn on now */

        lastIblAtmosphere = gpu;
        iblDirty = false;
        iblBuilt = true;
    }

    if (rebuildAerial) {
        vkCmdBindDescriptorSets(
            commandBuffer, VK_PIPELINE_BIND_POINT_COMPUTE,
            envPipelineLayout, 0, 1, &atmoDescriptorSet, 0, NULL);
        vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_COMPUTE, envPipelines[VANILLA_ENV_PIPE_AERIAL]);
        vkCmdPushConstants(commandBuffer, envPipelineLayout, VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(rayPush), &rayPush);

        /* One thread per screen cell, marching through the 32 slices. */
        vkCmdDispatch(commandBuffer,
            VANILLA_VK_envGroups(VANILLA_AERIAL_SIZE, 8),
            VANILLA_VK_envGroups(VANILLA_AERIAL_SIZE, 8), 1);

        VANILLA_VK_envBarrier(
            commandBuffer,
            cs, VK_ACCESS_2_SHADER_STORAGE_WRITE_BIT,
            cs | VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT, VK_ACCESS_2_SHADER_SAMPLED_READ_BIT);

        if (!aerialBuilt) { VANILLA_VK_markLightingInfoDirty(); }   /* the flag can turn on now */

        memcpy(lastAerialPush, &rayPush, sizeof(rayPush));
        aerialBuilt = true;
    }
}

/* Called while the lighting info is built (lightning.c). */
void VANILLA_VK_fillEnvironmentInfo(VANILLA_GPULightingInfo* info, uint32_t sunIndex) {
    const bool on = environmentActive && environmentLighting && iblBuilt;

    if (on) { info->flags |= VANILLA_LIGHTINGFLAG_ENVIRONMENT; }
    info->envIntensity = environmentIntensity;
    info->envMaxLod    = (float)(prefilterMipCount - 1);
    info->sunIndex     = sunIndex;

    if (environmentActive && aerialActive && aerialBuilt) {
        info->flags |= VANILLA_LIGHTINGFLAG_AERIAL;
        info->aerialKmPerUnit = atmosphere.metersPerUnit * 0.001f * atmosphere.aerialPerspectiveScale;
    }
}

/* Bindings 15 / 16 of the graphics and compute sets (placeholders when inactive). */
void VANILLA_VK_getEnvironmentImageInfos(VkDescriptorImageInfo* outSpecular, VkDescriptorImageInfo* outBrdf, VkDescriptorImageInfo* outAerial) {
    *outSpecular = (VkDescriptorImageInfo){ envSampler, prefilterCube.sampledView, VK_IMAGE_LAYOUT_GENERAL };
    *outBrdf     = (VkDescriptorImageInfo){ envSampler, brdfLut.sampledView,       VK_IMAGE_LAYOUT_GENERAL };
    *outAerial   = (VkDescriptorImageInfo){ envSampler, aerialVolume.sampledView,  VK_IMAGE_LAYOUT_GENERAL };
}

/*
 * Inside the HDR pipeline's rendering, after its entities (viewport already set).
 * Only the first HDR pipeline gets the sky.
 */
void VANILLA_VK_recordSky(VkCommandBuffer commandBuffer, uint16_t pipelineIndex) {
    if (!environmentActive || !skyVisible || skyPipeline == VK_NULL_HANDLE) { return; }
    if ((int32_t)pipelineIndex != VANILLA_VK_firstHdrPipeline()) { return; }

    const VANILLA_SkyPush push = VANILLA_VK_cameraRayPush();

    vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, skyPipeline);
    vkCmdBindDescriptorSets(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS,
        skyPipelineLayout, 0, 1, &skyDescriptorSet, 0, NULL);
    vkCmdPushConstants(commandBuffer, skyPipelineLayout, VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof(push), &push);
    vkCmdDraw(commandBuffer, 3, 1, 0, 0);
}

/* --------------------------------------------------------- */
/* API                                                        */
/* --------------------------------------------------------- */

VANILLA_EXPORT void VANILLA_setSkyVisible(bool visible) { skyVisible = visible; }
VANILLA_EXPORT bool VANILLA_isSkyVisible(void)          { return skyVisible; }

VANILLA_EXPORT void VANILLA_setEnvironmentLighting(bool enabled) {
    if (environmentLighting == enabled) { return; }
    environmentLighting = enabled;
    if (enabled) { iblDirty = true; }   /* the sun may have moved while it was off */
    VANILLA_VK_markLightingInfoDirty();
}

VANILLA_EXPORT bool VANILLA_isEnvironmentLighting(void) { return environmentLighting; }

VANILLA_EXPORT void VANILLA_setEnvironmentIntensity(float intensity) {
    if (!(intensity >= 0.0f)) { intensity = 0.0f; }   /* also NaN */
    environmentIntensity = intensity;
    VANILLA_VK_markLightingInfoDirty();
}

VANILLA_EXPORT float VANILLA_getEnvironmentIntensity(void) { return environmentIntensity; }

static float VANILLA_VK_envClampMin(float value, float minimum) {
    return (value > minimum) ? value : minimum;   /* also turns NaN into minimum */
}

VANILLA_EXPORT VANILLA_Atmosphere VANILLA_getDefaultAtmosphere(void) {
    return VANILLA_VK_defaultAtmosphere();
}

VANILLA_EXPORT void VANILLA_setAtmosphere(const VANILLA_Atmosphere* a) {
    if (a == NULL) { return; }

    VANILLA_Atmosphere v = *a;

    v.metersPerUnit       = VANILLA_VK_envClampMin(v.metersPerUnit, 1e-6f);
    v.planetRadius        = VANILLA_VK_envClampMin(v.planetRadius, 1.0f);
    v.atmosphereHeight    = VANILLA_VK_envClampMin(v.atmosphereHeight, 0.1f);
    v.rayleighScaleHeight = VANILLA_VK_envClampMin(v.rayleighScaleHeight, 0.01f);
    v.mieScaleHeight      = VANILLA_VK_envClampMin(v.mieScaleHeight, 0.01f);
    v.mieScattering       = VANILLA_VK_envClampMin(v.mieScattering, 0.0f);
    v.mieAbsorption       = VANILLA_VK_envClampMin(v.mieAbsorption, 0.0f);
    if (!(v.mieAnisotropy > -0.999f)) { v.mieAnisotropy = -0.999f; }
    if (v.mieAnisotropy > 0.999f)     { v.mieAnisotropy = 0.999f; }

    v.aerialPerspectiveScale = VANILLA_VK_envClampMin(v.aerialPerspectiveScale, 0.0f);

    atmosphere    = v;
    atmosphereSet = true;
    lutsDirty     = true;
    VANILLA_VK_markLightingInfoDirty();   /* aerial scale / metersPerUnit live in the lighting info */
}

VANILLA_EXPORT VANILLA_Atmosphere VANILLA_getAtmosphere(void) {
    return atmosphereSet ? atmosphere : VANILLA_VK_defaultAtmosphere();
}
