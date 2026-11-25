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
 * GPU frustum culling.
 *
 * Every model gets a bounding sphere when it is uploaded (position = first 3 floats of vertex location 0).
 * Before each pass that draws entity lists (every shadow cascade / point-spot face, then the main camera),
 * VANILLA_VK_cullPass runs embedded cull.comp over the opaque and mask lists of the culled pipelines: the
 * visible commands are compacted into the same list region of an output buffer and counted, and the pass draws
 * them with vkCmdDrawIndexedIndirectCount. Passes reuse one output buffer, one after another.
 *
 * FRUSTUM_OCCLUSION: after the scene pipeline's solids (first HDR pipeline) a Hi-Z pyramid is built from its
 * depth (half resolution, farthest depth per texel, every MSAA sample). The next frame's main-camera cull
 * projects each sphere with the view-projection of that frame and drops it when it lies behind the pyramid.
 * Only the scene pipeline's lists are occlusion-tested; shadows stay frustum-only. A frame late by design:
 * something revealed by a fast camera turn may appear one frame later.
 *
 * Not culled: blend lists (sorted), pipelines with skipCulling or preserveDrawOrder, skinned entities
 * (their deformed mesh leaves the bind-pose sphere), models without bounds.
 * Without Vulkan 1.2 drawIndirectCount the mode stays NONE.
 */

#include "vanilla_window_vulkan_utils.h"
#include "vanilla_window_cull_spv.h"
#include <stdint.h>
#include <stdbool.h>
#include <math.h>

#define VANILLA_CULL_NEVER     0x80000000u   /* slot model bit: never cull (skinned) */
#define VANILLA_CULL_MODES     3              /* lists per pipeline (opaque, mask, blend) */

typedef struct {
    float    planes[6][4];
    uint32_t inputOffset;
    uint32_t count;
    uint32_t countIndex;
    uint32_t flags;          /* bit 0: shadow pass */
} VANILLA_CullPush;

static VANILLA_Culling cullingMode = VANILLA_CULLING_FRUSTUM;
static bool            cullingAvailable = false;   /* device + resources */

static uint32_t modelCapacity = 0;
static uint32_t slotCapacity  = 0;

/* Host-visible staging (written by the CPU) and device-local copies (read by cull.comp). */
static VkBuffer       boundsStaging  = VK_NULL_HANDLE;  static VkDeviceMemory boundsStagingMemory = VK_NULL_HANDLE;
static VkBuffer       boundsBuffer   = VK_NULL_HANDLE;  static VkDeviceMemory boundsMemory        = VK_NULL_HANDLE;
static VkBuffer       slotStaging    = VK_NULL_HANDLE;  static VkDeviceMemory slotStagingMemory   = VK_NULL_HANDLE;
static VkBuffer       slotBuffer     = VK_NULL_HANDLE;  static VkDeviceMemory slotMemory          = VK_NULL_HANDLE;
static float*         boundsMapped   = NULL;            /* 4 floats per model */
static uint32_t*      slotMapped     = NULL;            /* 1 uint per entity slot */
static uint32_t       boundsDirtyMin = UINT32_MAX, boundsDirtyMax = 0;
static uint32_t       slotDirtyMin   = UINT32_MAX, slotDirtyMax   = 0;

static VkBuffer       outputBuffer   = VK_NULL_HANDLE;  static VkDeviceMemory outputMemory = VK_NULL_HANDLE;
static VkBuffer       countBuffer    = VK_NULL_HANDLE;  static VkDeviceMemory countMemory  = VK_NULL_HANDLE;
static uint32_t       listCountTotal = 0;

static bool*          listCulled     = NULL;   /* per list, this pass */

/* Hi-Z (occlusion) */
#define VANILLA_HIZ_MAX_MIPS 16

typedef struct {
    int32_t srcSize[2];
    int32_t dstSize[2];
    int32_t samples;
} VANILLA_HiZPush;

/* std430 mirror of Occlusion in cull.comp */
typedef struct {
    mat4    viewProj;
    int32_t size[2];
    int32_t mips;
    int32_t _padding;
} VANILLA_OcclusionParams;

static VkImage        hizImage      = VK_NULL_HANDLE;
static VkDeviceMemory hizMemory     = VK_NULL_HANDLE;
static VkImageView    hizSampled    = VK_NULL_HANDLE;                 /* all mips */
static VkImageView    hizMipViews[VANILLA_HIZ_MAX_MIPS] = {0};
static uint32_t       hizMips       = 0;
static uint32_t       hizSize[2]    = { 0, 0 };                      /* mip 0 */
static VkSampler      hizSampler    = VK_NULL_HANDLE;                 /* nearest, texelFetch */
static bool           hizValid      = false;                          /* built at least once with this size */
static VkBuffer       occlusionBuffer = VK_NULL_HANDLE;
static VkDeviceMemory occlusionMemory = VK_NULL_HANDLE;

static VkDescriptorSetLayout hizInitSetLayout   = VK_NULL_HANDLE;   /* 0 depth (sampled), 1 mip 0 (storage) */
static VkDescriptorSetLayout hizReduceSetLayout = VK_NULL_HANDLE;   /* 0 src mip, 1 dst mip (storage) */
static VkPipelineLayout      hizInitLayout      = VK_NULL_HANDLE;
static VkPipelineLayout      hizReduceLayout    = VK_NULL_HANDLE;
static VkPipeline            hizInitPipeline    = VK_NULL_HANDLE;
static VkPipeline            hizInitMsPipeline  = VK_NULL_HANDLE;
static VkPipeline            hizReducePipeline  = VK_NULL_HANDLE;
static VkDescriptorPool      hizPool            = VK_NULL_HANDLE;   /* recreated with the pyramid */
static VkDescriptorSet       hizInitSet         = VK_NULL_HANDLE;
static VkDescriptorSet       hizReduceSets[VANILLA_HIZ_MAX_MIPS] = {0};

static VkDescriptorSetLayout cullSetLayout      = VK_NULL_HANDLE;
static VkDescriptorPool      cullDescriptorPool = VK_NULL_HANDLE;
static VkDescriptorSet       cullDescriptorSet  = VK_NULL_HANDLE;
static VkPipelineLayout      cullPipelineLayout = VK_NULL_HANDLE;
static VkPipeline            cullPipeline       = VK_NULL_HANDLE;

/* --------------------------------------------------------- */
/* Buffers                                                    */
/* --------------------------------------------------------- */

static int VANILLA_VK_cullBuffer(VkDeviceSize size, VkBufferUsageFlags usage, VkMemoryPropertyFlags properties,
                                 VkBuffer* outBuffer, VkDeviceMemory* outMemory) {
    VkBufferCreateInfo info = {
        .sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO,
        .size = size,
        .usage = usage,
        .sharingMode = VK_SHARING_MODE_EXCLUSIVE
    };
    if (vkCreateBuffer(vulkan.device, &info, NULL, outBuffer) != VK_SUCCESS) { *outBuffer = VK_NULL_HANDLE; return 1; }

    VkMemoryRequirements requirements;
    vkGetBufferMemoryRequirements(vulkan.device, *outBuffer, &requirements);
    *outMemory = VANILLA_gmmap(&requirements, properties);
    if (*outMemory == VK_NULL_HANDLE) { return 1; }
    if (vkBindBufferMemory(vulkan.device, *outBuffer, *outMemory, 0) != VK_SUCCESS) { return 1; }
    return 0;
}

static void VANILLA_VK_cullDestroyBuffer(VkBuffer* buffer, VkDeviceMemory* memory) {
    if (*buffer != VK_NULL_HANDLE) { vkDestroyBuffer(vulkan.device, *buffer, NULL); *buffer = VK_NULL_HANDLE; }
    if (*memory != VK_NULL_HANDLE) { VANILLA_gmunmap(*memory); *memory = VK_NULL_HANDLE; }
}

/* --------------------------------------------------------- */
/* Pipeline                                                   */
/* --------------------------------------------------------- */

static int VANILLA_VK_createCullPipeline(void) {
    VkDescriptorSetLayoutBinding bindings[9];
    for (uint32_t i = 0; i < 9; i++) {
        bindings[i] = (VkDescriptorSetLayoutBinding){
            .binding = i,
            .descriptorType = (i == 7) ? VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER : VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
            .descriptorCount = 1,
            .stageFlags = VK_SHADER_STAGE_COMPUTE_BIT
        };
    }

    VkDescriptorSetLayoutCreateInfo layoutInfo = {
        .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO,
        .bindingCount = 9,
        .pBindings = bindings
    };
    if (vkCreateDescriptorSetLayout(vulkan.device, &layoutInfo, NULL, &cullSetLayout) != VK_SUCCESS) {
        cullSetLayout = VK_NULL_HANDLE;
        return 1;
    }

    VkDescriptorPoolSize poolSizes[2] = {
        { .type = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,         .descriptorCount = 8 },
        { .type = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, .descriptorCount = 1 }
    };
    VkDescriptorPoolCreateInfo poolInfo = {
        .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO,
        .maxSets = 1,
        .poolSizeCount = 2,
        .pPoolSizes = poolSizes
    };
    if (vkCreateDescriptorPool(vulkan.device, &poolInfo, NULL, &cullDescriptorPool) != VK_SUCCESS) {
        cullDescriptorPool = VK_NULL_HANDLE;
        return 1;
    }

    VkDescriptorSetAllocateInfo allocInfo = {
        .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO,
        .descriptorPool = cullDescriptorPool,
        .descriptorSetCount = 1,
        .pSetLayouts = &cullSetLayout
    };
    if (vkAllocateDescriptorSets(vulkan.device, &allocInfo, &cullDescriptorSet) != VK_SUCCESS) { return 1; }

    VkDescriptorBufferInfo infos[9] = {
        { vulkan.indirectCommandBuffer, 0, VK_WHOLE_SIZE },
        { outputBuffer,                 0, VK_WHOLE_SIZE },
        { countBuffer,                  0, VK_WHOLE_SIZE },
        { vulkan.globalTransformBuffer, 0, VK_WHOLE_SIZE },
        { slotBuffer,                   0, VK_WHOLE_SIZE },
        { boundsBuffer,                 0, VK_WHOLE_SIZE },
        { vulkan.materialSlotBuffer,    0, VK_WHOLE_SIZE },
        { 0 },                                                  /* 7: Hi-Z, written by VANILLA_VK_recreateHiZ */
        { occlusionBuffer,              0, VK_WHOLE_SIZE }
    };
    VkWriteDescriptorSet writes[8];
    uint32_t w = 0;
    for (uint32_t i = 0; i < 9; i++) {
        if (i == 7) { continue; }
        writes[w++] = (VkWriteDescriptorSet){
            .sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
            .dstSet = cullDescriptorSet,
            .dstBinding = i,
            .descriptorCount = 1,
            .descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
            .pBufferInfo = &infos[i]
        };
    }
    vkUpdateDescriptorSets(vulkan.device, w, writes, 0, NULL);

    VkPushConstantRange pushRange = { VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(VANILLA_CullPush) };
    VkPipelineLayoutCreateInfo pipelineLayoutInfo = {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO,
        .setLayoutCount = 1,
        .pSetLayouts = &cullSetLayout,
        .pushConstantRangeCount = 1,
        .pPushConstantRanges = &pushRange
    };
    if (vkCreatePipelineLayout(vulkan.device, &pipelineLayoutInfo, NULL, &cullPipelineLayout) != VK_SUCCESS) {
        cullPipelineLayout = VK_NULL_HANDLE;
        return 1;
    }

    VkShaderModuleCreateInfo shaderInfo = {
        .sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO,
        .codeSize = VANILLA_cullSpv_len * sizeof(uint32_t),
        .pCode = VANILLA_cullSpv
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
        .layout = cullPipelineLayout,
        .basePipelineIndex = -1
    };
    VkResult result = vkCreateComputePipelines(vulkan.device, VK_NULL_HANDLE, 1, &pipelineInfo, NULL, &cullPipeline);
    vkDestroyShaderModule(vulkan.device, shaderModule, NULL);
    if (result != VK_SUCCESS) { cullPipeline = VK_NULL_HANDLE; return 1; }
    return 0;
}

static VkPipeline VANILLA_VK_hizPipeline(const uint32_t* code, size_t words, VkPipelineLayout layout) {
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
    if (vkCreateComputePipelines(vulkan.device, VK_NULL_HANDLE, 1, &pipelineInfo, NULL, &pipeline) != VK_SUCCESS) { pipeline = VK_NULL_HANDLE; }
    vkDestroyShaderModule(vulkan.device, shaderModule, NULL);
    return pipeline;
}

/* Layouts + pipelines of the pyramid build (once). */
static int VANILLA_VK_createHiZPipelines(void) {
    VkDescriptorSetLayoutBinding initBindings[2] = {
        { .binding = 0, .descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, .descriptorCount = 1, .stageFlags = VK_SHADER_STAGE_COMPUTE_BIT },
        { .binding = 1, .descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE,          .descriptorCount = 1, .stageFlags = VK_SHADER_STAGE_COMPUTE_BIT }
    };
    VkDescriptorSetLayoutBinding reduceBindings[2] = {
        { .binding = 0, .descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, .descriptorCount = 1, .stageFlags = VK_SHADER_STAGE_COMPUTE_BIT },
        { .binding = 1, .descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, .descriptorCount = 1, .stageFlags = VK_SHADER_STAGE_COMPUTE_BIT }
    };

    VkDescriptorSetLayoutCreateInfo initInfo   = { .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO, .bindingCount = 2, .pBindings = initBindings };
    VkDescriptorSetLayoutCreateInfo reduceInfo = { .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO, .bindingCount = 2, .pBindings = reduceBindings };
    if (vkCreateDescriptorSetLayout(vulkan.device, &initInfo, NULL, &hizInitSetLayout) != VK_SUCCESS)     { hizInitSetLayout = VK_NULL_HANDLE; return 1; }
    if (vkCreateDescriptorSetLayout(vulkan.device, &reduceInfo, NULL, &hizReduceSetLayout) != VK_SUCCESS) { hizReduceSetLayout = VK_NULL_HANDLE; return 1; }

    VkPushConstantRange pushRange = { VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(VANILLA_HiZPush) };
    VkPipelineLayoutCreateInfo layoutInfo = {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO,
        .setLayoutCount = 1,
        .pSetLayouts = &hizInitSetLayout,
        .pushConstantRangeCount = 1,
        .pPushConstantRanges = &pushRange
    };
    if (vkCreatePipelineLayout(vulkan.device, &layoutInfo, NULL, &hizInitLayout) != VK_SUCCESS) { hizInitLayout = VK_NULL_HANDLE; return 1; }
    layoutInfo.pSetLayouts = &hizReduceSetLayout;
    if (vkCreatePipelineLayout(vulkan.device, &layoutInfo, NULL, &hizReduceLayout) != VK_SUCCESS) { hizReduceLayout = VK_NULL_HANDLE; return 1; }

    hizInitPipeline   = VANILLA_VK_hizPipeline(VANILLA_hizInitSpv,   VANILLA_hizInitSpv_len,   hizInitLayout);
    hizInitMsPipeline = VANILLA_VK_hizPipeline(VANILLA_hizInitMsSpv, VANILLA_hizInitMsSpv_len, hizInitLayout);
    hizReducePipeline = VANILLA_VK_hizPipeline(VANILLA_hizReduceSpv, VANILLA_hizReduceSpv_len, hizReduceLayout);
    if (!hizInitPipeline || !hizInitMsPipeline || !hizReducePipeline) { return 1; }

    VkSamplerCreateInfo samplerInfo = {
        .sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO,
        .magFilter = VK_FILTER_NEAREST,
        .minFilter = VK_FILTER_NEAREST,
        .mipmapMode = VK_SAMPLER_MIPMAP_MODE_NEAREST,
        .addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE,
        .addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE,
        .addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE,
        .maxLod = VK_LOD_CLAMP_NONE
    };
    if (vkCreateSampler(vulkan.device, &samplerInfo, NULL, &hizSampler) != VK_SUCCESS) { hizSampler = VK_NULL_HANDLE; return 1; }

    return VANILLA_VK_cullBuffer(sizeof(VANILLA_OcclusionParams),
        VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
        &occlusionBuffer, &occlusionMemory);
}

static void VANILLA_VK_destroyHiZImage(void) {
    if (hizPool != VK_NULL_HANDLE) {
        vkDestroyDescriptorPool(vulkan.device, hizPool, NULL);
        hizPool = VK_NULL_HANDLE;
        hizInitSet = VK_NULL_HANDLE;
        for (uint32_t m = 0; m < VANILLA_HIZ_MAX_MIPS; m++) { hizReduceSets[m] = VK_NULL_HANDLE; }
    }
    for (uint32_t m = 0; m < VANILLA_HIZ_MAX_MIPS; m++) {
        if (hizMipViews[m] != VK_NULL_HANDLE) { vkDestroyImageView(vulkan.device, hizMipViews[m], NULL); hizMipViews[m] = VK_NULL_HANDLE; }
    }
    if (hizSampled != VK_NULL_HANDLE) { vkDestroyImageView(vulkan.device, hizSampled, NULL); hizSampled = VK_NULL_HANDLE; }
    if (hizImage != VK_NULL_HANDLE)   { vkDestroyImage(vulkan.device, hizImage, NULL);        hizImage = VK_NULL_HANDLE; }
    if (hizMemory != VK_NULL_HANDLE)  { VANILLA_gmunmap(hizMemory);                           hizMemory = VK_NULL_HANDLE; }
    hizMips = 0;
    hizValid = false;
}

static int VANILLA_VK_createHiZImage(void) {
    hizSize[0] = (vulkan.swapchainExtent.width  + 1) / 2;
    hizSize[1] = (vulkan.swapchainExtent.height + 1) / 2;
    if (hizSize[0] == 0) { hizSize[0] = 1; }
    if (hizSize[1] == 0) { hizSize[1] = 1; }

    uint32_t largest = hizSize[0] > hizSize[1] ? hizSize[0] : hizSize[1];
    hizMips = 1;
    while ((largest >> hizMips) > 0 && hizMips < VANILLA_HIZ_MAX_MIPS) { hizMips++; }

    VkImageCreateInfo imageInfo = {
        .sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO,
        .imageType = VK_IMAGE_TYPE_2D,
        .format = VK_FORMAT_R32_SFLOAT,
        .extent = { hizSize[0], hizSize[1], 1 },
        .mipLevels = hizMips,
        .arrayLayers = 1,
        .samples = VK_SAMPLE_COUNT_1_BIT,
        .tiling = VK_IMAGE_TILING_OPTIMAL,
        .usage = VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_SAMPLED_BIT,
        .sharingMode = VK_SHARING_MODE_EXCLUSIVE,
        .initialLayout = VK_IMAGE_LAYOUT_UNDEFINED
    };
    if (vkCreateImage(vulkan.device, &imageInfo, NULL, &hizImage) != VK_SUCCESS) { hizImage = VK_NULL_HANDLE; return 1; }

    VkMemoryRequirements requirements;
    vkGetImageMemoryRequirements(vulkan.device, hizImage, &requirements);
    hizMemory = VANILLA_gmmap(&requirements, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
    if (hizMemory == VK_NULL_HANDLE) { return 1; }
    if (vkBindImageMemory(vulkan.device, hizImage, hizMemory, 0) != VK_SUCCESS) { return 1; }

    VkImageViewCreateInfo viewInfo = {
        .sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO,
        .image = hizImage,
        .viewType = VK_IMAGE_VIEW_TYPE_2D,
        .format = VK_FORMAT_R32_SFLOAT,
        .subresourceRange = { VK_IMAGE_ASPECT_COLOR_BIT, 0, hizMips, 0, 1 }
    };
    if (vkCreateImageView(vulkan.device, &viewInfo, NULL, &hizSampled) != VK_SUCCESS) { hizSampled = VK_NULL_HANDLE; return 1; }
    for (uint32_t m = 0; m < hizMips; m++) {
        viewInfo.subresourceRange.baseMipLevel = m;
        viewInfo.subresourceRange.levelCount = 1;
        if (vkCreateImageView(vulkan.device, &viewInfo, NULL, &hizMipViews[m]) != VK_SUCCESS) { hizMipViews[m] = VK_NULL_HANDLE; return 1; }
    }

    /* GENERAL forever (storage writes, sampled reads). */
    VkCommandBuffer commandBuffer = VANILLA_VK_beginSingleTimeCommands();
    if (commandBuffer == VK_NULL_HANDLE) { return 1; }
    VkImageMemoryBarrier2 barrier = {
        .sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2,
        .srcStageMask = VK_PIPELINE_STAGE_2_NONE,
        .dstStageMask = VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT,
        .dstAccessMask = VK_ACCESS_2_SHADER_STORAGE_WRITE_BIT | VK_ACCESS_2_SHADER_SAMPLED_READ_BIT,
        .oldLayout = VK_IMAGE_LAYOUT_UNDEFINED,
        .newLayout = VK_IMAGE_LAYOUT_GENERAL,
        .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
        .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
        .image = hizImage,
        .subresourceRange = { VK_IMAGE_ASPECT_COLOR_BIT, 0, hizMips, 0, 1 }
    };
    VkDependencyInfo dependencyInfo = { .sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO, .imageMemoryBarrierCount = 1, .pImageMemoryBarriers = &barrier };
    vkCmdPipelineBarrier2(commandBuffer, &dependencyInfo);
    if (VANILLA_VK_endSingleTimeCommands(commandBuffer)) { return 1; }

    /* Descriptor sets: init (depth -> mip 0) and one per reduce step. */
    VkDescriptorPoolSize poolSizes[2] = {
        { .type = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, .descriptorCount = 1 },
        { .type = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE,          .descriptorCount = 1 + 2 * hizMips }
    };
    VkDescriptorPoolCreateInfo poolInfo = {
        .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO,
        .maxSets = hizMips,
        .poolSizeCount = 2,
        .pPoolSizes = poolSizes
    };
    if (vkCreateDescriptorPool(vulkan.device, &poolInfo, NULL, &hizPool) != VK_SUCCESS) { hizPool = VK_NULL_HANDLE; return 1; }

    VkDescriptorSetAllocateInfo allocInfo = {
        .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO,
        .descriptorPool = hizPool,
        .descriptorSetCount = 1,
        .pSetLayouts = &hizInitSetLayout
    };
    if (vkAllocateDescriptorSets(vulkan.device, &allocInfo, &hizInitSet) != VK_SUCCESS) { return 1; }
    allocInfo.pSetLayouts = &hizReduceSetLayout;
    for (uint32_t m = 1; m < hizMips; m++) {
        if (vkAllocateDescriptorSets(vulkan.device, &allocInfo, &hizReduceSets[m]) != VK_SUCCESS) { return 1; }
    }

    const bool msaa = VANILLA_VK_msaaSamples() > VK_SAMPLE_COUNT_1_BIT;
    VkDescriptorImageInfo depthInfo = {
        hizSampler, msaa ? VANILLA_VK_msaaDepthView() : vulkan.depthImageView, VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL
    };
    VkDescriptorImageInfo mipInfos[VANILLA_HIZ_MAX_MIPS];
    for (uint32_t m = 0; m < hizMips; m++) { mipInfos[m] = (VkDescriptorImageInfo){ VK_NULL_HANDLE, hizMipViews[m], VK_IMAGE_LAYOUT_GENERAL }; }

    VkWriteDescriptorSet writes[2 + 2 * VANILLA_HIZ_MAX_MIPS];
    uint32_t w = 0;
    writes[w++] = (VkWriteDescriptorSet){ .sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET, .dstSet = hizInitSet, .dstBinding = 0,
        .descriptorCount = 1, .descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, .pImageInfo = &depthInfo };
    writes[w++] = (VkWriteDescriptorSet){ .sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET, .dstSet = hizInitSet, .dstBinding = 1,
        .descriptorCount = 1, .descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, .pImageInfo = &mipInfos[0] };
    for (uint32_t m = 1; m < hizMips; m++) {
        writes[w++] = (VkWriteDescriptorSet){ .sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET, .dstSet = hizReduceSets[m], .dstBinding = 0,
            .descriptorCount = 1, .descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, .pImageInfo = &mipInfos[m - 1] };
        writes[w++] = (VkWriteDescriptorSet){ .sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET, .dstSet = hizReduceSets[m], .dstBinding = 1,
            .descriptorCount = 1, .descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, .pImageInfo = &mipInfos[m] };
    }

    /* The cull set samples the pyramid. */
    VkDescriptorImageInfo hizInfo = { hizSampler, hizSampled, VK_IMAGE_LAYOUT_GENERAL };
    writes[w++] = (VkWriteDescriptorSet){ .sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET, .dstSet = cullDescriptorSet, .dstBinding = 7,
        .descriptorCount = 1, .descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, .pImageInfo = &hizInfo };

    vkUpdateDescriptorSets(vulkan.device, w, writes, 0, NULL);
    hizValid = false;   /* nothing built at this size yet */
    return 0;
}

/* Swapchain size or MSAA changed (the depth it reads, its size). The device is idle. */
void VANILLA_VK_recreateHiZ(void) {
    if (!cullingAvailable) { return; }
    VANILLA_VK_destroyHiZImage();
    if (VANILLA_VK_createHiZImage()) {
        printf("[VANILLA WINDOW] Hi-Z rebuild failed: occlusion culling off\n");
        VANILLA_VK_destroyHiZImage();
        cullingAvailable = false;
    }
}

/* --------------------------------------------------------- */
/* Init / destroy                                             */
/* --------------------------------------------------------- */

int VANILLA_VK_initCulling(void) {
    cullingAvailable = false;
    if (!vulkan.drawIndirectCountSupported) {
        printf("[VANILLA WINDOW] drawIndirectCount not supported: GPU culling off\n");
        return 0;
    }

    modelCapacity  = stream->modelChunkLimit * CHUNKSIZE;
    slotCapacity   = stream->chunkLimit * CHUNKSIZE;
    listCountTotal = (uint32_t)vulkan.pipelineCount * VANILLA_CULL_MODES;

    const VkMemoryPropertyFlags host = VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT;
    const VkMemoryPropertyFlags dev  = VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT;
    const VkBufferUsageFlags    sto  = VK_BUFFER_USAGE_STORAGE_BUFFER_BIT;

    const VkDeviceSize boundsSize = (VkDeviceSize)modelCapacity * 4 * sizeof(float);
    const VkDeviceSize slotSize   = (VkDeviceSize)slotCapacity * sizeof(uint32_t);

    if (VANILLA_VK_cullBuffer(boundsSize, VK_BUFFER_USAGE_TRANSFER_SRC_BIT, host, &boundsStaging, &boundsStagingMemory)) { goto fail; }
    if (VANILLA_VK_cullBuffer(boundsSize, sto | VK_BUFFER_USAGE_TRANSFER_DST_BIT, dev, &boundsBuffer, &boundsMemory))   { goto fail; }
    if (VANILLA_VK_cullBuffer(slotSize, VK_BUFFER_USAGE_TRANSFER_SRC_BIT, host, &slotStaging, &slotStagingMemory))       { goto fail; }
    if (VANILLA_VK_cullBuffer(slotSize, sto | VK_BUFFER_USAGE_TRANSFER_DST_BIT, dev, &slotBuffer, &slotMemory))         { goto fail; }

    if (vkMapMemory(vulkan.device, boundsStagingMemory, 0, boundsSize, 0, (void**)&boundsMapped) != VK_SUCCESS) { boundsMapped = NULL; goto fail; }
    if (vkMapMemory(vulkan.device, slotStagingMemory, 0, slotSize, 0, (void**)&slotMapped) != VK_SUCCESS)       { slotMapped = NULL; goto fail; }
    memset(boundsMapped, 0, (size_t)boundsSize);   /* radius 0 = no bounds yet = never culled */
    memset(slotMapped, 0, (size_t)slotSize);
    boundsDirtyMin = 0; boundsDirtyMax = modelCapacity - 1;
    slotDirtyMin   = 0; slotDirtyMax   = slotCapacity - 1;

    /* Same layout as the indirect device buffer: list regions keep their offsets. */
    if (VANILLA_VK_cullBuffer(VANILLA_VK_indirectBufferSize(), sto | VK_BUFFER_USAGE_INDIRECT_BUFFER_BIT, dev, &outputBuffer, &outputMemory)) { goto fail; }
    if (VANILLA_VK_cullBuffer((VkDeviceSize)listCountTotal * sizeof(uint32_t),
                              sto | VK_BUFFER_USAGE_INDIRECT_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT, dev, &countBuffer, &countMemory)) { goto fail; }

    listCulled = VANILLA_mmap(sizeof(bool) * listCountTotal);
    if (listCulled == VANILLA_MAP_FAILED) { listCulled = NULL; goto fail; }

    if (VANILLA_VK_createHiZPipelines()) { goto fail; }
    if (VANILLA_VK_createCullPipeline()) { goto fail; }
    if (VANILLA_VK_createHiZImage())     { goto fail; }

    cullingAvailable = true;
    return 0;

fail:
    printf("[VANILLA WINDOW] culling init failed: GPU culling off\n");
    VANILLA_VK_destroyCulling();
    return 0;   /* not fatal: everything is drawn */
}

void VANILLA_VK_destroyCulling(void) {
    VANILLA_VK_destroyHiZImage();
    if (hizInitPipeline != VK_NULL_HANDLE)    { vkDestroyPipeline(vulkan.device, hizInitPipeline, NULL);    hizInitPipeline = VK_NULL_HANDLE; }
    if (hizInitMsPipeline != VK_NULL_HANDLE)  { vkDestroyPipeline(vulkan.device, hizInitMsPipeline, NULL);  hizInitMsPipeline = VK_NULL_HANDLE; }
    if (hizReducePipeline != VK_NULL_HANDLE)  { vkDestroyPipeline(vulkan.device, hizReducePipeline, NULL);  hizReducePipeline = VK_NULL_HANDLE; }
    if (hizInitLayout != VK_NULL_HANDLE)      { vkDestroyPipelineLayout(vulkan.device, hizInitLayout, NULL);   hizInitLayout = VK_NULL_HANDLE; }
    if (hizReduceLayout != VK_NULL_HANDLE)    { vkDestroyPipelineLayout(vulkan.device, hizReduceLayout, NULL); hizReduceLayout = VK_NULL_HANDLE; }
    if (hizInitSetLayout != VK_NULL_HANDLE)   { vkDestroyDescriptorSetLayout(vulkan.device, hizInitSetLayout, NULL);   hizInitSetLayout = VK_NULL_HANDLE; }
    if (hizReduceSetLayout != VK_NULL_HANDLE) { vkDestroyDescriptorSetLayout(vulkan.device, hizReduceSetLayout, NULL); hizReduceSetLayout = VK_NULL_HANDLE; }
    if (hizSampler != VK_NULL_HANDLE)         { vkDestroySampler(vulkan.device, hizSampler, NULL); hizSampler = VK_NULL_HANDLE; }
    VANILLA_VK_cullDestroyBuffer(&occlusionBuffer, &occlusionMemory);

    if (cullPipeline != VK_NULL_HANDLE)       { vkDestroyPipeline(vulkan.device, cullPipeline, NULL);             cullPipeline = VK_NULL_HANDLE; }
    if (cullPipelineLayout != VK_NULL_HANDLE) { vkDestroyPipelineLayout(vulkan.device, cullPipelineLayout, NULL); cullPipelineLayout = VK_NULL_HANDLE; }
    if (cullDescriptorPool != VK_NULL_HANDLE) {
        vkDestroyDescriptorPool(vulkan.device, cullDescriptorPool, NULL);
        cullDescriptorPool = VK_NULL_HANDLE;
        cullDescriptorSet = VK_NULL_HANDLE;
    }
    if (cullSetLayout != VK_NULL_HANDLE) { vkDestroyDescriptorSetLayout(vulkan.device, cullSetLayout, NULL); cullSetLayout = VK_NULL_HANDLE; }

    if (boundsMapped != NULL) { vkUnmapMemory(vulkan.device, boundsStagingMemory); boundsMapped = NULL; }
    if (slotMapped != NULL)   { vkUnmapMemory(vulkan.device, slotStagingMemory);   slotMapped = NULL; }

    VANILLA_VK_cullDestroyBuffer(&boundsStaging, &boundsStagingMemory);
    VANILLA_VK_cullDestroyBuffer(&boundsBuffer,  &boundsMemory);
    VANILLA_VK_cullDestroyBuffer(&slotStaging,   &slotStagingMemory);
    VANILLA_VK_cullDestroyBuffer(&slotBuffer,    &slotMemory);
    VANILLA_VK_cullDestroyBuffer(&outputBuffer,  &outputMemory);
    VANILLA_VK_cullDestroyBuffer(&countBuffer,   &countMemory);

    if (listCulled != NULL) { VANILLA_munmap(listCulled, sizeof(bool) * listCountTotal); listCulled = NULL; }

    cullingAvailable = false;
}

/* --------------------------------------------------------- */
/* Bounds                                                     */
/* --------------------------------------------------------- */

/* Bounding sphere of a model from its ANYCORE vertices (position = first 3 floats of vertex location 0). */
void VANILLA_VK_cullSetModelBounds(uint32_t model, const void* anycoreVertices, uint32_t vertexCount) {
    if (boundsMapped == NULL || model >= modelCapacity || anycoreVertices == NULL || vertexCount == 0) { return; }

    uint32_t positionOffset = UINT32_MAX;
    for (uint32_t a = 0; a < vertexLayout.attributeCount; a++) {
        if (vertexLayout.attributes[a].location == 0 && vertexLayout.attributes[a].size >= 3 * sizeof(float)) {
            positionOffset = vertexLayout.attributes[a].anycoreOffset;
        }
    }
    if (positionOffset == UINT32_MAX) { return; }   /* unknown layout: never culled */

    const uint8_t* bytes  = (const uint8_t*)anycoreVertices;
    const size_t   stride_ = stream->stride;

    float mn[3] = {  INFINITY,  INFINITY,  INFINITY };
    float mx[3] = { -INFINITY, -INFINITY, -INFINITY };
    for (uint32_t v = 0; v < vertexCount; v++) {
        const float* p = (const float*)(bytes + (size_t)v * stride_ + positionOffset);
        for (int k = 0; k < 3; k++) {
            if (p[k] < mn[k]) { mn[k] = p[k]; }
            if (p[k] > mx[k]) { mx[k] = p[k]; }
        }
    }

    const float c[3] = { (mn[0] + mx[0]) * 0.5f, (mn[1] + mx[1]) * 0.5f, (mn[2] + mx[2]) * 0.5f };
    float r2 = 0.0f;
    for (uint32_t v = 0; v < vertexCount; v++) {
        const float* p = (const float*)(bytes + (size_t)v * stride_ + positionOffset);
        const float dx = p[0] - c[0], dy = p[1] - c[1], dz = p[2] - c[2];
        const float d2 = dx * dx + dy * dy + dz * dz;
        if (d2 > r2) { r2 = d2; }
    }

    float* b = boundsMapped + (size_t)model * 4;
    b[0] = c[0]; b[1] = c[1]; b[2] = c[2];
    b[3] = sqrtf(r2) * 1.0001f + 1e-5f;   /* never 0 for a real mesh */

    if (model < boundsDirtyMin) { boundsDirtyMin = model; }
    if (model > boundsDirtyMax || boundsDirtyMax == UINT32_MAX) { boundsDirtyMax = model; }
}

/* Model of an entity slot (called whenever its draw command is built). Skinned entities are never culled. */
void VANILLA_VK_cullSetSlotModel(uint32_t slot, uint32_t model) {
    if (slotMapped == NULL || slot >= slotCapacity) { return; }

    uint32_t value = model;
    if (VANILLA_VK_skinnedVertexOffset(slot) != UINT32_MAX) { value |= VANILLA_CULL_NEVER; }
    if (slotMapped[slot] == value) { return; }   /* unchanged: nothing to upload */

    slotMapped[slot] = value;
    if (slot < slotDirtyMin) { slotDirtyMin = slot; }
    if (slot > slotDirtyMax || slotDirtyMax == UINT32_MAX) { slotDirtyMax = slot; }
}

/* Changed bounds / slot models -> device. Before the main barrier (with the other uploads). */
void VANILLA_VK_recordCullUploads(VkCommandBuffer commandBuffer) {
    if (!cullingAvailable) { return; }

    if (boundsDirtyMin != UINT32_MAX) {
        const VkDeviceSize offset = (VkDeviceSize)boundsDirtyMin * 16;
        const VkBufferCopy region = { offset, offset, (VkDeviceSize)(boundsDirtyMax - boundsDirtyMin + 1) * 16 };
        vkCmdCopyBuffer(commandBuffer, boundsStaging, boundsBuffer, 1, &region);
        boundsDirtyMin = UINT32_MAX; boundsDirtyMax = 0;
    }
    if (slotDirtyMin != UINT32_MAX) {
        const VkDeviceSize offset = (VkDeviceSize)slotDirtyMin * 4;
        const VkBufferCopy region = { offset, offset, (VkDeviceSize)(slotDirtyMax - slotDirtyMin + 1) * 4 };
        vkCmdCopyBuffer(commandBuffer, slotStaging, slotBuffer, 1, &region);
        slotDirtyMin = UINT32_MAX; slotDirtyMax = 0;
    }
}

/* --------------------------------------------------------- */
/* Passes                                                     */
/* --------------------------------------------------------- */

static void VANILLA_VK_cullBarrier(VkCommandBuffer commandBuffer,
                                   VkPipelineStageFlags2 srcStage, VkAccessFlags2 srcAccess,
                                   VkPipelineStageFlags2 dstStage, VkAccessFlags2 dstAccess) {
    VkMemoryBarrier2 barrier = {
        .sType = VK_STRUCTURE_TYPE_MEMORY_BARRIER_2,
        .srcStageMask = srcStage, .srcAccessMask = srcAccess,
        .dstStageMask = dstStage, .dstAccessMask = dstAccess
    };
    VkDependencyInfo dependencyInfo = { .sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO, .memoryBarrierCount = 1, .pMemoryBarriers = &barrier };
    vkCmdPipelineBarrier2(commandBuffer, &dependencyInfo);
}

/* Frustum planes of a (Vulkan, z 0..1) view-projection, inside = positive, normalized. */
static void VANILLA_VK_extractPlanes(const mat4* m, float planes[6][4]) {
    const float* a = m->m;   /* column-major: element (row r, col c) = a[c * 4 + r] */
    float rows[4][4];
    for (int r = 0; r < 4; r++) {
        for (int c = 0; c < 4; c++) { rows[r][c] = a[c * 4 + r]; }
    }

    for (int c = 0; c < 4; c++) {
        planes[0][c] = rows[3][c] + rows[0][c];   /* left   */
        planes[1][c] = rows[3][c] - rows[0][c];   /* right  */
        planes[2][c] = rows[3][c] + rows[1][c];   /* bottom */
        planes[3][c] = rows[3][c] - rows[1][c];   /* top    */
        planes[4][c] = rows[2][c];                /* near (z >= 0) */
        planes[5][c] = rows[3][c] - rows[2][c];   /* far    */
    }

    for (int p = 0; p < 6; p++) {
        const float len = sqrtf(planes[p][0] * planes[p][0] + planes[p][1] * planes[p][1] + planes[p][2] * planes[p][2]);
        if (len > 1e-12f) {
            for (int c = 0; c < 4; c++) { planes[p][c] /= len; }
        }
    }
}

static bool VANILLA_VK_pipelineCullable(uint16_t p) {
    return !pipelineInfos[p].skipCulling && !pipelineInfos[p].preserveDrawOrder;
}

/*
 * Culls the opaque + mask lists of the pipelines a pass draws (shadowPass: castShadows pipelines) against
 * viewProj. Outside any rendering. Until the next cullPass, those lists draw through VANILLA_VK_drawList.
 */
void VANILLA_VK_cullPass(VkCommandBuffer commandBuffer, const mat4* viewProj, bool shadowPass) {
    if (listCulled != NULL) { memset(listCulled, 0, sizeof(bool) * listCountTotal); }
    if (!cullingAvailable || cullingMode == VANILLA_CULLING_NONE) { return; }

    VANILLA_CullPush push = { .flags = shadowPass ? 1u : 0u };
    VANILLA_VK_extractPlanes(viewProj, push.planes);

    /* The previous pass's draws (and culls) are done with the output / counts. */
    VANILLA_VK_cullBarrier(commandBuffer,
        VK_PIPELINE_STAGE_2_DRAW_INDIRECT_BIT | VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT, VK_ACCESS_2_NONE,
        VK_PIPELINE_STAGE_2_TRANSFER_BIT | VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT,     VK_ACCESS_2_NONE);

    vkCmdFillBuffer(commandBuffer, countBuffer, 0, VK_WHOLE_SIZE, 0);

    /* Counts reset + this frame's uploads (commands, bounds, slot models, transforms) -> cull. */
    VANILLA_VK_cullBarrier(commandBuffer,
        VK_PIPELINE_STAGE_2_TRANSFER_BIT | VK_PIPELINE_STAGE_2_HOST_BIT,
        VK_ACCESS_2_TRANSFER_WRITE_BIT | VK_ACCESS_2_HOST_WRITE_BIT,
        VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT,
        VK_ACCESS_2_SHADER_STORAGE_READ_BIT | VK_ACCESS_2_SHADER_STORAGE_WRITE_BIT);

    vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_COMPUTE, cullPipeline);
    vkCmdBindDescriptorSets(commandBuffer, VK_PIPELINE_BIND_POINT_COMPUTE, cullPipelineLayout, 0, 1, &cullDescriptorSet, 0, NULL);

    /* Occlusion: main camera, the pipeline whose depth the pyramid holds, once a pyramid exists. */
    const bool occlusion = !shadowPass && cullingMode == VANILLA_CULLING_FRUSTUM_OCCLUSION && hizValid;
    const int32_t occlusionPipeline = VANILLA_VK_firstHdrPipeline();

    for (uint16_t p = 0; p < vulkan.pipelineCount; p++) {
        if (!VANILLA_VK_pipelineCullable(p)) { continue; }
        if (shadowPass && !pipelineInfos[p].castShadows) { continue; }

        push.flags = (shadowPass ? 1u : 0u) | ((occlusion && (int32_t)p == occlusionPipeline) ? 2u : 0u);

        for (uint32_t mode = VANILLA_ALPHAMODE_OPAQUE; mode <= VANILLA_ALPHAMODE_MASK; mode++) {
            const uint32_t list  = p * VANILLA_CULL_MODES + mode;
            const uint32_t count = VANILLA_VK_indirectCount(p, mode);
            listCulled[list] = true;              /* drawn with the (possibly zero) count */
            if (count == 0) { continue; }

            push.inputOffset = (uint32_t)(VANILLA_VK_indirectOffset(p, mode) / sizeof(VkDrawIndexedIndirectCommand));
            push.count       = count;
            push.countIndex  = list;

            vkCmdPushConstants(commandBuffer, cullPipelineLayout, VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(push), &push);
            vkCmdDispatch(commandBuffer, (count + 63) / 64, 1, 1);
        }
    }

    VANILLA_VK_cullBarrier(commandBuffer,
        VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT, VK_ACCESS_2_SHADER_STORAGE_WRITE_BIT,
        VK_PIPELINE_STAGE_2_DRAW_INDIRECT_BIT,  VK_ACCESS_2_INDIRECT_COMMAND_READ_BIT);
}

/* Draws one list of a pipeline (bound): culled through the counts of the last cullPass, else everything. */
void VANILLA_VK_drawList(VkCommandBuffer commandBuffer, uint16_t pipeline, uint32_t mode) {
    const uint32_t count = VANILLA_VK_indirectCount(pipeline, mode);
    if (count == 0) { return; }

    const VkDeviceSize offset = VANILLA_VK_indirectOffset(pipeline, mode);
    const uint32_t list = pipeline * VANILLA_CULL_MODES + mode;

    if (listCulled != NULL && listCulled[list]) {
        vkCmdDrawIndexedIndirectCount(commandBuffer, outputBuffer, offset, countBuffer,
                                      (VkDeviceSize)list * sizeof(uint32_t), count, sizeof(VkDrawIndexedIndirectCommand));
        return;
    }

    vkCmdDrawIndexedIndirect(commandBuffer, vulkan.indirectCommandBuffer, offset, count, sizeof(VkDrawIndexedIndirectCommand));
}

bool VANILLA_VK_hiZWanted(void) {
    return cullingAvailable && cullingMode == VANILLA_CULLING_FRUSTUM_OCCLUSION && hizImage != VK_NULL_HANDLE;
}

static void VANILLA_VK_depthLayout(VkCommandBuffer commandBuffer, VkImage image, bool toRead) {
    VkImageMemoryBarrier2 barrier = {
        .sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2,
        .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
        .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
        .image = image,
        .subresourceRange = { VK_IMAGE_ASPECT_DEPTH_BIT, 0, 1, 0, 1 }
    };
    if (toRead) {
        barrier.srcStageMask  = VK_PIPELINE_STAGE_2_EARLY_FRAGMENT_TESTS_BIT | VK_PIPELINE_STAGE_2_LATE_FRAGMENT_TESTS_BIT;
        barrier.srcAccessMask = VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
        barrier.dstStageMask  = VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT;
        barrier.dstAccessMask = VK_ACCESS_2_SHADER_SAMPLED_READ_BIT;
        barrier.oldLayout     = VK_IMAGE_LAYOUT_DEPTH_ATTACHMENT_OPTIMAL;
        barrier.newLayout     = VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL;
    }
    else {
        barrier.srcStageMask  = VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT;
        barrier.srcAccessMask = VK_ACCESS_2_NONE;
        barrier.dstStageMask  = VK_PIPELINE_STAGE_2_EARLY_FRAGMENT_TESTS_BIT | VK_PIPELINE_STAGE_2_LATE_FRAGMENT_TESTS_BIT;
        barrier.dstAccessMask = VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_READ_BIT | VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
        barrier.oldLayout     = VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL;
        barrier.newLayout     = VK_IMAGE_LAYOUT_DEPTH_ATTACHMENT_OPTIMAL;
    }
    VkDependencyInfo dependencyInfo = { .sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO, .imageMemoryBarrierCount = 1, .pImageMemoryBarriers = &barrier };
    vkCmdPipelineBarrier2(commandBuffer, &dependencyInfo);
}

/*
 * Builds the Hi-Z pyramid from the scene depth (outside any rendering, depth in DEPTH_ATTACHMENT_OPTIMAL and
 * stored) and remembers viewProj for the next frame's occlusion test.
 */
void VANILLA_VK_buildHiZ(VkCommandBuffer commandBuffer, const mat4* viewProj) {
    if (!VANILLA_VK_hiZWanted()) { return; }

    const VkSampleCountFlagBits samples = VANILLA_VK_msaaSamples();
    const bool msaa = samples > VK_SAMPLE_COUNT_1_BIT;
    const VkImage depthImage = msaa ? VANILLA_VK_msaaDepthImage() : vulkan.depthImage;

    VANILLA_VK_depthLayout(commandBuffer, depthImage, true);

    /* This frame's cull has read the old pyramid. */
    VANILLA_VK_cullBarrier(commandBuffer,
        VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT, VK_ACCESS_2_NONE,
        VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT, VK_ACCESS_2_NONE);

    VANILLA_HiZPush push = {
        .srcSize = { (int32_t)vulkan.swapchainExtent.width, (int32_t)vulkan.swapchainExtent.height },
        .dstSize = { (int32_t)hizSize[0], (int32_t)hizSize[1] },
        .samples = (int32_t)samples
    };

    vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_COMPUTE, msaa ? hizInitMsPipeline : hizInitPipeline);
    vkCmdBindDescriptorSets(commandBuffer, VK_PIPELINE_BIND_POINT_COMPUTE, hizInitLayout, 0, 1, &hizInitSet, 0, NULL);
    vkCmdPushConstants(commandBuffer, hizInitLayout, VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(push), &push);
    vkCmdDispatch(commandBuffer, (hizSize[0] + 7) / 8, (hizSize[1] + 7) / 8, 1);

    vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_COMPUTE, hizReducePipeline);
    for (uint32_t m = 1; m < hizMips; m++) {
        VANILLA_VK_cullBarrier(commandBuffer,
            VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT, VK_ACCESS_2_SHADER_STORAGE_WRITE_BIT,
            VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT, VK_ACCESS_2_SHADER_STORAGE_READ_BIT);

        const uint32_t srcW = hizSize[0] >> (m - 1) ? hizSize[0] >> (m - 1) : 1;
        const uint32_t srcH = hizSize[1] >> (m - 1) ? hizSize[1] >> (m - 1) : 1;
        const uint32_t dstW = hizSize[0] >> m ? hizSize[0] >> m : 1;
        const uint32_t dstH = hizSize[1] >> m ? hizSize[1] >> m : 1;
        push.srcSize[0] = (int32_t)srcW; push.srcSize[1] = (int32_t)srcH;
        push.dstSize[0] = (int32_t)dstW; push.dstSize[1] = (int32_t)dstH;

        vkCmdBindDescriptorSets(commandBuffer, VK_PIPELINE_BIND_POINT_COMPUTE, hizReduceLayout, 0, 1, &hizReduceSets[m], 0, NULL);
        vkCmdPushConstants(commandBuffer, hizReduceLayout, VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(push), &push);
        vkCmdDispatch(commandBuffer, (dstW + 7) / 8, (dstH + 7) / 8, 1);
    }

    VANILLA_VK_depthLayout(commandBuffer, depthImage, false);

    /* The view-projection the pyramid was rendered with. */
    VANILLA_OcclusionParams params = {
        .viewProj = *viewProj,
        .size = { (int32_t)hizSize[0], (int32_t)hizSize[1] },
        .mips = (int32_t)hizMips
    };
    vkCmdUpdateBuffer(commandBuffer, occlusionBuffer, 0, sizeof(params), &params);

    /* Pyramid + params -> next frame's cull. */
    VANILLA_VK_cullBarrier(commandBuffer,
        VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT | VK_PIPELINE_STAGE_2_TRANSFER_BIT,
        VK_ACCESS_2_SHADER_STORAGE_WRITE_BIT | VK_ACCESS_2_TRANSFER_WRITE_BIT,
        VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT,
        VK_ACCESS_2_SHADER_SAMPLED_READ_BIT | VK_ACCESS_2_SHADER_STORAGE_READ_BIT);

    hizValid = true;
}

/* --------------------------------------------------------- */
/* API                                                        */
/* --------------------------------------------------------- */

VANILLA_EXPORT void VANILLA_setCulling(VANILLA_Culling mode) {
    if (mode > VANILLA_CULLING_FRUSTUM_OCCLUSION) { mode = VANILLA_CULLING_FRUSTUM; }
    cullingMode = mode;
}

VANILLA_EXPORT VANILLA_Culling VANILLA_getCulling(void) {
    return cullingAvailable ? cullingMode : VANILLA_CULLING_NONE;
}
