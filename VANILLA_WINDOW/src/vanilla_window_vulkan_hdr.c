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
 * HDR pipelines, tone mapping and FXAA.
 *
 * A pipeline with PipelineCreateInfo.hdr renders into its own RGBA16F image
 * (multisampled when MSAA is on). Built-in fullscreen passes (embedded shaders) then:
 *   tone map   exposure + operator; with MSAA every sample is tone mapped, then averaged;
 *   FXAA       optional, on the tone mapped result (scene only, never UI).
 * The result goes:
 *   - with post-processing: into the pipeline's normal image (pp shaders read it as before);
 *   - without post-processing: onto the swapchain, before the non-HDR pipelines (UI)
 *     are drawn on top. All HDR pipelines share one HDR image in this mode.
 *
 * Fullscreen pipelines:
 *   toneSingle / toneMulti   1 sample target, no depth  (sampler2D / sampler2DMS input)
 *   toneDirect               swapchain pass: depth + MSAA sample count
 *   fxaaSingle               1 sample target, no depth
 *   fxaaDirect               swapchain pass: depth + MSAA sample count
 * They are rebuilt with the pipelines when the MSAA sample count changes.
 */

#include "vanilla_window_vulkan_utils.h"
#include "vanilla_window_tonemap_spv.h"
#include <stdint.h>
#include <stdbool.h>

#define VANILLA_HDR_FORMAT VK_FORMAT_R16G16B16A16_SFLOAT

/* --------------------------------------------------------- */
/* Settings (runtime)                                         */
/* --------------------------------------------------------- */

static VANILLA_ToneMapping toneMapping = VANILLA_TONEMAPPING_ACES;
static float               exposure    = 1.0f;

VANILLA_EXPORT void VANILLA_setToneMapping(VANILLA_ToneMapping mode) {
    if (mode != VANILLA_TONEMAPPING_NONE &&
        mode != VANILLA_TONEMAPPING_ACES &&
        mode != VANILLA_TONEMAPPING_AGX) { return; }
    toneMapping = mode;
}

VANILLA_EXPORT void VANILLA_setExposure(float value) {
    if (value <= 0.0f) { return; }
    exposure = value;
}

VANILLA_EXPORT VANILLA_ToneMapping VANILLA_getToneMapping(void) { return toneMapping; }
VANILLA_EXPORT float VANILLA_getExposure(void) { return exposure; }

/* --------------------------------------------------------- */
/* State                                                      */
/* --------------------------------------------------------- */

/* 16 bytes shared by both fullscreen shaders. */
typedef struct {
    float    a;      /* tone map: exposure      | FXAA: 1 / width  */
    float    b;      /* tone map: mode (as uint)| FXAA: 1 / height */
    uint32_t c;
    uint32_t d;
} VANILLA_FullscreenPush;

typedef struct {
    VkImage        image;
    VkDeviceMemory memory;
    VkImageView    view;
} VANILLA_HdrImage;

static uint16_t          hdrPipelineCount = 0;
static int32_t           firstHdrPipeline = -1;

static VANILLA_HdrImage* hdrImages  = NULL;   /* per pipeline (HDR ones only), maybe multisampled */
static VANILLA_HdrImage* fxaaInputs = NULL;   /* per pipeline (HDR ones only), tone mapped, 1 sample */

static VkSampler             linearSampler = VK_NULL_HANDLE;
static VkDescriptorSetLayout setLayout     = VK_NULL_HANDLE;
static VkDescriptorPool      setPool       = VK_NULL_HANDLE;
static VkDescriptorSet*      toneSets      = NULL;   /* per pipeline: reads hdrImages[p]  */
static VkDescriptorSet*      fxaaSets      = NULL;   /* per pipeline: reads fxaaInputs[p] */
static VkPipelineLayout      fullscreenLayout = VK_NULL_HANDLE;

static VkPipeline toneSingle = VK_NULL_HANDLE;
static VkPipeline toneMulti  = VK_NULL_HANDLE;
static VkPipeline toneDirect = VK_NULL_HANDLE;
static VkPipeline fxaaSingle = VK_NULL_HANDLE;
static VkPipeline fxaaDirect = VK_NULL_HANDLE;

/* --------------------------------------------------------- */
/* Queries                                                    */
/* --------------------------------------------------------- */

bool VANILLA_VK_isHdrPipeline(uint16_t pipeline) {
    return pipelineInfos != NULL && pipeline < vulkan.pipelineCount && pipelineInfos[pipeline].hdr;
}

VkFormat VANILLA_VK_pipelineColorFormat(uint16_t pipeline) {
    return VANILLA_VK_isHdrPipeline(pipeline) ? VANILLA_HDR_FORMAT : vulkan.swapchainFormat;
}

int32_t VANILLA_VK_firstHdrPipeline(void) { return firstHdrPipeline; }

VkImage VANILLA_VK_hdrImage(uint16_t p)       { return (hdrImages  && p < vulkan.pipelineCount) ? hdrImages[p].image  : VK_NULL_HANDLE; }
VkImageView VANILLA_VK_hdrView(uint16_t p)    { return (hdrImages  && p < vulkan.pipelineCount) ? hdrImages[p].view   : VK_NULL_HANDLE; }
VkImage VANILLA_VK_fxaaInputImage(uint16_t p) { return (fxaaInputs && p < vulkan.pipelineCount) ? fxaaInputs[p].image : VK_NULL_HANDLE; }
VkImageView VANILLA_VK_fxaaInputView(uint16_t p) { return (fxaaInputs && p < vulkan.pipelineCount) ? fxaaInputs[p].view : VK_NULL_HANDLE; }

/* --------------------------------------------------------- */
/* Init / destroy                                             */
/* --------------------------------------------------------- */

void VANILLA_VK_destroyHdr(void);

static int VANILLA_VK_createHdrImage(VANILLA_HdrImage* out, VkFormat format, VkSampleCountFlagBits samples) {
    VkImageCreateInfo imageInfo = {
        .sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO,
        .imageType = VK_IMAGE_TYPE_2D,
        .format = format,
        .extent = { vulkan.swapchainExtent.width, vulkan.swapchainExtent.height, 1 },
        .mipLevels = 1,
        .arrayLayers = 1,
        .samples = samples,
        .tiling = VK_IMAGE_TILING_OPTIMAL,
        .usage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT,
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

    VkImageViewCreateInfo viewInfo = {
        .sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO,
        .image = out->image,
        .viewType = VK_IMAGE_VIEW_TYPE_2D,
        .format = format,
        .subresourceRange = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1 }
    };

    if (vkCreateImageView(vulkan.device, &viewInfo, NULL, &out->view) != VK_SUCCESS) {
        out->view = VK_NULL_HANDLE;
        return 1;
    }

    return 0;
}

static void VANILLA_VK_destroyHdrImage(VANILLA_HdrImage* image) {
    if (image->view)   { vkDestroyImageView(vulkan.device, image->view, NULL); }
    if (image->image)  { vkDestroyImage(vulkan.device, image->image, NULL); }
    if (image->memory) { VANILLA_gmunmap(image->memory); }
    *image = (VANILLA_HdrImage){0};
}

static VkPipeline VANILLA_VK_createFullscreenPipeline(const uint32_t* fragCode, size_t fragWords, bool withDepth, VkSampleCountFlagBits samples) {
    VkShaderModuleCreateInfo vertInfo = {
        .sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO,
        .codeSize = VANILLA_toneMapVertSpv_len * sizeof(uint32_t),
        .pCode = VANILLA_toneMapVertSpv
    };
    VkShaderModuleCreateInfo fragInfo = {
        .sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO,
        .codeSize = fragWords * sizeof(uint32_t),
        .pCode = fragCode
    };

    VkShaderModule vert = VK_NULL_HANDLE, frag = VK_NULL_HANDLE;
    if (vkCreateShaderModule(vulkan.device, &vertInfo, NULL, &vert) != VK_SUCCESS) { return VK_NULL_HANDLE; }
    if (vkCreateShaderModule(vulkan.device, &fragInfo, NULL, &frag) != VK_SUCCESS) {
        vkDestroyShaderModule(vulkan.device, vert, NULL);
        return VK_NULL_HANDLE;
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
        .rasterizationSamples = samples
    };
    VkPipelineDepthStencilStateCreateInfo depthStencil = {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO,
        .depthTestEnable = VK_FALSE,
        .depthWriteEnable = VK_FALSE
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

    VkPipelineRenderingCreateInfo renderingInfo = {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO,
        .colorAttachmentCount = 1,
        .pColorAttachmentFormats = &vulkan.swapchainFormat,
        .depthAttachmentFormat = withDepth ? VK_FORMAT_D32_SFLOAT : VK_FORMAT_UNDEFINED
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
        .layout = fullscreenLayout,
        .basePipelineIndex = -1
    };

    VkPipeline pipeline = VK_NULL_HANDLE;
    if (vkCreateGraphicsPipelines(vulkan.device, VK_NULL_HANDLE, 1, &pipelineInfo, NULL, &pipeline) != VK_SUCCESS) {
        pipeline = VK_NULL_HANDLE;
    }

    vkDestroyShaderModule(vulkan.device, vert, NULL);
    vkDestroyShaderModule(vulkan.device, frag, NULL);
    return pipeline;
}

static void VANILLA_VK_writeSet(VkDescriptorSet set, VkImageView view) {
    VkDescriptorImageInfo imageInfo = {
        .sampler = linearSampler,
        .imageView = view,
        .imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL
    };
    VkWriteDescriptorSet write = {
        .sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
        .dstSet = set,
        .dstBinding = 0,
        .descriptorCount = 1,
        .descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
        .pImageInfo = &imageInfo
    };
    vkUpdateDescriptorSets(vulkan.device, 1, &write, 0, NULL);
}

int VANILLA_VK_initHdr(void) {
    const uint16_t count = vulkan.pipelineCount;
    const VkSampleCountFlagBits samples = VANILLA_VK_msaaSamples();

    hdrPipelineCount = 0;
    firstHdrPipeline = -1;
    for (uint16_t p = 0; p < count; p++) {
        if (VANILLA_VK_isHdrPipeline(p)) {
            if (firstHdrPipeline < 0) { firstHdrPipeline = p; }
            hdrPipelineCount++;
        }
    }

    if (hdrPipelineCount == 0) { return 0; }   /* nothing to do: zero cost */

    hdrImages  = VANILLA_mmap(sizeof(VANILLA_HdrImage) * count);
    fxaaInputs = VANILLA_mmap(sizeof(VANILLA_HdrImage) * count);
    toneSets   = VANILLA_mmap(sizeof(VkDescriptorSet) * count);
    fxaaSets   = VANILLA_mmap(sizeof(VkDescriptorSet) * count);

    if (hdrImages == VANILLA_MAP_FAILED || fxaaInputs == VANILLA_MAP_FAILED ||
        toneSets  == VANILLA_MAP_FAILED || fxaaSets   == VANILLA_MAP_FAILED) {
        if (hdrImages  == VANILLA_MAP_FAILED) { hdrImages  = NULL; }
        if (fxaaInputs == VANILLA_MAP_FAILED) { fxaaInputs = NULL; }
        if (toneSets   == VANILLA_MAP_FAILED) { toneSets   = NULL; }
        if (fxaaSets   == VANILLA_MAP_FAILED) { fxaaSets   = NULL; }
        goto fail;
    }

    /* mmap memory is zeroed: every handle starts as VK_NULL_HANDLE. */

    for (uint16_t p = 0; p < count; p++) {
        if (!VANILLA_VK_isHdrPipeline(p)) { continue; }
        if (VANILLA_VK_createHdrImage(&hdrImages[p],  VANILLA_HDR_FORMAT,     samples))               { goto fail; }
        if (VANILLA_VK_createHdrImage(&fxaaInputs[p], vulkan.swapchainFormat, VK_SAMPLE_COUNT_1_BIT)) { goto fail; }
    }

    VkSamplerCreateInfo samplerInfo = {
        .sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO,
        .magFilter = VK_FILTER_LINEAR,      /* FXAA samples between pixels */
        .minFilter = VK_FILTER_LINEAR,
        .mipmapMode = VK_SAMPLER_MIPMAP_MODE_NEAREST,
        .addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE,
        .addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE,
        .addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE,
        .maxLod = 0.0f
    };
    if (vkCreateSampler(vulkan.device, &samplerInfo, NULL, &linearSampler) != VK_SUCCESS) {
        linearSampler = VK_NULL_HANDLE;
        goto fail;
    }

    VkDescriptorSetLayoutBinding binding = {
        .binding = 0,
        .descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
        .descriptorCount = 1,
        .stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT
    };
    VkDescriptorSetLayoutCreateInfo layoutInfo = {
        .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO,
        .bindingCount = 1,
        .pBindings = &binding
    };
    if (vkCreateDescriptorSetLayout(vulkan.device, &layoutInfo, NULL, &setLayout) != VK_SUCCESS) {
        setLayout = VK_NULL_HANDLE;
        goto fail;
    }

    VkDescriptorPoolSize poolSize = { VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 2u * hdrPipelineCount };
    VkDescriptorPoolCreateInfo poolInfo = {
        .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO,
        .maxSets = 2u * hdrPipelineCount,
        .poolSizeCount = 1,
        .pPoolSizes = &poolSize
    };
    if (vkCreateDescriptorPool(vulkan.device, &poolInfo, NULL, &setPool) != VK_SUCCESS) {
        setPool = VK_NULL_HANDLE;
        goto fail;
    }

    for (uint16_t p = 0; p < count; p++) {
        if (!VANILLA_VK_isHdrPipeline(p)) { continue; }

        VkDescriptorSetLayout layouts[2] = { setLayout, setLayout };
        VkDescriptorSet allocated[2];
        VkDescriptorSetAllocateInfo allocInfo = {
            .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO,
            .descriptorPool = setPool,
            .descriptorSetCount = 2,
            .pSetLayouts = layouts
        };
        if (vkAllocateDescriptorSets(vulkan.device, &allocInfo, allocated) != VK_SUCCESS) { goto fail; }

        toneSets[p] = allocated[0];
        fxaaSets[p] = allocated[1];
        VANILLA_VK_writeSet(toneSets[p], hdrImages[p].view);
        VANILLA_VK_writeSet(fxaaSets[p], fxaaInputs[p].view);
    }

    VkPushConstantRange pushRange = {
        .stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT,
        .offset = 0,
        .size = sizeof(VANILLA_FullscreenPush)
    };
    VkPipelineLayoutCreateInfo pipelineLayoutInfo = {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO,
        .setLayoutCount = 1,
        .pSetLayouts = &setLayout,
        .pushConstantRangeCount = 1,
        .pPushConstantRanges = &pushRange
    };
    if (vkCreatePipelineLayout(vulkan.device, &pipelineLayoutInfo, NULL, &fullscreenLayout) != VK_SUCCESS) {
        fullscreenLayout = VK_NULL_HANDLE;
        goto fail;
    }

    const bool multi = samples > VK_SAMPLE_COUNT_1_BIT;

    toneSingle = VANILLA_VK_createFullscreenPipeline(VANILLA_toneMapFragSpv,   VANILLA_toneMapFragSpv_len,   false, VK_SAMPLE_COUNT_1_BIT);
    toneMulti  = multi ? VANILLA_VK_createFullscreenPipeline(VANILLA_toneMapMsFragSpv, VANILLA_toneMapMsFragSpv_len, false, VK_SAMPLE_COUNT_1_BIT) : VK_NULL_HANDLE;
    toneDirect = multi
        ? VANILLA_VK_createFullscreenPipeline(VANILLA_toneMapMsFragSpv, VANILLA_toneMapMsFragSpv_len, true, samples)
        : VANILLA_VK_createFullscreenPipeline(VANILLA_toneMapFragSpv,   VANILLA_toneMapFragSpv_len,   true, samples);
    fxaaSingle = VANILLA_VK_createFullscreenPipeline(VANILLA_fxaaFragSpv, VANILLA_fxaaFragSpv_len, false, VK_SAMPLE_COUNT_1_BIT);
    fxaaDirect = VANILLA_VK_createFullscreenPipeline(VANILLA_fxaaFragSpv, VANILLA_fxaaFragSpv_len, true,  samples);

    if (toneSingle == VK_NULL_HANDLE || toneDirect == VK_NULL_HANDLE ||
        fxaaSingle == VK_NULL_HANDLE || fxaaDirect == VK_NULL_HANDLE ||
        (multi && toneMulti == VK_NULL_HANDLE)) {
        goto fail;
    }

    return 0;

fail:
    printf("[VANILLA WINDOW] HDR init failed\n");
    VANILLA_VK_destroyHdr();
    return 1;
}

void VANILLA_VK_destroyHdr(void) {
    const uint16_t count = vulkan.pipelineCount;

    VkPipeline* pipelines[] = { &toneSingle, &toneMulti, &toneDirect, &fxaaSingle, &fxaaDirect };
    for (size_t i = 0; i < sizeof(pipelines) / sizeof(pipelines[0]); i++) {
        if (*pipelines[i] != VK_NULL_HANDLE) {
            vkDestroyPipeline(vulkan.device, *pipelines[i], NULL);
            *pipelines[i] = VK_NULL_HANDLE;
        }
    }

    if (fullscreenLayout != VK_NULL_HANDLE) { vkDestroyPipelineLayout(vulkan.device, fullscreenLayout, NULL); fullscreenLayout = VK_NULL_HANDLE; }
    if (setPool != VK_NULL_HANDLE)          { vkDestroyDescriptorPool(vulkan.device, setPool, NULL);          setPool = VK_NULL_HANDLE; }
    if (setLayout != VK_NULL_HANDLE)        { vkDestroyDescriptorSetLayout(vulkan.device, setLayout, NULL);   setLayout = VK_NULL_HANDLE; }
    if (linearSampler != VK_NULL_HANDLE)    { vkDestroySampler(vulkan.device, linearSampler, NULL);           linearSampler = VK_NULL_HANDLE; }

    for (uint16_t p = 0; p < count; p++) {
        if (hdrImages)  { VANILLA_VK_destroyHdrImage(&hdrImages[p]); }
        if (fxaaInputs) { VANILLA_VK_destroyHdrImage(&fxaaInputs[p]); }
    }

    if (hdrImages)  { VANILLA_munmap(hdrImages,  sizeof(VANILLA_HdrImage) * count); hdrImages  = NULL; }
    if (fxaaInputs) { VANILLA_munmap(fxaaInputs, sizeof(VANILLA_HdrImage) * count); fxaaInputs = NULL; }
    if (toneSets)   { VANILLA_munmap(toneSets,   sizeof(VkDescriptorSet) * count);  toneSets   = NULL; }
    if (fxaaSets)   { VANILLA_munmap(fxaaSets,   sizeof(VkDescriptorSet) * count);  fxaaSets   = NULL; }

    hdrPipelineCount = 0;
    firstHdrPipeline = -1;
}

/* --------------------------------------------------------- */
/* Recording helpers                                          */
/* --------------------------------------------------------- */

/* Any color image: (discard) -> attachment, or attachment -> sampled. */
void VANILLA_VK_colorBarrier(VkCommandBuffer commandBuffer, VkImage image, bool toAttachment) {
    VkImageMemoryBarrier2 barrier = {
        .sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2,
        .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
        .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
        .image = image,
        .subresourceRange = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1 }
    };

    if (toAttachment) {
        barrier.srcStageMask  = VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT | VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT;
        barrier.srcAccessMask = VK_ACCESS_2_NONE;
        barrier.dstStageMask  = VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT;
        barrier.dstAccessMask = VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT | VK_ACCESS_2_COLOR_ATTACHMENT_READ_BIT;
        barrier.oldLayout     = VK_IMAGE_LAYOUT_UNDEFINED;
        barrier.newLayout     = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
    }
    else {
        barrier.srcStageMask  = VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT;
        barrier.srcAccessMask = VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT;
        barrier.dstStageMask  = VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT;
        barrier.dstAccessMask = VK_ACCESS_2_SHADER_SAMPLED_READ_BIT;
        barrier.oldLayout     = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
        barrier.newLayout     = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    }

    VkDependencyInfo dependencyInfo = {
        .sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO,
        .imageMemoryBarrierCount = 1,
        .pImageMemoryBarriers = &barrier
    };
    vkCmdPipelineBarrier2(commandBuffer, &dependencyInfo);
}

void VANILLA_VK_hdrBarrier(VkCommandBuffer commandBuffer, uint16_t pipeline, bool toAttachment) {
    VANILLA_VK_colorBarrier(commandBuffer, hdrImages[pipeline].image, toAttachment);
}

/* Starts a color-only, full-screen rendering into `view` (1 sample, contents discarded). */
void VANILLA_VK_beginFullscreen(VkCommandBuffer commandBuffer, VkImageView view) {
    VkRenderingAttachmentInfo target = {
        .sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO,
        .imageView = view,
        .imageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
        .resolveMode = VK_RESOLVE_MODE_NONE,
        .loadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE,
        .storeOp = VK_ATTACHMENT_STORE_OP_STORE
    };

    VkRenderingInfo rendering = {
        .sType = VK_STRUCTURE_TYPE_RENDERING_INFO,
        .renderArea = { .offset = { 0, 0 }, .extent = vulkan.swapchainExtent },
        .layerCount = 1,
        .colorAttachmentCount = 1,
        .pColorAttachments = &target
    };

    vkCmdBeginRendering(commandBuffer, &rendering);
}

static void VANILLA_VK_drawFullscreen(VkCommandBuffer commandBuffer, VkPipeline pipeline, VkDescriptorSet set, const VANILLA_FullscreenPush* push) {
    VkViewport viewport = {
        .x = 0.0f, .y = 0.0f,
        .width = (float)vulkan.swapchainExtent.width,
        .height = (float)vulkan.swapchainExtent.height,
        .minDepth = 0.0f, .maxDepth = 1.0f
    };
    VkRect2D scissor = { .offset = { 0, 0 }, .extent = vulkan.swapchainExtent };

    vkCmdSetViewport(commandBuffer, 0, 1, &viewport);
    vkCmdSetScissor(commandBuffer, 0, 1, &scissor);

    vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline);
    vkCmdBindDescriptorSets(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, fullscreenLayout, 0, 1, &set, 0, NULL);
    vkCmdPushConstants(commandBuffer, fullscreenLayout, VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof(*push), push);

    vkCmdDraw(commandBuffer, 3, 1, 0, 0);
}

/*
 * Tone maps `pipeline`'s HDR image into the current rendering.
 * direct = the swapchain pass (depth attachment + MSAA sample count), otherwise a
 * 1-sample color-only target.
 */
void VANILLA_VK_recordToneMap(VkCommandBuffer commandBuffer, uint16_t pipeline, bool direct) {
    VANILLA_FullscreenPush push = {0};
    push.a = exposure;
    uint32_t mode = (uint32_t)toneMapping;
    memcpy(&push.b, &mode, sizeof(mode));   /* shader reads it as uint */

    VkPipeline p = direct ? toneDirect
                          : (VANILLA_VK_msaaSamples() > VK_SAMPLE_COUNT_1_BIT ? toneMulti : toneSingle);
    VANILLA_VK_drawFullscreen(commandBuffer, p, toneSets[pipeline], &push);
}

/* FXAA from `pipeline`'s tone mapped image (fxaaInputs) into the current rendering. */
void VANILLA_VK_recordFxaa(VkCommandBuffer commandBuffer, uint16_t pipeline, bool direct) {
    VANILLA_FullscreenPush push = {0};
    push.a = 1.0f / (float)vulkan.swapchainExtent.width;
    push.b = 1.0f / (float)vulkan.swapchainExtent.height;

    VANILLA_VK_drawFullscreen(commandBuffer, direct ? fxaaDirect : fxaaSingle, fxaaSets[pipeline], &push);
}
