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
 * Lighting
 *
 * Point and spot lights are baked into a light volume (L1 spherical harmonics,
 * three RGBA16F 3D images: red, green, blue; texel = L00, L1-1, L10, L11).
 * The volume is split into bricks of 8^3 voxels (8x8x1 in 2D). Changing a light
 * marks the bricks under its old and new box as dirty; only dirty bricks are
 * rebuilt by the bake compute shader, each from scratch.
 *
 * Directional lights are not baked. They live in the lighting info buffer and
 * are evaluated directly in the fragment shader.
 *
 * 2D builds: the volume is X x Y x 1, the sprite plane is z = 0 and
 * light.position.z is the light's height above that plane.
 */

#include "vanilla_window_vulkan_utils.h"
#include "vanilla_window_lightbake_spv.h"
#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

/* --------------------------------------------------------- */
/* Light volume configuration                                 */
/* --------------------------------------------------------- */

#define VANILLA_LIGHT_BRICK_SIZE      8
#define VANILLA_LIGHT_VOLUME_MAX_RES  512

Vec3f    lightVolumeMin           = { -64.0f, -64.0f, -64.0f };
Vec3f    lightVolumeMax           = {  64.0f,  64.0f,  64.0f };
uint32_t lightVolumeResolution[3] = { 128, 128, 128 };

static bool didsetlightvolumebounds = false;

VANILLA_EXPORT void VANILLA_setLightVolumeBounds(
    Vec3f min,
    Vec3f max,
    const uint32_t resolution[3]
) {
    if (didstart || didsetlightvolumebounds) { return; }
    if (resolution == NULL) { return; }
    if (max.x <= min.x || max.y <= min.y || max.z <= min.z) { return; }

    uint32_t rounded[3];

    for (int i = 0; i < 3; i++) {
        if (resolution[i] == 0 ||
            resolution[i] > VANILLA_LIGHT_VOLUME_MAX_RES) {
            return;
        }

        rounded[i] =
            (resolution[i] + VANILLA_LIGHT_BRICK_SIZE - 1) /
            VANILLA_LIGHT_BRICK_SIZE * VANILLA_LIGHT_BRICK_SIZE;
    }

    lightVolumeMin = min;
    lightVolumeMax = max;

    for (int i = 0; i < 3; i++) {
        lightVolumeResolution[i] = rounded[i];
    }

    didsetlightvolumebounds = true;
}

/* --------------------------------------------------------- */
/* On / off switches                                          */
/* --------------------------------------------------------- */

/*
 * lightingSupported: module-level, before start. false = no lighting resources are
 *   created (tiny placeholders keep the shader bindings valid), nothing lighting
 *   related runs per frame, the scene is drawn unlit.
 * lightingEnabled / shadowsEnabled: runtime. Shadows only run while lighting is on;
 *   turning lighting back on restores the user's shadow setting.
 */
static bool lightingSupported   = true;
static bool lightingEnabled     = true;
static bool shadowsEnabled      = true;
static bool didsetlightingsupport = false;

VANILLA_EXPORT void VANILLA_setLightingSupport(bool supported) {
    if (didstart || didsetlightingsupport) { return; }
    lightingSupported = supported;
    didsetlightingsupport = true;
}

bool VANILLA_VK_isLightingSupported(void) { return lightingSupported; }

/* --------------------------------------------------------- */
/* Limits                                                     */
/* --------------------------------------------------------- */

uint32_t lightLimit            = 512;
uint32_t directionalLightLimit = 1;

static bool didsetlightlimit            = false;
static bool didsetdirectionallightlimit = false;

VANILLA_EXPORT void VANILLA_setLightLimit(uint32_t limit) {
    if (didstart || didsetlightlimit) { return; }
    if (limit == 0) { return; }

    lightLimit = limit;
    didsetlightlimit = true;
}

VANILLA_EXPORT void VANILLA_setDirectionalLightLimit(uint32_t limit) {
    if (didstart || didsetdirectionallightlimit) { return; }
    if (limit == 0 || limit > VANILLA_DIRECTIONAL_LIGHT_MAX_LIMIT) { return; }

    directionalLightLimit = limit;
    didsetdirectionallightlimit = true;
}

/* --------------------------------------------------------- */
/* Custom bake shader                                         */
/* --------------------------------------------------------- */

/*
 * Optional replacement for the embedded bake shader. Must keep the same
 * interface as shaders/lightbake.comp (bindings 0-3, push constant, 8x8x1).
 * The code is copied, so the caller's buffer can be freed right away.
 */
static uint32_t* customBakeShader     = NULL;
static size_t    customBakeShaderSize = 0;

VANILLA_EXPORT void VANILLA_setLightBakeShader(const uint32_t* code, size_t size) {
    if (didstart || customBakeShader != NULL) { return; }
    if (code == NULL || size == 0 || (size % sizeof(uint32_t)) != 0) { return; }

    uint32_t* copy = VANILLA_mmap(size);
    if (copy == VANILLA_MAP_FAILED) { return; }

    memcpy(copy, code, size);

    customBakeShader     = copy;
    customBakeShaderSize = size;
}

static void VANILLA_VK_freeCustomBakeShader(void) {
    if (customBakeShader != NULL) {
        VANILLA_munmap(customBakeShader, customBakeShaderSize);
        customBakeShader     = NULL;
        customBakeShaderSize = 0;
    }
}

/* --------------------------------------------------------- */
/* GPU layouts                                                */
/* --------------------------------------------------------- */

/* VANILLA_GPULightingInfo lives in vanilla_window_vulkan_utils.h (shared with shadows). */

_Static_assert(sizeof(VANILLA_GPULight) == 64, "VANILLA_GPULight must be 64 bytes");
_Static_assert(sizeof(VANILLA_GPULightingInfo) == 5024, "VANILLA_GPULightingInfo must be 5024 bytes (4864 CPU + 160 GPU-written)");

typedef struct {
    uint32_t brickOffset;
    uint32_t brickCount;
} VANILLA_LightBakePush;

/* --------------------------------------------------------- */
/* CPU-side state                                             */
/* --------------------------------------------------------- */

/*
 * lights[]            CPU copy of the GPU light array (lightLimit entries).
 * lightCount          highest used slot + 1 (bake loops up to it).
 * directionalSlots[]  slots of the live directional lights (compact).
 * dirtyLightSlots[]   slots changed since the last upload (deduped by bitset).
 * dirtyBrickList[]    bricks to rebake this frame (deduped by bitset).
 */
static VANILLA_GPULight* lights             = NULL;
static uint16_t*         lightGenerations   = NULL;
static uint32_t*         lightFreeSlots     = NULL;
static uint32_t          lightFreeSlotCount = 0;
static uint32_t          lightCount         = 0;

static uint32_t*         directionalSlots   = NULL;
static uint32_t          directionalCount   = 0;

static uint32_t*         dirtyLightSlots    = NULL;
static uint32_t*         dirtyLightBits     = NULL;
static uint32_t          dirtyLightCount    = 0;

static bool              is2D               = false;
static uint32_t          brickCounts[3]     = {0};
static uint32_t          totalBrickCount    = 0;

static uint32_t*         dirtyBrickList     = NULL;
static uint32_t*         dirtyBrickBits     = NULL;
static uint32_t          dirtyBrickCount    = 0;

static uint8_t*          lightCastShadows   = NULL;  /* per slot */
static float*            lightSourceSizes   = NULL;  /* per slot */
static uint32_t          shadowLightSlot    = UINT32_MAX;   /* directional */

/* Shadowed point / spot lights: not baked, evaluated per pixel with their own shadow maps. */
static uint32_t          localShadowSlots[VANILLA_LOCAL_SHADOW_MAX];
static uint32_t          localShadowCount   = 0;

static Vec3f             ambientLight       = { 0.1f, 0.1f, 0.1f };
static bool              lightingInfoDirty  = true;

/* --------------------------------------------------------- */
/* GPU-side state                                             */
/* --------------------------------------------------------- */

static VkBuffer       lightBuffer        = VK_NULL_HANDLE;
static VkDeviceMemory lightMemory        = VK_NULL_HANDLE;

static VkBuffer       lightingInfoBuffer = VK_NULL_HANDLE;
static VkDeviceMemory lightingInfoMemory = VK_NULL_HANDLE;

static VkBuffer       brickListBuffer    = VK_NULL_HANDLE;
static VkDeviceMemory brickListMemory    = VK_NULL_HANDLE;

static VkImage        shImages[3]        = {0};
static VkDeviceMemory shMemories[3]      = {0};
static VkImageView    shViews[3]         = {0};
static VkSampler      lightVolumeSampler = VK_NULL_HANDLE;

static VkDescriptorSetLayout bakeSetLayout      = VK_NULL_HANDLE;
static VkDescriptorPool      bakeDescriptorPool = VK_NULL_HANDLE;
static VkDescriptorSet       bakeDescriptorSet  = VK_NULL_HANDLE;
static VkPipelineLayout      bakePipelineLayout = VK_NULL_HANDLE;
static VkPipeline            bakePipeline       = VK_NULL_HANDLE;

static uint32_t maxBakeGroupCountX = 65535;

/* --------------------------------------------------------- */
/* Dirty tracking                                             */
/* --------------------------------------------------------- */

static inline uint32_t VANILLA_VK_lightBitWords(void) {
    return (lightLimit + 31) / 32;
}

static inline uint32_t VANILLA_VK_brickBitWords(void) {
    return (totalBrickCount + 31) / 32;
}

static void VANILLA_VK_markLightDirty(uint32_t slot) {
    uint32_t word = slot >> 5;
    uint32_t bit  = 1u << (slot & 31);

    if (dirtyLightBits[word] & bit) { return; }

    dirtyLightBits[word] |= bit;
    dirtyLightSlots[dirtyLightCount++] = slot;
}

static void VANILLA_VK_markBrickDirty(uint32_t brick) {
    uint32_t word = brick >> 5;
    uint32_t bit  = 1u << (brick & 31);

    if (dirtyBrickBits[word] & bit) { return; }

    dirtyBrickBits[word] |= bit;
    dirtyBrickList[dirtyBrickCount++] = brick;
}

/*
 * Marks every brick touched by the light's influence box.
 * Directional lights only mark the lighting info buffer.
 */
static void VANILLA_VK_markLightArea(const VANILLA_GPULight* light) {
    if (light->type == VANILLA_LIGHTTYPE_DIRECTIONAL) {
        lightingInfoDirty = true;
        return;
    }

    const float r = light->range;

    const float lo[3] = { light->position.x - r, light->position.y - r, light->position.z - r };
    const float hi[3] = { light->position.x + r, light->position.y + r, light->position.z + r };
    const float mn[3] = { lightVolumeMin.x, lightVolumeMin.y, lightVolumeMin.z };
    const float mx[3] = { lightVolumeMax.x, lightVolumeMax.y, lightVolumeMax.z };

    uint32_t b0[3] = {0};
    uint32_t b1[3] = {0};

    /* 2D: z is the light's height, not a volume axis. */
    const int axes = is2D ? 2 : 3;

    for (int a = 0; a < axes; a++) {
        if (hi[a] < mn[a] || lo[a] > mx[a]) { return; }

        const uint32_t res   = lightVolumeResolution[a];
        const float    scale = (float)res / (mx[a] - mn[a]);

        float v0 = (lo[a] - mn[a]) * scale;
        float v1 = (hi[a] - mn[a]) * scale;

        if (v0 < 0.0f) { v0 = 0.0f; }
        if (v1 < 0.0f) { v1 = 0.0f; }
        if (v1 > (float)(res - 1)) { v1 = (float)(res - 1); }
        if (v0 > v1) { v0 = v1; }

        b0[a] = (uint32_t)v0 / VANILLA_LIGHT_BRICK_SIZE;
        b1[a] = (uint32_t)v1 / VANILLA_LIGHT_BRICK_SIZE;
    }

    for (uint32_t bz = b0[2]; bz <= b1[2]; bz++) {
        for (uint32_t by = b0[1]; by <= b1[1]; by++) {
            for (uint32_t bx = b0[0]; bx <= b1[0]; bx++) {
                VANILLA_VK_markBrickDirty((bz * brickCounts[1] + by) * brickCounts[0] + bx);
            }
        }
    }
}

/* --------------------------------------------------------- */
/* GPU resource helpers                                       */
/* --------------------------------------------------------- */

static int VANILLA_VK_createLightDeviceBuffer(VkDeviceSize size, VkBufferUsageFlags usage, VkBuffer* outBuffer, VkDeviceMemory* outMemory) {
    VkBufferCreateInfo createInfo = {
        .sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO,
        .size = size,
        .usage = usage,
        .sharingMode = VK_SHARING_MODE_EXCLUSIVE
    };

    if (vkCreateBuffer(vulkan.device, &createInfo, NULL, outBuffer) != VK_SUCCESS) {
        *outBuffer = VK_NULL_HANDLE;
        return 1;
    }

    VkMemoryRequirements requirements;
    vkGetBufferMemoryRequirements(vulkan.device, *outBuffer, &requirements);

    *outMemory = VANILLA_gmmap(&requirements, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
    if (*outMemory == VK_NULL_HANDLE) { return 1; }

    if (vkBindBufferMemory(vulkan.device, *outBuffer, *outMemory, 0) != VK_SUCCESS) { return 1; }

    return 0;
}

static int VANILLA_VK_createLightVolumeImages(void) {
    for (int i = 0; i < 3; i++) {
        VkImageCreateInfo imageInfo = {
            .sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO,
            .imageType = VK_IMAGE_TYPE_3D,
            .format = VK_FORMAT_R16G16B16A16_SFLOAT,
            .extent = {
                lightVolumeResolution[0],
                lightVolumeResolution[1],
                lightVolumeResolution[2]
            },
            .mipLevels = 1,
            .arrayLayers = 1,
            .samples = VK_SAMPLE_COUNT_1_BIT,
            .tiling = VK_IMAGE_TILING_OPTIMAL,
            .usage = VK_IMAGE_USAGE_STORAGE_BIT |
                     VK_IMAGE_USAGE_SAMPLED_BIT |
                     VK_IMAGE_USAGE_TRANSFER_DST_BIT,
            .sharingMode = VK_SHARING_MODE_EXCLUSIVE,
            .initialLayout = VK_IMAGE_LAYOUT_UNDEFINED
        };

        if (vkCreateImage(vulkan.device, &imageInfo, NULL, &shImages[i]) != VK_SUCCESS) {
            shImages[i] = VK_NULL_HANDLE;
            return 1;
        }

        VkMemoryRequirements requirements;
        vkGetImageMemoryRequirements(vulkan.device, shImages[i], &requirements);

        shMemories[i] = VANILLA_gmmap(&requirements, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
        if (shMemories[i] == VK_NULL_HANDLE) { return 1; }

        if (vkBindImageMemory(vulkan.device, shImages[i], shMemories[i], 0) != VK_SUCCESS) { return 1; }

        VkImageViewCreateInfo viewInfo = {
            .sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO,
            .image = shImages[i],
            .viewType = VK_IMAGE_VIEW_TYPE_3D,
            .format = VK_FORMAT_R16G16B16A16_SFLOAT,
            .subresourceRange = {
                .aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
                .baseMipLevel = 0,
                .levelCount = 1,
                .baseArrayLayer = 0,
                .layerCount = 1
            }
        };

        if (vkCreateImageView(vulkan.device, &viewInfo, NULL, &shViews[i]) != VK_SUCCESS) {
            shViews[i] = VK_NULL_HANDLE;
            return 1;
        }
    }

    /* Outside the volume = no baked light (transparent black border). */
    VkSamplerCreateInfo samplerInfo = {
        .sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO,
        .magFilter = VK_FILTER_LINEAR,
        .minFilter = VK_FILTER_LINEAR,
        .mipmapMode = VK_SAMPLER_MIPMAP_MODE_NEAREST,
        .addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_BORDER,
        .addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_BORDER,
        .addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE,
        .borderColor = VK_BORDER_COLOR_FLOAT_TRANSPARENT_BLACK,
        .maxLod = 0.0f
    };

    /* 3D builds also need the border on W. 2D samples the single slice. */
    if (!is2D) { samplerInfo.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_BORDER; }

    if (vkCreateSampler(vulkan.device, &samplerInfo, NULL, &lightVolumeSampler) != VK_SUCCESS) {
        lightVolumeSampler = VK_NULL_HANDLE;
        return 1;
    }

    return 0;
}

/* Moves the volume images to GENERAL (kept forever) and zero-fills everything. */
static int VANILLA_VK_clearLightResources(void) {
    VkCommandBuffer commandBuffer = VANILLA_VK_beginSingleTimeCommands();
    if (commandBuffer == VK_NULL_HANDLE) { return 1; }

    VkImageMemoryBarrier2 toGeneral[3];
    for (int i = 0; i < 3; i++) {
        toGeneral[i] = (VkImageMemoryBarrier2){
            .sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2,
            .srcStageMask = VK_PIPELINE_STAGE_2_NONE,
            .srcAccessMask = VK_ACCESS_2_NONE,
            .dstStageMask = VK_PIPELINE_STAGE_2_TRANSFER_BIT,
            .dstAccessMask = VK_ACCESS_2_TRANSFER_WRITE_BIT,
            .oldLayout = VK_IMAGE_LAYOUT_UNDEFINED,
            .newLayout = VK_IMAGE_LAYOUT_GENERAL,
            .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
            .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
            .image = shImages[i],
            .subresourceRange = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1 }
        };
    }

    VkDependencyInfo toGeneralInfo = {
        .sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO,
        .imageMemoryBarrierCount = 3,
        .pImageMemoryBarriers = toGeneral
    };
    vkCmdPipelineBarrier2(commandBuffer, &toGeneralInfo);

    VkClearColorValue zero = {0};
    VkImageSubresourceRange range = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1 };

    for (int i = 0; i < 3; i++) {
        vkCmdClearColorImage(commandBuffer, shImages[i], VK_IMAGE_LAYOUT_GENERAL, &zero, 1, &range);
    }

    vkCmdFillBuffer(commandBuffer, lightBuffer, 0, VK_WHOLE_SIZE, 0);
    vkCmdFillBuffer(commandBuffer, lightingInfoBuffer, 0, VK_WHOLE_SIZE, 0);

    VkMemoryBarrier2 done = {
        .sType = VK_STRUCTURE_TYPE_MEMORY_BARRIER_2,
        .srcStageMask = VK_PIPELINE_STAGE_2_TRANSFER_BIT,
        .srcAccessMask = VK_ACCESS_2_TRANSFER_WRITE_BIT,
        .dstStageMask = VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT | VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT,
        .dstAccessMask = VK_ACCESS_2_SHADER_READ_BIT | VK_ACCESS_2_SHADER_WRITE_BIT
    };

    VkDependencyInfo doneInfo = {
        .sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO,
        .memoryBarrierCount = 1,
        .pMemoryBarriers = &done
    };
    vkCmdPipelineBarrier2(commandBuffer, &doneInfo);

    return VANILLA_VK_endSingleTimeCommands(commandBuffer);
}

static int VANILLA_VK_createLightBakePipeline(void) {
    VkDescriptorSetLayoutBinding bindings[4] = {
        { .binding = 0, .descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, .descriptorCount = 1, .stageFlags = VK_SHADER_STAGE_COMPUTE_BIT },
        { .binding = 1, .descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, .descriptorCount = 1, .stageFlags = VK_SHADER_STAGE_COMPUTE_BIT },
        { .binding = 2, .descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, .descriptorCount = 1, .stageFlags = VK_SHADER_STAGE_COMPUTE_BIT },
        { .binding = 3, .descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE,  .descriptorCount = 3, .stageFlags = VK_SHADER_STAGE_COMPUTE_BIT }
    };

    VkDescriptorSetLayoutCreateInfo layoutInfo = {
        .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO,
        .bindingCount = 4,
        .pBindings = bindings
    };

    if (vkCreateDescriptorSetLayout(vulkan.device, &layoutInfo, NULL, &bakeSetLayout) != VK_SUCCESS) {
        bakeSetLayout = VK_NULL_HANDLE;
        return 1;
    }

    VkDescriptorPoolSize poolSizes[2] = {
        { .type = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, .descriptorCount = 3 },
        { .type = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE,  .descriptorCount = 3 }
    };

    VkDescriptorPoolCreateInfo poolInfo = {
        .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO,
        .maxSets = 1,
        .poolSizeCount = 2,
        .pPoolSizes = poolSizes
    };

    if (vkCreateDescriptorPool(vulkan.device, &poolInfo, NULL, &bakeDescriptorPool) != VK_SUCCESS) {
        bakeDescriptorPool = VK_NULL_HANDLE;
        return 1;
    }

    VkDescriptorSetAllocateInfo allocInfo = {
        .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO,
        .descriptorPool = bakeDescriptorPool,
        .descriptorSetCount = 1,
        .pSetLayouts = &bakeSetLayout
    };

    if (vkAllocateDescriptorSets(vulkan.device, &allocInfo, &bakeDescriptorSet) != VK_SUCCESS) { return 1; }

    VkDescriptorBufferInfo bufferInfos[3] = {
        { .buffer = lightBuffer,        .offset = 0, .range = VK_WHOLE_SIZE },
        { .buffer = lightingInfoBuffer, .offset = 0, .range = VK_WHOLE_SIZE },
        { .buffer = brickListBuffer,    .offset = 0, .range = VK_WHOLE_SIZE }
    };

    VkDescriptorImageInfo imageInfos[3];
    for (int i = 0; i < 3; i++) {
        imageInfos[i] = (VkDescriptorImageInfo){
            .sampler = VK_NULL_HANDLE,
            .imageView = shViews[i],
            .imageLayout = VK_IMAGE_LAYOUT_GENERAL
        };
    }

    VkWriteDescriptorSet writes[4] = {0};
    for (int i = 0; i < 3; i++) {
        writes[i].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        writes[i].dstSet = bakeDescriptorSet;
        writes[i].dstBinding = (uint32_t)i;
        writes[i].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
        writes[i].descriptorCount = 1;
        writes[i].pBufferInfo = &bufferInfos[i];
    }

    writes[3].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    writes[3].dstSet = bakeDescriptorSet;
    writes[3].dstBinding = 3;
    writes[3].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
    writes[3].descriptorCount = 3;
    writes[3].pImageInfo = imageInfos;

    vkUpdateDescriptorSets(vulkan.device, 4, writes, 0, NULL);

    VkPushConstantRange pushRange = {
        .stageFlags = VK_SHADER_STAGE_COMPUTE_BIT,
        .offset = 0,
        .size = sizeof(VANILLA_LightBakePush)
    };

    VkPipelineLayoutCreateInfo pipelineLayoutInfo = {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO,
        .setLayoutCount = 1,
        .pSetLayouts = &bakeSetLayout,
        .pushConstantRangeCount = 1,
        .pPushConstantRanges = &pushRange
    };

    if (vkCreatePipelineLayout(vulkan.device, &pipelineLayoutInfo, NULL, &bakePipelineLayout) != VK_SUCCESS) {
        bakePipelineLayout = VK_NULL_HANDLE;
        return 1;
    }

    /* User-provided bake shader if given, otherwise the embedded default. */
    VkShaderModuleCreateInfo shaderInfo = {
        .sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO,
        .codeSize = VANILLA_lightBakeSpv_len * sizeof(uint32_t),
        .pCode = VANILLA_lightBakeSpv
    };

    if (customBakeShader != NULL) {
        shaderInfo.codeSize = customBakeShaderSize;
        shaderInfo.pCode    = customBakeShader;
    }

    VkShaderModule shaderModule = VK_NULL_HANDLE;
    VkResult moduleResult = vkCreateShaderModule(vulkan.device, &shaderInfo, NULL, &shaderModule);

    /* The copy is no longer needed once the module exists (or failed). */
    VANILLA_VK_freeCustomBakeShader();

    if (moduleResult != VK_SUCCESS) { return 1; }

    VkComputePipelineCreateInfo pipelineInfo = {
        .sType = VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO,
        .stage = {
            .sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
            .stage = VK_SHADER_STAGE_COMPUTE_BIT,
            .module = shaderModule,
            .pName = "main"
        },
        .layout = bakePipelineLayout,
        .basePipelineIndex = -1
    };

    VkResult result = vkCreateComputePipelines(vulkan.device, VK_NULL_HANDLE, 1, &pipelineInfo, NULL, &bakePipeline);
    vkDestroyShaderModule(vulkan.device, shaderModule, NULL);

    if (result != VK_SUCCESS) {
        bakePipeline = VK_NULL_HANDLE;
        return 1;
    }

    return 0;
}

/* --------------------------------------------------------- */
/* Initialization / destruction                               */
/* --------------------------------------------------------- */

void VANILLA_VK_destroyLightning(void);

int VANILLA_VK_initLightning(void) {
    is2D = (GET_DIMENSION(ANYCOREBuildID) == 2);

    if (!lightingSupported) {
        /* Placeholders only: one brick, tiny buffers, no bake pipeline. */
        lightVolumeResolution[0] = VANILLA_LIGHT_BRICK_SIZE;
        lightVolumeResolution[1] = VANILLA_LIGHT_BRICK_SIZE;
        lightVolumeResolution[2] = VANILLA_LIGHT_BRICK_SIZE;
        lightLimit = 1;
        directionalLightLimit = 1;
    }

    if (is2D) {
        /* One slice; z of the bounds is irrelevant. */
        lightVolumeResolution[2] = 1;
        lightVolumeMin.z = -0.5f;
        lightVolumeMax.z =  0.5f;
    }

    brickCounts[0]  = lightVolumeResolution[0] / VANILLA_LIGHT_BRICK_SIZE;
    brickCounts[1]  = lightVolumeResolution[1] / VANILLA_LIGHT_BRICK_SIZE;
    brickCounts[2]  = is2D ? 1 : lightVolumeResolution[2] / VANILLA_LIGHT_BRICK_SIZE;
    totalBrickCount = brickCounts[0] * brickCounts[1] * brickCounts[2];

    lights           = VANILLA_mmap(sizeof(VANILLA_GPULight) * lightLimit);
    lightGenerations = VANILLA_mmap(sizeof(uint16_t) * lightLimit);
    lightFreeSlots   = VANILLA_mmap(sizeof(uint32_t) * lightLimit);
    directionalSlots = VANILLA_mmap(sizeof(uint32_t) * directionalLightLimit);
    dirtyLightSlots  = VANILLA_mmap(sizeof(uint32_t) * lightLimit);
    dirtyLightBits   = VANILLA_mmap(sizeof(uint32_t) * VANILLA_VK_lightBitWords());
    dirtyBrickList   = VANILLA_mmap(sizeof(uint32_t) * totalBrickCount);
    dirtyBrickBits   = VANILLA_mmap(sizeof(uint32_t) * VANILLA_VK_brickBitWords());
    lightCastShadows = VANILLA_mmap(sizeof(uint8_t) * lightLimit);
    lightSourceSizes = VANILLA_mmap(sizeof(float) * lightLimit);

    if (lightCastShadows == VANILLA_MAP_FAILED ||
        lightSourceSizes == VANILLA_MAP_FAILED ||
        lights           == VANILLA_MAP_FAILED ||
        lightGenerations == VANILLA_MAP_FAILED ||
        lightFreeSlots   == VANILLA_MAP_FAILED ||
        directionalSlots == VANILLA_MAP_FAILED ||
        dirtyLightSlots  == VANILLA_MAP_FAILED ||
        dirtyLightBits   == VANILLA_MAP_FAILED ||
        dirtyBrickList   == VANILLA_MAP_FAILED ||
        dirtyBrickBits   == VANILLA_MAP_FAILED) {
        goto fail;
    }

    /* Reverse fill: first allocation receives slot 0. */
    for (uint32_t i = 0; i < lightLimit; i++) {
        lightFreeSlots[i] = lightLimit - 1 - i;
    }

    lightFreeSlotCount = lightLimit;
    lightCount         = 0;
    directionalCount   = 0;
    dirtyLightCount    = 0;
    dirtyBrickCount    = 0;
    lightingInfoDirty  = true;
    shadowLightSlot    = UINT32_MAX;
    localShadowCount   = 0;

    VkPhysicalDeviceProperties properties;
    vkGetPhysicalDeviceProperties(vulkan.physicalDevice, &properties);
    maxBakeGroupCountX = properties.limits.maxComputeWorkGroupCount[0];

    if (VANILLA_VK_createLightDeviceBuffer(
            sizeof(VANILLA_GPULight) * lightLimit,
            VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT,
            &lightBuffer, &lightMemory)) { goto fail; }

    if (VANILLA_VK_createLightDeviceBuffer(
            sizeof(VANILLA_GPULightingInfo),
            VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT,
            &lightingInfoBuffer, &lightingInfoMemory)) { goto fail; }

    if (VANILLA_VK_createLightDeviceBuffer(
            sizeof(uint32_t) * totalBrickCount,
            VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT,
            &brickListBuffer, &brickListMemory)) { goto fail; }

    if (VANILLA_VK_createLightVolumeImages()) { goto fail; }
    if (VANILLA_VK_clearLightResources())     { goto fail; }
    if (lightingSupported) {
        if (VANILLA_VK_createLightBakePipeline()) { goto fail; }
    }

    return 0;

fail:
    printf("[VANILLA WINDOW] lighting init failed\n");
    VANILLA_VK_destroyLightning();
    return 1;
}

#define VANILLA_LIGHT_UNMAP(ptr, size)                              \
    do {                                                            \
        if ((ptr) != NULL && (void*)(ptr) != VANILLA_MAP_FAILED) {  \
            VANILLA_munmap((ptr), (size));                          \
        }                                                           \
        (ptr) = NULL;                                               \
    } while (0)

static void VANILLA_VK_destroyLightBuffer(VkBuffer* buffer, VkDeviceMemory* memory) {
    if (*buffer != VK_NULL_HANDLE) {
        vkDestroyBuffer(vulkan.device, *buffer, NULL);
        *buffer = VK_NULL_HANDLE;
    }
    if (*memory != VK_NULL_HANDLE) {
        VANILLA_gmunmap(*memory);
        *memory = VK_NULL_HANDLE;
    }
}

void VANILLA_VK_destroyLightning(void) {
    VANILLA_VK_freeCustomBakeShader();

    if (bakePipeline != VK_NULL_HANDLE) {
        vkDestroyPipeline(vulkan.device, bakePipeline, NULL);
        bakePipeline = VK_NULL_HANDLE;
    }
    if (bakePipelineLayout != VK_NULL_HANDLE) {
        vkDestroyPipelineLayout(vulkan.device, bakePipelineLayout, NULL);
        bakePipelineLayout = VK_NULL_HANDLE;
    }
    if (bakeDescriptorPool != VK_NULL_HANDLE) {
        vkDestroyDescriptorPool(vulkan.device, bakeDescriptorPool, NULL);
        bakeDescriptorPool = VK_NULL_HANDLE;
        bakeDescriptorSet  = VK_NULL_HANDLE;
    }
    if (bakeSetLayout != VK_NULL_HANDLE) {
        vkDestroyDescriptorSetLayout(vulkan.device, bakeSetLayout, NULL);
        bakeSetLayout = VK_NULL_HANDLE;
    }

    if (lightVolumeSampler != VK_NULL_HANDLE) {
        vkDestroySampler(vulkan.device, lightVolumeSampler, NULL);
        lightVolumeSampler = VK_NULL_HANDLE;
    }

    for (int i = 0; i < 3; i++) {
        if (shViews[i] != VK_NULL_HANDLE) {
            vkDestroyImageView(vulkan.device, shViews[i], NULL);
            shViews[i] = VK_NULL_HANDLE;
        }
        if (shImages[i] != VK_NULL_HANDLE) {
            vkDestroyImage(vulkan.device, shImages[i], NULL);
            shImages[i] = VK_NULL_HANDLE;
        }
        if (shMemories[i] != VK_NULL_HANDLE) {
            VANILLA_gmunmap(shMemories[i]);
            shMemories[i] = VK_NULL_HANDLE;
        }
    }

    VANILLA_VK_destroyLightBuffer(&lightBuffer, &lightMemory);
    VANILLA_VK_destroyLightBuffer(&lightingInfoBuffer, &lightingInfoMemory);
    VANILLA_VK_destroyLightBuffer(&brickListBuffer, &brickListMemory);

    VANILLA_LIGHT_UNMAP(lights,           sizeof(VANILLA_GPULight) * lightLimit);
    VANILLA_LIGHT_UNMAP(lightGenerations, sizeof(uint16_t) * lightLimit);
    VANILLA_LIGHT_UNMAP(lightFreeSlots,   sizeof(uint32_t) * lightLimit);
    VANILLA_LIGHT_UNMAP(directionalSlots, sizeof(uint32_t) * directionalLightLimit);
    VANILLA_LIGHT_UNMAP(dirtyLightSlots,  sizeof(uint32_t) * lightLimit);
    VANILLA_LIGHT_UNMAP(dirtyLightBits,   sizeof(uint32_t) * VANILLA_VK_lightBitWords());
    VANILLA_LIGHT_UNMAP(dirtyBrickList,   sizeof(uint32_t) * totalBrickCount);
    VANILLA_LIGHT_UNMAP(dirtyBrickBits,   sizeof(uint32_t) * VANILLA_VK_brickBitWords());
    VANILLA_LIGHT_UNMAP(lightCastShadows, sizeof(uint8_t) * lightLimit);
    VANILLA_LIGHT_UNMAP(lightSourceSizes, sizeof(float) * lightLimit);
    shadowLightSlot = UINT32_MAX;
    localShadowCount = 0;

    lightFreeSlotCount = 0;
    lightCount         = 0;
    directionalCount   = 0;
    dirtyLightCount    = 0;
    dirtyBrickCount    = 0;
}

/* --------------------------------------------------------- */
/* Descriptor access for the graphics / compute sets          */
/* --------------------------------------------------------- */

VkBuffer VANILLA_VK_getLightBuffer(void)        { return lightBuffer; }
VkBuffer VANILLA_VK_getLightingInfoBuffer(void) { return lightingInfoBuffer; }

void VANILLA_VK_getLightVolumeImageInfos(VkDescriptorImageInfo out[3]) {
    for (int i = 0; i < 3; i++) {
        out[i].sampler     = lightVolumeSampler;
        out[i].imageView   = shViews[i];
        out[i].imageLayout = VK_IMAGE_LAYOUT_GENERAL;
    }
}

/* --------------------------------------------------------- */
/* Shadow light                                               */
/* --------------------------------------------------------- */

/* The directional light that casts shadows this frame, if any. */
bool VANILLA_VK_getShadowLight(VANILLA_GPULight* outLight, float* outSourceSize) {
    if (lights == NULL || shadowLightSlot == UINT32_MAX) { return false; }

    const VANILLA_GPULight* light = &lights[shadowLightSlot];
    if (light->valid == 0 || light->enabled == 0) { return false; }
    if (light->type != VANILLA_LIGHTTYPE_DIRECTIONAL) { return false; }

    if (outLight)      { *outLight = *light; }
    if (outSourceSize) { *outSourceSize = lightSourceSizes[shadowLightSlot]; }
    return true;
}

/* The sun for the atmosphere: the shadow light, else the first enabled directional light. */
static uint32_t VANILLA_VK_sunSlot(void) {
    if (lights == NULL) { return UINT32_MAX; }
    if (VANILLA_VK_getShadowLight(NULL, NULL)) { return shadowLightSlot; }

    for (uint32_t i = 0; i < directionalCount; i++) {
        const uint32_t slot = directionalSlots[i];
        if (lights[slot].valid != 0 && lights[slot].enabled != 0) { return slot; }
    }
    return UINT32_MAX;
}

bool VANILLA_VK_getSunLight(VANILLA_GPULight* outLight, float* outSourceSize) {
    const uint32_t slot = VANILLA_VK_sunSlot();
    if (slot == UINT32_MAX) { return false; }

    if (outLight)      { *outLight = lights[slot]; }
    if (outSourceSize) { *outSourceSize = lightSourceSizes[slot]; }
    return true;
}

/* Environment settings changed: re-upload the info (flags, intensity) next frame. */
void VANILLA_VK_markLightingInfoDirty(void) { lightingInfoDirty = true; }

/* Gives a point / spot light a local shadow slot (it leaves the baked volume). */
static void VANILLA_VK_assignLocalShadow(uint32_t slot) {
    if ((lights[slot].valid >> VANILLA_LIGHT_SHADOW_SHIFT) != 0) { return; }   /* already has one */
    if (localShadowCount >= VANILLA_VK_getLocalShadowLimit()) { return; }       /* stays baked */

    const uint32_t index = localShadowCount++;
    localShadowSlots[index] = slot;

    lights[slot].valid = VANILLA_LIGHT_VALID_BIT | ((index + 1) << VANILLA_LIGHT_SHADOW_SHIFT);

    VANILLA_VK_markLightArea(&lights[slot]);   /* remove it from the bake */
    VANILLA_VK_markLightDirty(slot);
    lightingInfoDirty = true;
}

/* Returns the light to the baked volume and compacts the local shadow list. */
static void VANILLA_VK_releaseLocalShadow(uint32_t slot) {
    uint32_t index = lights[slot].valid >> VANILLA_LIGHT_SHADOW_SHIFT;
    if (index == 0) { return; }
    index--;

    lights[slot].valid = VANILLA_LIGHT_VALID_BIT;
    VANILLA_VK_markLightArea(&lights[slot]);   /* bake it again */
    VANILLA_VK_markLightDirty(slot);

    const uint32_t last = --localShadowCount;
    if (index != last) {
        const uint32_t moved = localShadowSlots[last];
        localShadowSlots[index] = moved;
        lights[moved].valid = VANILLA_LIGHT_VALID_BIT | ((index + 1) << VANILLA_LIGHT_SHADOW_SHIFT);
        VANILLA_VK_markLightDirty(moved);
    }

    lightingInfoDirty = true;
}

/*
 * Directional: at most one shadow light, a new one takes over from the previous.
 * Point / spot: up to the local shadow limit; beyond it the light stays baked (no shadow).
 */
static void VANILLA_VK_setShadowFlag(uint32_t slot, bool cast) {
    if (lights[slot].type != VANILLA_LIGHTTYPE_DIRECTIONAL) {
        lightCastShadows[slot] = cast ? 1 : 0;
        if (cast) { VANILLA_VK_assignLocalShadow(slot); }
        else      { VANILLA_VK_releaseLocalShadow(slot); }
        return;
    }

    if (cast) {
        if (shadowLightSlot != UINT32_MAX && shadowLightSlot != slot) {
            lightCastShadows[shadowLightSlot] = 0;
        }
        lightCastShadows[slot] = 1;
        shadowLightSlot = slot;
    }
    else {
        lightCastShadows[slot] = 0;
        if (shadowLightSlot == slot) { shadowLightSlot = UINT32_MAX; }
    }

    lightingInfoDirty = true;
}

/* --------------------------------------------------------- */
/* Per-frame recording                                        */
/* --------------------------------------------------------- */

/* vkCmdUpdateBuffer accepts at most 65536 bytes per call. */
static void VANILLA_VK_updateBufferChunked(VkCommandBuffer commandBuffer, VkBuffer buffer, VkDeviceSize offset, VkDeviceSize size, const void* data) {
    const uint8_t* bytes = (const uint8_t*)data;

    while (size > 0) {
        VkDeviceSize chunk = size > 65536 ? 65536 : size;
        vkCmdUpdateBuffer(commandBuffer, buffer, offset, chunk, bytes);

        offset += chunk;
        bytes  += chunk;
        size   -= chunk;
    }
}

static void VANILLA_VK_lightBarrier(VkCommandBuffer commandBuffer, VkPipelineStageFlags2 srcStage, VkAccessFlags2 srcAccess, VkPipelineStageFlags2 dstStage, VkAccessFlags2 dstAccess) {
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

/*
 * Uploads changed lights, the lighting info and the dirty brick list, then
 * rebakes the dirty bricks. Called once per frame before the main barrier.
 */
void VANILLA_VK_recordLighting(VkCommandBuffer commandBuffer) {
    /* Unsupported: the info buffer stays zeroed (flags = 0 = unlit) and nothing runs. */
    if (lights == NULL || bakePipeline == VK_NULL_HANDLE) { return; }

    if (!lightingEnabled) {
        /*
         * Only the flags word changes. Light / brick changes stay queued (their bitsets
         * dedupe them) and are applied when lighting is turned back on.
         */
        VANILLA_VK_clearShadowPasses();

        if (lightingInfoDirty) {
            const uint32_t flags = 0;
            vkCmdUpdateBuffer(commandBuffer, lightingInfoBuffer,
                offsetof(VANILLA_GPULightingInfo, flags), sizeof(flags), &flags);

            VANILLA_VK_lightBarrier(
                commandBuffer,
                VK_PIPELINE_STAGE_2_TRANSFER_BIT,
                VK_ACCESS_2_TRANSFER_WRITE_BIT,
                VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT | VK_PIPELINE_STAGE_2_VERTEX_SHADER_BIT | VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT,
                VK_ACCESS_2_SHADER_STORAGE_READ_BIT);

            lightingInfoDirty = false;
        }
        return;
    }

    const bool shadowsOn = shadowsEnabled;

    VANILLA_GPULight shadowLight;
    float shadowSourceSize = 0.0f;
    const bool hasShadowLight = shadowsOn && VANILLA_VK_getShadowLight(&shadowLight, &shadowSourceSize);

    /* Cascades follow the camera, so the info is re-uploaded every frame while shadows are on. */
    const bool anyWork = dirtyLightCount > 0 || lightingInfoDirty || dirtyBrickCount > 0 || hasShadowLight || localShadowCount > 0;
    if (!anyWork) {
        VANILLA_VK_clearShadowPasses();
        return;
    }

    /* 1. Lights: per slot, or the whole active range when many changed. */
    if (dirtyLightCount > 0) {
        if (dirtyLightCount > lightLimit / 4) {
            uint32_t uploadCount = 0;
            for (uint32_t i = 0; i < dirtyLightCount; i++) {
                if (dirtyLightSlots[i] + 1 > uploadCount) { uploadCount = dirtyLightSlots[i] + 1; }
            }

            VANILLA_VK_updateBufferChunked(
                commandBuffer, lightBuffer, 0,
                sizeof(VANILLA_GPULight) * uploadCount, lights);
        }
        else {
            for (uint32_t i = 0; i < dirtyLightCount; i++) {
                const uint32_t slot = dirtyLightSlots[i];
                vkCmdUpdateBuffer(
                    commandBuffer, lightBuffer,
                    sizeof(VANILLA_GPULight) * slot,
                    sizeof(VANILLA_GPULight), &lights[slot]);
            }
        }

        memset(dirtyLightBits, 0, sizeof(uint32_t) * VANILLA_VK_lightBitWords());
        dirtyLightCount = 0;
    }

    /* 2. Lighting info. lightCount changes with create/destroy, so it rides along. */
    {
        VANILLA_GPULightingInfo info = {0};

        info.volumeMin        = lightVolumeMin;
        info.volumeMax        = lightVolumeMax;
        info.lightCount       = lightCount;
        info.directionalCount = directionalCount;
        info.resolution[0]    = lightVolumeResolution[0];
        info.resolution[1]    = lightVolumeResolution[1];
        info.resolution[2]    = lightVolumeResolution[2];
        info.dimension        = is2D ? 2 : 3;
        info.ambient          = ambientLight;
        info.flags            = VANILLA_LIGHTINGFLAG_LIGHTING | (shadowsOn ? VANILLA_LIGHTINGFLAG_SHADOWS : 0u);

        uint32_t shadowDirectional = UINT32_MAX;

        for (uint32_t i = 0; i < directionalCount; i++) {
            info.directional[i] = lights[directionalSlots[i]];
            if (hasShadowLight && directionalSlots[i] == shadowLightSlot) { shadowDirectional = i; }
        }

        VANILLA_VK_fillShadowInfo(
            &info,
            shadowDirectional != UINT32_MAX ? &shadowLight : NULL,
            shadowDirectional,
            shadowSourceSize);

        info.localShadowCount = localShadowCount;
        for (uint32_t i = 0; i < localShadowCount; i++) {
            info.localLights[i] = lights[localShadowSlots[i]];
            info.localShadowExtra[i][0] = lightSourceSizes[localShadowSlots[i]];
        }

        /* Shadows off: the local lights are still lit per pixel, just without depth passes. */
        if (shadowsOn) { VANILLA_VK_fillLocalShadowInfo(&info); }
        else           { VANILLA_VK_clearShadowPasses(); }

        /* Environment: flag, intensity, which directional light is the atmosphere's sun. */
        {
            const uint32_t sunSlot = VANILLA_VK_sunSlot();
            uint32_t sunIndex = UINT32_MAX;
            for (uint32_t i = 0; i < directionalCount; i++) {
                if (directionalSlots[i] == sunSlot) { sunIndex = i; break; }
            }
            VANILLA_VK_fillEnvironmentInfo(&info, sunIndex);
        }

        /* The GPU part (environment SH, sun transmittance) is written by env_sh.comp: never overwrite it. */
        vkCmdUpdateBuffer(commandBuffer, lightingInfoBuffer, 0, VANILLA_LIGHTINGINFO_CPU_SIZE, &info);
        lightingInfoDirty = false;
    }

    /* 3. Dirty bricks. */
    const uint32_t brickCount = dirtyBrickCount;

    if (brickCount > 0) {
        VANILLA_VK_updateBufferChunked(
            commandBuffer, brickListBuffer, 0,
            sizeof(uint32_t) * brickCount, dirtyBrickList);

        memset(dirtyBrickBits, 0, sizeof(uint32_t) * VANILLA_VK_brickBitWords());
        dirtyBrickCount = 0;
    }

    /* Uploads (and last frame's readers) before the bake. */
    VANILLA_VK_lightBarrier(
        commandBuffer,
        VK_PIPELINE_STAGE_2_TRANSFER_BIT | VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT | VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT,
        VK_ACCESS_2_TRANSFER_WRITE_BIT,
        VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT | VK_PIPELINE_STAGE_2_VERTEX_SHADER_BIT | VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT,
        VK_ACCESS_2_SHADER_STORAGE_READ_BIT | VK_ACCESS_2_SHADER_STORAGE_WRITE_BIT
    );

    if (brickCount == 0) { return; }

    vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_COMPUTE, bakePipeline);
    vkCmdBindDescriptorSets(
        commandBuffer, VK_PIPELINE_BIND_POINT_COMPUTE,
        bakePipelineLayout, 0, 1, &bakeDescriptorSet, 0, NULL);

    const uint32_t slices = is2D ? 1 : VANILLA_LIGHT_BRICK_SIZE;

    for (uint32_t base = 0; base < brickCount; base += maxBakeGroupCountX) {
        uint32_t count = brickCount - base;
        if (count > maxBakeGroupCountX) { count = maxBakeGroupCountX; }

        VANILLA_LightBakePush push = { .brickOffset = base, .brickCount = brickCount };
        vkCmdPushConstants(
            commandBuffer, bakePipelineLayout,
            VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(push), &push);

        vkCmdDispatch(commandBuffer, count, 1, slices);
    }

    /* Baked volume -> fragment sampling and user compute. */
    VANILLA_VK_lightBarrier(
        commandBuffer,
        VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT,
        VK_ACCESS_2_SHADER_STORAGE_WRITE_BIT,
        VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT | VK_PIPELINE_STAGE_2_VERTEX_SHADER_BIT | VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT,
        VK_ACCESS_2_SHADER_SAMPLED_READ_BIT | VK_ACCESS_2_SHADER_STORAGE_READ_BIT
    );
}

/* --------------------------------------------------------- */
/* ID validation                                              */
/* --------------------------------------------------------- */

static VANILLA_GPULight* VANILLA_VK_getLight(LightID id) {
    if (lights == NULL)          { return NULL; }
    if (id.slot >= lightLimit)   { return NULL; }
    if (id.generation == 0)      { return NULL; }
    if (lightGenerations[id.slot] != id.generation) { return NULL; }
    if (lights[id.slot].valid == 0) { return NULL; }

    return &lights[id.slot];
}

/* --------------------------------------------------------- */
/* Public API: create / destroy                               */
/* --------------------------------------------------------- */

VANILLA_EXPORT VANILLA_WINDOW_RESULT VANILLA_createLight(
    const VANILLA_LightCreateInfo* createInfo,
    LightID* outID
) {
    if (!lightingSupported) { return VANILLA_WINDOW_ERR_LIGHTING_UNSUPPORTED; }
    if (didstart == false || lights == NULL) { return VANILLA_WINDOW_ERR_DIDNT_START; }
    if (createInfo == NULL || outID == NULL) { return VANILLA_WINDOW_ERR_NULL_POINTER; }

    switch (createInfo->type) {
        case VANILLA_LIGHTTYPE_DIRECTIONAL:
            if (directionalCount >= directionalLightLimit) {
                return VANILLA_WINDOW_ERR_CAPACITY_REACHED;
            }
            break;

        case VANILLA_LIGHTTYPE_POINT:
            if (createInfo->range <= 0.0f) { return VANILLA_WINDOW_ERR_INVALID_LIGHT; }
            break;

        case VANILLA_LIGHTTYPE_SPOT:
            if (createInfo->range <= 0.0f ||
                createInfo->innerCone <= createInfo->outerCone) {
                return VANILLA_WINDOW_ERR_INVALID_LIGHT;
            }
            break;

        default:
            return VANILLA_WINDOW_ERR_INVALID_LIGHT;
    }

    if (lightFreeSlotCount == 0) { return VANILLA_WINDOW_ERR_CAPACITY_REACHED; }

    uint32_t slot = lightFreeSlots[--lightFreeSlotCount];

    uint16_t generation = ++lightGenerations[slot];
    if (generation == 0) { generation = ++lightGenerations[slot]; }

    VANILLA_GPULight* light = &lights[slot];

    light->position  = createInfo->position;
    light->range     = createInfo->range;
    light->direction = createInfo->direction;
    light->intensity = createInfo->intensity;
    light->color     = createInfo->color;
    light->type      = (uint32_t)createInfo->type;
    light->innerCone = createInfo->innerCone;
    light->outerCone = createInfo->outerCone;
    light->enabled   = 1;
    light->valid     = 1;

    lightSourceSizes[slot] = createInfo->sourceSize > 0.0f ? createInfo->sourceSize : 0.0f;
    lightCastShadows[slot] = 0;
    if (createInfo->castShadows) { VANILLA_VK_setShadowFlag(slot, true); }

    if (createInfo->type == VANILLA_LIGHTTYPE_DIRECTIONAL) {
        directionalSlots[directionalCount++] = slot;
    }

    if (lightCount < slot + 1) { lightCount = slot + 1; }

    VANILLA_VK_markLightDirty(slot);
    VANILLA_VK_markLightArea(light);
    lightingInfoDirty = true;

    outID->slot       = slot;
    outID->generation = generation;

    return VANILLA_WINDOW_SUCCESS;
}

VANILLA_EXPORT VANILLA_WINDOW_RESULT VANILLA_destroyLight(LightID id) {
    if (didstart == false || lights == NULL) { return VANILLA_WINDOW_ERR_DIDNT_START; }

    VANILLA_GPULight* light = VANILLA_VK_getLight(id);
    if (light == NULL) { return VANILLA_WINDOW_ERR_INVALID_LIGHT; }

    VANILLA_VK_markLightArea(light);

    if (light->type == VANILLA_LIGHTTYPE_DIRECTIONAL) {
        for (uint32_t i = 0; i < directionalCount; i++) {
            if (directionalSlots[i] == id.slot) {
                directionalSlots[i] = directionalSlots[--directionalCount];
                break;
            }
        }
    }

    if (lightCastShadows[id.slot]) { VANILLA_VK_setShadowFlag(id.slot, false); }

    light->valid   = 0;
    light->enabled = 0;

    VANILLA_VK_markLightDirty(id.slot);
    lightingInfoDirty = true;

    lightFreeSlots[lightFreeSlotCount++] = id.slot;

    /* Shrink the active range past trailing dead slots. */
    while (lightCount > 0 && lights[lightCount - 1].valid == 0) {
        lightCount--;
    }

    return VANILLA_WINDOW_SUCCESS;
}

/* --------------------------------------------------------- */
/* Public API: setters                                        */
/* --------------------------------------------------------- */

/*
 * Setters that move or resize the light's box mark the old and the new box.
 * The others only mark the current box.
 */

VANILLA_EXPORT void VANILLA_setLightPosition(LightID id, Vec3f position) {
    VANILLA_GPULight* light = VANILLA_VK_getLight(id);
    if (light == NULL) { return; }

    VANILLA_VK_markLightArea(light);
    light->position = position;
    VANILLA_VK_markLightArea(light);

    VANILLA_VK_markLightDirty(id.slot);
}

VANILLA_EXPORT void VANILLA_setLightRange(LightID id, float range) {
    VANILLA_GPULight* light = VANILLA_VK_getLight(id);
    if (light == NULL || range <= 0.0f) { return; }

    VANILLA_VK_markLightArea(light);
    light->range = range;
    VANILLA_VK_markLightArea(light);

    VANILLA_VK_markLightDirty(id.slot);
}

VANILLA_EXPORT void VANILLA_setLightDirection(LightID id, Vec3f direction) {
    VANILLA_GPULight* light = VANILLA_VK_getLight(id);
    if (light == NULL) { return; }

    light->direction = direction;
    VANILLA_VK_markLightArea(light);
    VANILLA_VK_markLightDirty(id.slot);
}

VANILLA_EXPORT void VANILLA_setLightCone(LightID id, float innerCone, float outerCone) {
    VANILLA_GPULight* light = VANILLA_VK_getLight(id);
    if (light == NULL || innerCone <= outerCone) { return; }

    light->innerCone = innerCone;
    light->outerCone = outerCone;
    VANILLA_VK_markLightArea(light);
    VANILLA_VK_markLightDirty(id.slot);
}

VANILLA_EXPORT void VANILLA_setLightColor(LightID id, Vec3f color) {
    VANILLA_GPULight* light = VANILLA_VK_getLight(id);
    if (light == NULL) { return; }

    light->color = color;
    VANILLA_VK_markLightArea(light);
    VANILLA_VK_markLightDirty(id.slot);
}

VANILLA_EXPORT void VANILLA_setLightIntensity(LightID id, float intensity) {
    VANILLA_GPULight* light = VANILLA_VK_getLight(id);
    if (light == NULL) { return; }

    light->intensity = intensity;
    VANILLA_VK_markLightArea(light);
    VANILLA_VK_markLightDirty(id.slot);
}

VANILLA_EXPORT void VANILLA_setLightEnabled(LightID id, bool enabled) {
    VANILLA_GPULight* light = VANILLA_VK_getLight(id);
    if (light == NULL) { return; }

    const uint32_t value = enabled ? 1 : 0;
    if (light->enabled == value) { return; }

    light->enabled = value;
    VANILLA_VK_markLightArea(light);
    VANILLA_VK_markLightDirty(id.slot);
}

VANILLA_EXPORT void VANILLA_setLightCastShadows(LightID id, bool cast) {
    VANILLA_GPULight* light = VANILLA_VK_getLight(id);
    if (light == NULL) { return; }

    VANILLA_VK_setShadowFlag(id.slot, cast);
}

VANILLA_EXPORT void VANILLA_setLightSourceSize(LightID id, float sourceSize) {
    VANILLA_GPULight* light = VANILLA_VK_getLight(id);
    if (light == NULL || sourceSize < 0.0f) { return; }

    lightSourceSizes[id.slot] = sourceSize;
    lightingInfoDirty = true;
}

/* Lighting off = unlit (albedo + emissive); shadows are off too while lighting is off. */
VANILLA_EXPORT void VANILLA_setLightingEnabled(bool enabled) {
    if (lightingEnabled == enabled) { return; }
    lightingEnabled = enabled;
    lightingInfoDirty = true;
}

/* Shadows off: lights keep working, depth passes and shadow lookups stop. */
VANILLA_EXPORT void VANILLA_setShadowsEnabled(bool enabled) {
    if (shadowsEnabled == enabled) { return; }
    shadowsEnabled = enabled;
    lightingInfoDirty = true;
}

VANILLA_EXPORT bool VANILLA_isLightingEnabled(void) { return lightingSupported && lightingEnabled; }
VANILLA_EXPORT bool VANILLA_isShadowsEnabled(void)  { return lightingSupported && lightingEnabled && shadowsEnabled; }

VANILLA_EXPORT void VANILLA_setAmbientLight(Vec3f color) {
    ambientLight = color;
    lightingInfoDirty = true;
}
