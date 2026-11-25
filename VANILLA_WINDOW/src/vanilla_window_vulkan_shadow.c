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
 * Directional shadows: stable cascaded shadow maps.
 *
 * - One depth layer per cascade (2D array image).
 * - Cascades split the view range [near, shadowDistance] (practical split scheme).
 * - Every cascade is fitted with a bounding sphere and snapped to whole texels,
 *   so shadows do not shimmer while the camera moves or turns.
 * - The depth pass reuses the scene's indirect draws for pipelines with castShadows.
 *   Entities with VANILLA_ENTITYFLAGS1_NO_CAST_SHADOW are dropped in the vertex shader.
 * - Filtering (PCF / PCSS) happens in the scene fragment shader.
 *
 * Shadows are 3D only; 2D builds keep a 1x1 placeholder so descriptors stay valid.
 */

#include "vanilla_window_vulkan_utils.h"
#include "vanilla_window_shadowdepth_spv.h"
#include "vanilla_window_camera.h"
#include "vanilla_math.h"
#include <stdint.h>
#include <stdbool.h>
#include <math.h>

/* --------------------------------------------------------- */
/* Configuration                                              */
/* --------------------------------------------------------- */

#define VANILLA_SHADOW_MAX_RESOLUTION 8192
#define VANILLA_SHADOW_CASTER_MARGIN  60.0f   /* casters this far outside a cascade still cast */
#define VANILLA_SHADOW_SPLIT_LAMBDA   0.8f    /* 0 = uniform splits, 1 = logarithmic */
#define VANILLA_SHADOW_BLEND_RANGE    0.1f    /* last 10% of a cascade fades into the next */

static uint32_t shadowResolution   = 2048;
static uint32_t shadowCascadeCount = VANILLA_SHADOW_MAX_CASCADES;
static float    shadowDistance     = 100.0f;
static VANILLA_ShadowFilter shadowFilter = VANILLA_SHADOWFILTER_PCSS;

static bool didsetshadowresolution   = false;
static bool didsetshadowcascadecount = false;

/* Shadowed point / spot lights */
#define VANILLA_LOCAL_SHADOW_MAX_RESOLUTION 4096
#define VANILLA_LOCAL_SHADOW_FACE_FOV       1.6580628f   /* 95 deg: faces overlap, no seams under PCF */

static bool     shadowsSupported      = false;   /* false in 2D */

static uint32_t localShadowLimit      = 4;
static uint32_t localShadowResolution = 1024;
static bool     didsetlocalshadowlimit      = false;
static bool     didsetlocalshadowresolution = false;

static uint32_t* customDepthShader     = NULL;
static size_t    customDepthShaderSize = 0;

static uint32_t* customMaskVertShader     = NULL;
static size_t    customMaskVertShaderSize = 0;
static uint32_t* customMaskFragShader     = NULL;
static size_t    customMaskFragShaderSize = 0;

VANILLA_EXPORT void VANILLA_setShadowMapResolution(uint32_t resolution) {
    if (didstart || didsetshadowresolution) { return; }
    if (resolution < 256 || resolution > VANILLA_SHADOW_MAX_RESOLUTION) { return; }
    if ((resolution & (resolution - 1)) != 0) { return; }

    shadowResolution = resolution;
    didsetshadowresolution = true;
}

VANILLA_EXPORT void VANILLA_setShadowCascadeCount(uint32_t count) {
    if (didstart || didsetshadowcascadecount) { return; }
    if (count == 0 || count > VANILLA_SHADOW_MAX_CASCADES) { return; }

    shadowCascadeCount = count;
    didsetshadowcascadecount = true;
}

/*
 * Optional replacement for the embedded depth vertex shader (for custom vertex
 * layouts). Must keep the interface of shaders/shadowdepth.vert. Code is copied.
 */
VANILLA_EXPORT void VANILLA_setShadowDepthShader(const uint32_t* code, size_t size) {
    if (didstart || customDepthShader != NULL) { return; }
    if (code == NULL || size == 0 || (size % sizeof(uint32_t)) != 0) { return; }

    uint32_t* copy = VANILLA_mmap(size);
    if (copy == VANILLA_MAP_FAILED) { return; }

    memcpy(copy, code, size);

    customDepthShader     = copy;
    customDepthShaderSize = size;
}

/* How many point / spot lights may cast shadows at the same time (1..8, default 4). Before start, once. */
VANILLA_EXPORT void VANILLA_setLocalShadowLimit(uint32_t limit) {
    if (didstart || didsetlocalshadowlimit) { return; }
    if (limit == 0 || limit > VANILLA_LOCAL_SHADOW_MAX) { return; }

    localShadowLimit = limit;
    didsetlocalshadowlimit = true;
}

/* Point / spot shadow map resolution per face (256..4096, power of two, default 1024). Before start, once. */
VANILLA_EXPORT void VANILLA_setLocalShadowResolution(uint32_t resolution) {
    if (didstart || didsetlocalshadowresolution) { return; }
    if (resolution < 256 || resolution > VANILLA_LOCAL_SHADOW_MAX_RESOLUTION) { return; }
    if ((resolution & (resolution - 1)) != 0) { return; }

    localShadowResolution = resolution;
    didsetlocalshadowresolution = true;
}

uint32_t VANILLA_VK_getLocalShadowLimit(void) {
    return shadowsSupported ? localShadowLimit : 0;
}

/* Runtime: how far from the camera shadows reach. */
VANILLA_EXPORT void VANILLA_setShadowDistance(float distance) {
    if (distance <= 0.0f) { return; }
    shadowDistance = distance;
}

/* Runtime: PCF (fixed soft edge) or PCSS (contact-hardening). */
VANILLA_EXPORT void VANILLA_setShadowFilter(VANILLA_ShadowFilter filter) {
    if (filter != VANILLA_SHADOWFILTER_PCF && filter != VANILLA_SHADOWFILTER_PCSS) { return; }
    shadowFilter = filter;
}

static void VANILLA_VK_freeCustomDepthShader(void) {
    if (customDepthShader != NULL) {
        VANILLA_munmap(customDepthShader, customDepthShaderSize);
        customDepthShader     = NULL;
        customDepthShaderSize = 0;
    }
    if (customMaskVertShader != NULL) {
        VANILLA_munmap(customMaskVertShader, customMaskVertShaderSize);
        customMaskVertShader     = NULL;
        customMaskVertShaderSize = 0;
    }
    if (customMaskFragShader != NULL) {
        VANILLA_munmap(customMaskFragShader, customMaskFragShaderSize);
        customMaskFragShader     = NULL;
        customMaskFragShaderSize = 0;
    }
}

/*
 * Replaces the embedded alpha-tested shadow shaders (custom vertex layout). Copied. Before start, once.
 * The vertex shader keeps shadowmask.vert's interface (set 0 bindings 0 / 3, push constant, outputs
 * location 0 = UV, location 1 = flat material slot); the fragment shader discards below alphaCutoff.
 */
VANILLA_EXPORT void VANILLA_setShadowMaskShader(const uint32_t* vertexCode, size_t vertexSize, const uint32_t* fragmentCode, size_t fragmentSize) {
    if (didstart || customMaskVertShader != NULL) { return; }
    if (vertexCode == NULL || vertexSize == 0 || (vertexSize % sizeof(uint32_t)) != 0) { return; }
    if (fragmentCode == NULL || fragmentSize == 0 || (fragmentSize % sizeof(uint32_t)) != 0) { return; }

    uint32_t* vert = VANILLA_mmap(vertexSize);
    if (vert == VANILLA_MAP_FAILED) { return; }
    uint32_t* frag = VANILLA_mmap(fragmentSize);
    if (frag == VANILLA_MAP_FAILED) { VANILLA_munmap(vert, vertexSize); return; }

    memcpy(vert, vertexCode, vertexSize);
    memcpy(frag, fragmentCode, fragmentSize);

    customMaskVertShader = vert;  customMaskVertShaderSize = vertexSize;
    customMaskFragShader = frag;  customMaskFragShaderSize = fragmentSize;
}

/* --------------------------------------------------------- */
/* State                                                      */
/* --------------------------------------------------------- */

static uint32_t       shadowLayers     = 1;
static uint32_t       shadowExtent     = 1;
static VkFormat       shadowFormat     = VK_FORMAT_D32_SFLOAT;

static VkImage        shadowImage      = VK_NULL_HANDLE;
static VkDeviceMemory shadowMemory     = VK_NULL_HANDLE;
static VkImageView    shadowArrayView  = VK_NULL_HANDLE;
static VkImageView    shadowLayerViews[VANILLA_SHADOW_MAX_CASCADES] = {0};

static VkSampler      shadowCompareSampler = VK_NULL_HANDLE;
static VkSampler      shadowDepthSampler   = VK_NULL_HANDLE;

static VkPipeline     shadowPipeline   = VK_NULL_HANDLE;
static VkPipeline     shadowMaskPipeline = VK_NULL_HANDLE;   /* alpha-tested casters (glTF MASK) */

static uint32_t       localLayers      = 1;
static uint32_t       localExtent      = 1;
static VkImage        localImage       = VK_NULL_HANDLE;
static VkDeviceMemory localMemory      = VK_NULL_HANDLE;
static VkImageView    localArrayView   = VK_NULL_HANDLE;
static VkImageView    localLayerViews[VANILLA_LOCAL_SHADOW_MAX * VANILLA_LOCAL_SHADOW_FACES] = {0};

/* Filled by VANILLA_VK_fillLocalShadowInfo. */
static uint32_t       localActiveCount = 0;
static uint32_t       localFaceCounts[VANILLA_LOCAL_SHADOW_MAX];
static mat4           localMatrices[VANILLA_LOCAL_SHADOW_MAX * VANILLA_LOCAL_SHADOW_FACES];

/* Filled by VANILLA_VK_fillShadowInfo, used by the depth pass of the same frame. */
static bool           shadowActive     = false;
static mat4           cascadeMatrices[VANILLA_SHADOW_MAX_CASCADES];

/* --------------------------------------------------------- */
/* Small vector helpers                                       */
/* --------------------------------------------------------- */

static inline Vec3f v3(float x, float y, float z)      { return (Vec3f){ x, y, z }; }
static inline Vec3f v3add(Vec3f a, Vec3f b)             { return v3(a.x + b.x, a.y + b.y, a.z + b.z); }
static inline Vec3f v3sub(Vec3f a, Vec3f b)             { return v3(a.x - b.x, a.y - b.y, a.z - b.z); }
static inline Vec3f v3scale(Vec3f a, float s)           { return v3(a.x * s, a.y * s, a.z * s); }
static inline float v3dot(Vec3f a, Vec3f b)             { return a.x * b.x + a.y * b.y + a.z * b.z; }
static inline Vec3f v3cross(Vec3f a, Vec3f b) {
    return v3(a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x);
}
static inline Vec3f v3norm(Vec3f a) {
    float l = sqrtf(v3dot(a, a));
    return l > 1e-8f ? v3scale(a, 1.0f / l) : v3(0.0f, 0.0f, 1.0f);
}

/* --------------------------------------------------------- */
/* Initialization                                             */
/* --------------------------------------------------------- */

void VANILLA_VK_destroyShadows(void);

static bool VANILLA_VK_formatSupports(VkFormat format, VkFormatFeatureFlags features) {
    VkFormatProperties properties;
    vkGetPhysicalDeviceFormatProperties(vulkan.physicalDevice, format, &properties);
    return (properties.optimalTilingFeatures & features) == features;
}

static int VANILLA_VK_createShadowImage(void) {
    const VkFormatFeatureFlags needed =
        VK_FORMAT_FEATURE_DEPTH_STENCIL_ATTACHMENT_BIT |
        VK_FORMAT_FEATURE_SAMPLED_IMAGE_BIT |
        VK_FORMAT_FEATURE_TRANSFER_DST_BIT;

    if (VANILLA_VK_formatSupports(VK_FORMAT_D32_SFLOAT, needed)) {
        shadowFormat = VK_FORMAT_D32_SFLOAT;
    }
    else if (VANILLA_VK_formatSupports(VK_FORMAT_D16_UNORM, needed)) {
        shadowFormat = VK_FORMAT_D16_UNORM;
    }
    else {
        printf("[VANILLA WINDOW] no depth format usable for shadows\n");
        return 1;
    }

    VkImageCreateInfo imageInfo = {
        .sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO,
        .imageType = VK_IMAGE_TYPE_2D,
        .format = shadowFormat,
        .extent = { shadowExtent, shadowExtent, 1 },
        .mipLevels = 1,
        .arrayLayers = shadowLayers,
        .samples = VK_SAMPLE_COUNT_1_BIT,
        .tiling = VK_IMAGE_TILING_OPTIMAL,
        .usage = VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT |
                 VK_IMAGE_USAGE_SAMPLED_BIT |
                 VK_IMAGE_USAGE_TRANSFER_DST_BIT,
        .sharingMode = VK_SHARING_MODE_EXCLUSIVE,
        .initialLayout = VK_IMAGE_LAYOUT_UNDEFINED
    };

    if (vkCreateImage(vulkan.device, &imageInfo, NULL, &shadowImage) != VK_SUCCESS) {
        shadowImage = VK_NULL_HANDLE;
        return 1;
    }

    VkMemoryRequirements requirements;
    vkGetImageMemoryRequirements(vulkan.device, shadowImage, &requirements);

    shadowMemory = VANILLA_gmmap(&requirements, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
    if (shadowMemory == VK_NULL_HANDLE) { return 1; }

    if (vkBindImageMemory(vulkan.device, shadowImage, shadowMemory, 0) != VK_SUCCESS) { return 1; }

    VkImageViewCreateInfo viewInfo = {
        .sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO,
        .image = shadowImage,
        .viewType = VK_IMAGE_VIEW_TYPE_2D_ARRAY,
        .format = shadowFormat,
        .subresourceRange = { VK_IMAGE_ASPECT_DEPTH_BIT, 0, 1, 0, shadowLayers }
    };

    if (vkCreateImageView(vulkan.device, &viewInfo, NULL, &shadowArrayView) != VK_SUCCESS) {
        shadowArrayView = VK_NULL_HANDLE;
        return 1;
    }

    for (uint32_t i = 0; i < shadowLayers; i++) {
        VkImageViewCreateInfo layerInfo = {
            .sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO,
            .image = shadowImage,
            .viewType = VK_IMAGE_VIEW_TYPE_2D,
            .format = shadowFormat,
            .subresourceRange = { VK_IMAGE_ASPECT_DEPTH_BIT, 0, 1, i, 1 }
        };

        if (vkCreateImageView(vulkan.device, &layerInfo, NULL, &shadowLayerViews[i]) != VK_SUCCESS) {
            shadowLayerViews[i] = VK_NULL_HANDLE;
            return 1;
        }
    }

    /* Hardware comparison (PCF taps). Linear gives 2x2 bilinear PCF per tap when supported. */
    const bool linear = VANILLA_VK_formatSupports(shadowFormat, VK_FORMAT_FEATURE_SAMPLED_IMAGE_FILTER_LINEAR_BIT);

    VkSamplerCreateInfo compareInfo = {
        .sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO,
        .magFilter = linear ? VK_FILTER_LINEAR : VK_FILTER_NEAREST,
        .minFilter = linear ? VK_FILTER_LINEAR : VK_FILTER_NEAREST,
        .mipmapMode = VK_SAMPLER_MIPMAP_MODE_NEAREST,
        .addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_BORDER,
        .addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_BORDER,
        .addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE,
        .borderColor = VK_BORDER_COLOR_FLOAT_OPAQUE_WHITE,   /* outside = far = lit */
        .compareEnable = VK_TRUE,
        .compareOp = VK_COMPARE_OP_LESS_OR_EQUAL,
        .maxLod = 0.0f
    };

    if (vkCreateSampler(vulkan.device, &compareInfo, NULL, &shadowCompareSampler) != VK_SUCCESS) {
        shadowCompareSampler = VK_NULL_HANDLE;
        return 1;
    }

    /* Raw depth reads (PCSS blocker search). */
    VkSamplerCreateInfo depthInfo = compareInfo;
    depthInfo.magFilter = VK_FILTER_NEAREST;
    depthInfo.minFilter = VK_FILTER_NEAREST;
    depthInfo.compareEnable = VK_FALSE;
    depthInfo.compareOp = VK_COMPARE_OP_ALWAYS;

    if (vkCreateSampler(vulkan.device, &depthInfo, NULL, &shadowDepthSampler) != VK_SUCCESS) {
        shadowDepthSampler = VK_NULL_HANDLE;
        return 1;
    }

    return 0;
}

/* Clears every layer to 1.0 (lit) and leaves the image ready for sampling. */
static int VANILLA_VK_clearShadowImage(VkImage image, uint32_t layers) {
    VkCommandBuffer commandBuffer = VANILLA_VK_beginSingleTimeCommands();
    if (commandBuffer == VK_NULL_HANDLE) { return 1; }

    VkImageSubresourceRange range = { VK_IMAGE_ASPECT_DEPTH_BIT, 0, 1, 0, layers };

    VkImageMemoryBarrier2 toTransfer = {
        .sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2,
        .srcStageMask = VK_PIPELINE_STAGE_2_NONE,
        .srcAccessMask = VK_ACCESS_2_NONE,
        .dstStageMask = VK_PIPELINE_STAGE_2_TRANSFER_BIT,
        .dstAccessMask = VK_ACCESS_2_TRANSFER_WRITE_BIT,
        .oldLayout = VK_IMAGE_LAYOUT_UNDEFINED,
        .newLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
        .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
        .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
        .image = image,
        .subresourceRange = range
    };

    VkDependencyInfo toTransferInfo = {
        .sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO,
        .imageMemoryBarrierCount = 1,
        .pImageMemoryBarriers = &toTransfer
    };
    vkCmdPipelineBarrier2(commandBuffer, &toTransferInfo);

    VkClearDepthStencilValue clear = { 1.0f, 0 };
    vkCmdClearDepthStencilImage(commandBuffer, image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, &clear, 1, &range);

    VkImageMemoryBarrier2 toShader = toTransfer;
    toShader.srcStageMask  = VK_PIPELINE_STAGE_2_TRANSFER_BIT;
    toShader.srcAccessMask = VK_ACCESS_2_TRANSFER_WRITE_BIT;
    toShader.dstStageMask  = VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT | VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT;
    toShader.dstAccessMask = VK_ACCESS_2_SHADER_SAMPLED_READ_BIT;
    toShader.oldLayout     = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
    toShader.newLayout     = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;

    VkDependencyInfo toShaderInfo = {
        .sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO,
        .imageMemoryBarrierCount = 1,
        .pImageMemoryBarriers = &toShader
    };
    vkCmdPipelineBarrier2(commandBuffer, &toShaderInfo);

    return VANILLA_VK_endSingleTimeCommands(commandBuffer);
}

/* Depth array for point / spot shadows (format and samplers shared with the cascades). */
static int VANILLA_VK_createLocalShadowImage(void) {
    VkImageCreateInfo imageInfo = {
        .sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO,
        .imageType = VK_IMAGE_TYPE_2D,
        .format = shadowFormat,
        .extent = { localExtent, localExtent, 1 },
        .mipLevels = 1,
        .arrayLayers = localLayers,
        .samples = VK_SAMPLE_COUNT_1_BIT,
        .tiling = VK_IMAGE_TILING_OPTIMAL,
        .usage = VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT |
                 VK_IMAGE_USAGE_SAMPLED_BIT |
                 VK_IMAGE_USAGE_TRANSFER_DST_BIT,
        .sharingMode = VK_SHARING_MODE_EXCLUSIVE,
        .initialLayout = VK_IMAGE_LAYOUT_UNDEFINED
    };

    if (vkCreateImage(vulkan.device, &imageInfo, NULL, &localImage) != VK_SUCCESS) {
        localImage = VK_NULL_HANDLE;
        return 1;
    }

    VkMemoryRequirements requirements;
    vkGetImageMemoryRequirements(vulkan.device, localImage, &requirements);

    localMemory = VANILLA_gmmap(&requirements, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
    if (localMemory == VK_NULL_HANDLE) { return 1; }
    if (vkBindImageMemory(vulkan.device, localImage, localMemory, 0) != VK_SUCCESS) { return 1; }

    VkImageViewCreateInfo viewInfo = {
        .sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO,
        .image = localImage,
        .viewType = VK_IMAGE_VIEW_TYPE_2D_ARRAY,
        .format = shadowFormat,
        .subresourceRange = { VK_IMAGE_ASPECT_DEPTH_BIT, 0, 1, 0, localLayers }
    };

    if (vkCreateImageView(vulkan.device, &viewInfo, NULL, &localArrayView) != VK_SUCCESS) {
        localArrayView = VK_NULL_HANDLE;
        return 1;
    }

    for (uint32_t i = 0; i < localLayers; i++) {
        VkImageViewCreateInfo layerInfo = viewInfo;
        layerInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
        layerInfo.subresourceRange.baseArrayLayer = i;
        layerInfo.subresourceRange.layerCount = 1;

        if (vkCreateImageView(vulkan.device, &layerInfo, NULL, &localLayerViews[i]) != VK_SUCCESS) {
            localLayerViews[i] = VK_NULL_HANDLE;
            return 1;
        }
    }

    return 0;
}

/* Depth-only (fragCode NULL) or alpha-tested shadow pipeline. */
static int VANILLA_VK_createShadowPipeline(const uint32_t* vertCode, size_t vertBytes, const uint32_t* fragCode, size_t fragBytes, VkPipeline* outPipeline) {
    VkShaderModuleCreateInfo shaderInfo = {
        .sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO,
        .codeSize = vertBytes,
        .pCode = vertCode
    };

    VkShaderModule shaderModule = VK_NULL_HANDLE;
    if (vkCreateShaderModule(vulkan.device, &shaderInfo, NULL, &shaderModule) != VK_SUCCESS) { return 1; }

    VkShaderModule fragModule = VK_NULL_HANDLE;
    if (fragCode != NULL) {
        VkShaderModuleCreateInfo fragInfo = {
            .sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO,
            .codeSize = fragBytes,
            .pCode = fragCode
        };
        if (vkCreateShaderModule(vulkan.device, &fragInfo, NULL, &fragModule) != VK_SUCCESS) {
            vkDestroyShaderModule(vulkan.device, shaderModule, NULL);
            return 1;
        }
    }

    VkPipelineShaderStageCreateInfo stages[2] = {
        {
            .sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
            .stage = VK_SHADER_STAGE_VERTEX_BIT,
            .module = shaderModule,
            .pName = "main"
        },
        {
            .sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
            .stage = VK_SHADER_STAGE_FRAGMENT_BIT,
            .module = fragModule,
            .pName = "main"
        }
    };

    VkVertexInputBindingDescription bindingDesc;
    VkVertexInputAttributeDescription attrDescs[16];
    VkPipelineVertexInputStateCreateInfo vertexInput = VANILLA_makePipelineVertexInput(&bindingDesc, attrDescs);

    VkPipelineInputAssemblyStateCreateInfo inputAssembly = {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO,
        .topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST
    };

    VkPipelineViewportStateCreateInfo viewportState = {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO,
        .viewportCount = 1,
        .scissorCount = 1
    };

    /* No culling: open meshes and thin walls must still cast. Bias fights acne. */
    VkPipelineRasterizationStateCreateInfo rasterizer = {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO,
        .polygonMode = VK_POLYGON_MODE_FILL,
        .cullMode = VK_CULL_MODE_NONE,
        .frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE,
        .depthBiasEnable = VK_TRUE,
        .depthBiasConstantFactor = 1.0f,
        .depthBiasSlopeFactor = 1.75f,
        .lineWidth = 1.0f
    };

    VkPipelineMultisampleStateCreateInfo multisample = {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO,
        .rasterizationSamples = VK_SAMPLE_COUNT_1_BIT
    };

    VkPipelineDepthStencilStateCreateInfo depthStencil = {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO,
        .depthTestEnable = VK_TRUE,
        .depthWriteEnable = VK_TRUE,
        .depthCompareOp = VK_COMPARE_OP_LESS_OR_EQUAL
    };

    VkPipelineColorBlendStateCreateInfo colorBlend = {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO,
        .attachmentCount = 0
    };

    VkDynamicState dynamicStates[] = { VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR };
    VkPipelineDynamicStateCreateInfo dynamicState = {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO,
        .dynamicStateCount = 2,
        .pDynamicStates = dynamicStates
    };

    VkPipelineRenderingCreateInfo renderingInfo = {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO,
        .colorAttachmentCount = 0,
        .depthAttachmentFormat = shadowFormat
    };

    VkGraphicsPipelineCreateInfo pipelineInfo = {
        .sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO,
        .pNext = &renderingInfo,
        .stageCount = fragCode != NULL ? 2 : 1,
        .pStages = stages,
        .pVertexInputState = &vertexInput,
        .pInputAssemblyState = &inputAssembly,
        .pViewportState = &viewportState,
        .pRasterizationState = &rasterizer,
        .pMultisampleState = &multisample,
        .pDepthStencilState = &depthStencil,
        .pColorBlendState = &colorBlend,
        .pDynamicState = &dynamicState,
        .layout = vulkan.pipelineLayout,
        .basePipelineIndex = -1
    };

    VkResult result = vkCreateGraphicsPipelines(vulkan.device, VK_NULL_HANDLE, 1, &pipelineInfo, NULL, outPipeline);
    vkDestroyShaderModule(vulkan.device, shaderModule, NULL);
    if (fragModule != VK_NULL_HANDLE) { vkDestroyShaderModule(vulkan.device, fragModule, NULL); }

    if (result != VK_SUCCESS) {
        *outPipeline = VK_NULL_HANDLE;
        return 1;
    }

    return 0;
}

static int VANILLA_VK_createShadowPipelines(void) {
    const uint32_t* depthCode  = customDepthShader ? customDepthShader : VANILLA_shadowDepthSpv;
    const size_t    depthBytes = customDepthShader ? customDepthShaderSize : VANILLA_shadowDepthSpv_len * sizeof(uint32_t);

    const uint32_t* maskVert      = customMaskVertShader ? customMaskVertShader : VANILLA_shadowMaskVertSpv;
    const size_t    maskVertBytes = customMaskVertShader ? customMaskVertShaderSize : VANILLA_shadowMaskVertSpv_len * sizeof(uint32_t);
    const uint32_t* maskFrag      = customMaskFragShader ? customMaskFragShader : VANILLA_shadowMaskFragSpv;
    const size_t    maskFragBytes = customMaskFragShader ? customMaskFragShaderSize : VANILLA_shadowMaskFragSpv_len * sizeof(uint32_t);

    int failed = VANILLA_VK_createShadowPipeline(depthCode, depthBytes, NULL, 0, &shadowPipeline);
    if (!failed) {
        failed = VANILLA_VK_createShadowPipeline(maskVert, maskVertBytes, maskFrag, maskFragBytes, &shadowMaskPipeline);
    }

    VANILLA_VK_freeCustomDepthShader();   /* copies are not needed once the modules exist (or failed) */
    return failed;
}

int VANILLA_VK_initShadows(void) {
    /* 3D only, and only when the lighting module is in use. */
    shadowsSupported = (GET_DIMENSION(ANYCOREBuildID) == 3) && VANILLA_VK_isLightingSupported();

    shadowLayers = shadowsSupported ? shadowCascadeCount : 1;
    shadowExtent = shadowsSupported ? shadowResolution : 1;
    localLayers  = shadowsSupported ? localShadowLimit * VANILLA_LOCAL_SHADOW_FACES : 1;
    localExtent  = shadowsSupported ? localShadowResolution : 1;

    if (VANILLA_VK_createShadowImage())                          { goto fail; }
    if (VANILLA_VK_clearShadowImage(shadowImage, shadowLayers)) { goto fail; }
    if (VANILLA_VK_createLocalShadowImage())                     { goto fail; }
    if (VANILLA_VK_clearShadowImage(localImage, localLayers))   { goto fail; }

    if (shadowsSupported) {
        if (VANILLA_VK_createShadowPipelines()) { goto fail; }
    }
    else {
        VANILLA_VK_freeCustomDepthShader();
    }

    return 0;

fail:
    printf("[VANILLA WINDOW] shadow init failed\n");
    VANILLA_VK_destroyShadows();
    return 1;
}

void VANILLA_VK_destroyShadows(void) {
    VANILLA_VK_freeCustomDepthShader();

    if (shadowPipeline != VK_NULL_HANDLE) {
        vkDestroyPipeline(vulkan.device, shadowPipeline, NULL);
        shadowPipeline = VK_NULL_HANDLE;
    }
    if (shadowMaskPipeline != VK_NULL_HANDLE) {
        vkDestroyPipeline(vulkan.device, shadowMaskPipeline, NULL);
        shadowMaskPipeline = VK_NULL_HANDLE;
    }
    if (shadowCompareSampler != VK_NULL_HANDLE) {
        vkDestroySampler(vulkan.device, shadowCompareSampler, NULL);
        shadowCompareSampler = VK_NULL_HANDLE;
    }
    if (shadowDepthSampler != VK_NULL_HANDLE) {
        vkDestroySampler(vulkan.device, shadowDepthSampler, NULL);
        shadowDepthSampler = VK_NULL_HANDLE;
    }
    for (uint32_t i = 0; i < VANILLA_SHADOW_MAX_CASCADES; i++) {
        if (shadowLayerViews[i] != VK_NULL_HANDLE) {
            vkDestroyImageView(vulkan.device, shadowLayerViews[i], NULL);
            shadowLayerViews[i] = VK_NULL_HANDLE;
        }
    }
    for (uint32_t i = 0; i < VANILLA_LOCAL_SHADOW_MAX * VANILLA_LOCAL_SHADOW_FACES; i++) {
        if (localLayerViews[i] != VK_NULL_HANDLE) {
            vkDestroyImageView(vulkan.device, localLayerViews[i], NULL);
            localLayerViews[i] = VK_NULL_HANDLE;
        }
    }
    if (localArrayView != VK_NULL_HANDLE) {
        vkDestroyImageView(vulkan.device, localArrayView, NULL);
        localArrayView = VK_NULL_HANDLE;
    }
    if (localImage != VK_NULL_HANDLE) {
        vkDestroyImage(vulkan.device, localImage, NULL);
        localImage = VK_NULL_HANDLE;
    }
    if (localMemory != VK_NULL_HANDLE) {
        VANILLA_gmunmap(localMemory);
        localMemory = VK_NULL_HANDLE;
    }
    localActiveCount = 0;

    if (shadowArrayView != VK_NULL_HANDLE) {
        vkDestroyImageView(vulkan.device, shadowArrayView, NULL);
        shadowArrayView = VK_NULL_HANDLE;
    }
    if (shadowImage != VK_NULL_HANDLE) {
        vkDestroyImage(vulkan.device, shadowImage, NULL);
        shadowImage = VK_NULL_HANDLE;
    }
    if (shadowMemory != VK_NULL_HANDLE) {
        VANILLA_gmunmap(shadowMemory);
        shadowMemory = VK_NULL_HANDLE;
    }

    shadowActive = false;
}

/* No depth passes this frame (shadows or lighting turned off). */
void VANILLA_VK_clearShadowPasses(void) {
    shadowActive     = false;
    localActiveCount = 0;
}

void VANILLA_VK_getShadowImageInfos(VkDescriptorImageInfo* outCompare, VkDescriptorImageInfo* outDepth) {
    outCompare->sampler     = shadowCompareSampler;
    outCompare->imageView   = shadowArrayView;
    outCompare->imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;

    outDepth->sampler     = shadowDepthSampler;
    outDepth->imageView   = shadowArrayView;
    outDepth->imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
}

void VANILLA_VK_getLocalShadowImageInfos(VkDescriptorImageInfo* outCompare, VkDescriptorImageInfo* outDepth) {
    outCompare->sampler     = shadowCompareSampler;
    outCompare->imageView   = localArrayView;
    outCompare->imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;

    outDepth->sampler     = shadowDepthSampler;
    outDepth->imageView   = localArrayView;
    outDepth->imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
}

/* --------------------------------------------------------- */
/* Point / spot shadows                                       */
/* --------------------------------------------------------- */

/*
 * Builds the face matrices of the shadowed point / spot lights already copied
 * into info->localLights (count = info->localShadowCount).
 * Point: 6 faces (+X, -X, +Y, -Y, +Z, -Z), 95 deg so neighbouring faces overlap.
 * Spot: 1 face covering the outer cone.
 */
void VANILLA_VK_fillLocalShadowInfo(VANILLA_GPULightingInfo* info) {
    localActiveCount = 0;
    if (!shadowsSupported || shadowPipeline == VK_NULL_HANDLE) {
        info->localShadowCount = 0;
        return;
    }

    static const float faceDirs[6][3] = {
        {  1.0f,  0.0f,  0.0f }, { -1.0f,  0.0f,  0.0f },
        {  0.0f,  1.0f,  0.0f }, {  0.0f, -1.0f,  0.0f },
        {  0.0f,  0.0f,  1.0f }, {  0.0f,  0.0f, -1.0f }
    };

    uint32_t count = info->localShadowCount;
    if (count > localShadowLimit) { count = localShadowLimit; }

    for (uint32_t i = 0; i < count; i++) {
        const VANILLA_GPULight* light = &info->localLights[i];
        const Vec3f eye = light->position;

        const float farPlane  = light->range > 0.1f ? light->range : 0.1f;
        const float nearPlane = fmaxf(0.05f, farPlane * 0.002f);

        const bool  spot = (light->type == VANILLA_LIGHTTYPE_SPOT);
        uint32_t    faces = spot ? 1 : VANILLA_LOCAL_SHADOW_FACES;

        float fov = VANILLA_LOCAL_SHADOW_FACE_FOV;
        if (spot) {
            float outer = light->outerCone;
            if (outer < -1.0f) { outer = -1.0f; }
            if (outer >  1.0f) { outer =  1.0f; }
            fov = fminf(2.0f * acosf(outer) + 0.1f, 3.0f);
        }

        mat4 proj;
        VANILLA_MATH_perspective(&proj, fov, 1.0f, nearPlane, farPlane);

        for (uint32_t f = 0; f < faces; f++) {
            Vec3f dir = spot
                ? v3norm(light->direction)
                : v3(faceDirs[f][0], faceDirs[f][1], faceDirs[f][2]);

            const Vec3f up = (fabsf(dir.y) > 0.99f) ? v3(0.0f, 0.0f, 1.0f) : v3(0.0f, 1.0f, 0.0f);

            mat4 view, viewProj;
            VANILLA_MATH_lookAt(&view,
                eye.x, eye.y, eye.z,
                eye.x + dir.x, eye.y + dir.y, eye.z + dir.z,
                up.x, up.y, up.z);
            VANILLA_MATH_multiply(&viewProj, &proj, &view);

            const uint32_t layer = i * VANILLA_LOCAL_SHADOW_FACES + f;
            localMatrices[layer]             = viewProj;
            info->localShadowMatrices[layer] = viewProj;
        }

        info->localShadowParams[i][0] = nearPlane;
        info->localShadowParams[i][1] = farPlane;
        info->localShadowParams[i][2] = tanf(fov * 0.5f);
        info->localShadowParams[i][3] = (float)(i * VANILLA_LOCAL_SHADOW_FACES);
        info->localShadowExtra[i][1]  = (float)faces;

        localFaceCounts[i] = light->enabled ? faces : 0;   /* disabled: no depth pass */
    }

    info->localShadowCount = count;
    localActiveCount = count;
}

/* --------------------------------------------------------- */
/* Cascades                                                   */
/* --------------------------------------------------------- */

/*
 * Computes the cascades for this frame and writes them into the lighting info.
 * info == NULL or shadowLight == NULL: shadows off this frame.
 */
void VANILLA_VK_fillShadowInfo(VANILLA_GPULightingInfo* info, const VANILLA_GPULight* shadowLight, uint32_t shadowDirectional, float sourceSize) {
    shadowActive = false;

    if (info == NULL) { return; }

    info->shadowDirectional = UINT32_MAX;
    info->cascadeCount      = 0;

    if (!shadowsSupported || shadowLight == NULL || shadowPipeline == VK_NULL_HANDLE) { return; }

    /* Camera basis (same as VANILLA_MATH_lookAt). */
    const Vec3f eye     = v3(camera.posx, camera.posy, camera.posz);
    const Vec3f forward = v3norm(v3(camera.targetx, camera.targety, camera.targetz));
    const Vec3f right   = v3norm(v3cross(forward, v3(camera.upx, camera.upy, camera.upz)));
    const Vec3f up      = v3cross(right, forward);

    const float aspect = (float)vulkan.swapchainExtent.width / (float)vulkan.swapchainExtent.height;
    const bool  ortho  = (projectionType == VANILLA_PROJECTION_ORTHOGRAPHIC);

    const float nearPlane = camera.nearPlane > 0.0f ? camera.nearPlane : 0.01f;
    float farPlane = shadowDistance;
    if (camera.farPlane > nearPlane && camera.farPlane < farPlane) { farPlane = camera.farPlane; }
    if (farPlane <= nearPlane) { return; }

    const float tanV = tanf(camera.fov * 0.5f);
    const float tanH = tanV * aspect;

    /* Light basis: L points where the light travels. */
    const Vec3f L = v3norm(v3(shadowLight->direction.x, shadowLight->direction.y, shadowLight->direction.z));
    const Vec3f lightUp = (fabsf(L.y) > 0.99f) ? v3(0.0f, 0.0f, 1.0f) : v3(0.0f, 1.0f, 0.0f);

    const float halfRes = (float)shadowResolution * 0.5f;
    const float margin  = VANILLA_SHADOW_CASTER_MARGIN;

    float splitNear = nearPlane;

    for (uint32_t c = 0; c < shadowCascadeCount; c++) {
        /* Practical split: blend of uniform and logarithmic. */
        const float t        = (float)(c + 1) / (float)shadowCascadeCount;
        const float uniform  = nearPlane + (farPlane - nearPlane) * t;
        const float logSplit = nearPlane * powf(farPlane / nearPlane, t);
        const float splitFar = VANILLA_SHADOW_SPLIT_LAMBDA * logSplit + (1.0f - VANILLA_SHADOW_SPLIT_LAMBDA) * uniform;

        /* Slice corners. */
        Vec3f corners[8];
        const float depths[2] = { splitNear, splitFar };

        for (int k = 0; k < 2; k++) {
            const float d  = depths[k];
            const float hw = ortho ? orthographicSize * aspect * 0.5f : d * tanH;
            const float hh = ortho ? orthographicSize * 0.5f          : d * tanV;
            const Vec3f center = v3add(eye, v3scale(forward, d));

            corners[k * 4 + 0] = v3add(center, v3add(v3scale(right, -hw), v3scale(up, -hh)));
            corners[k * 4 + 1] = v3add(center, v3add(v3scale(right,  hw), v3scale(up, -hh)));
            corners[k * 4 + 2] = v3add(center, v3add(v3scale(right,  hw), v3scale(up,  hh)));
            corners[k * 4 + 3] = v3add(center, v3add(v3scale(right, -hw), v3scale(up,  hh)));
        }

        /* Bounding sphere. Radius is quantized so it does not jitter while turning. */
        Vec3f sphereCenter = v3(0.0f, 0.0f, 0.0f);
        for (int k = 0; k < 8; k++) { sphereCenter = v3add(sphereCenter, corners[k]); }
        sphereCenter = v3scale(sphereCenter, 1.0f / 8.0f);

        float radius = 0.0f;
        for (int k = 0; k < 8; k++) {
            Vec3f d = v3sub(corners[k], sphereCenter);
            float r = sqrtf(v3dot(d, d));
            if (r > radius) { radius = r; }
        }
        radius = ceilf(radius * 16.0f) / 16.0f;

        /* Light view + ortho box around the sphere, pulled back to catch outside casters. */
        const Vec3f lightEye = v3sub(sphereCenter, v3scale(L, radius + margin));

        mat4 view, proj, viewProj;
        VANILLA_MATH_lookAt(&view,
            lightEye.x, lightEye.y, lightEye.z,
            sphereCenter.x, sphereCenter.y, sphereCenter.z,
            lightUp.x, lightUp.y, lightUp.z);

        VANILLA_MATH_orthographic(&proj, -radius, radius, -radius, radius, 0.0f, 2.0f * (radius + margin));
        VANILLA_MATH_multiply(&viewProj, &proj, &view);

        /* Snap the world origin to whole texels: removes shimmering under camera motion. */
        const float originX = viewProj.m[12] * halfRes;
        const float originY = viewProj.m[13] * halfRes;
        proj.m[12] += (roundf(originX) - originX) / halfRes;
        proj.m[13] += (roundf(originY) - originY) / halfRes;
        VANILLA_MATH_multiply(&viewProj, &proj, &view);

        cascadeMatrices[c]        = viewProj;
        info->cascadeViewProj[c]  = viewProj;
        info->cascadeSplits[c]    = splitFar;
        info->cascadeRadius[c]    = radius;

        splitNear = splitFar;
    }

    info->shadowDirectional = shadowDirectional;
    info->cascadeCount      = shadowCascadeCount;
    info->shadowFilter      = (uint32_t)shadowFilter;
    info->shadowTexel       = 1.0f / (float)shadowResolution;
    info->cameraForward     = forward;
    info->shadowDistance    = farPlane;
    info->lightSize         = tanf(sourceSize * 0.5f);
    info->casterMargin      = margin;
    info->blendRange        = VANILLA_SHADOW_BLEND_RANGE;

    shadowActive = true;
}

/* --------------------------------------------------------- */
/* Depth pass                                                 */
/* --------------------------------------------------------- */

static void VANILLA_VK_depthBarrier(VkCommandBuffer commandBuffer, VkImage image, uint32_t layers, bool toAttachment) {
    VkImageMemoryBarrier2 barrier = {
        .sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2,
        .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
        .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
        .image = image,
        .subresourceRange = { VK_IMAGE_ASPECT_DEPTH_BIT, 0, 1, 0, layers }
    };

    if (toAttachment) {
        /* Previous contents are discarded (UNDEFINED); last frame's reads must be done. */
        barrier.srcStageMask  = VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT | VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT;
        barrier.srcAccessMask = VK_ACCESS_2_NONE;
        barrier.dstStageMask  = VK_PIPELINE_STAGE_2_EARLY_FRAGMENT_TESTS_BIT | VK_PIPELINE_STAGE_2_LATE_FRAGMENT_TESTS_BIT;
        barrier.dstAccessMask = VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_READ_BIT | VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
        barrier.oldLayout     = VK_IMAGE_LAYOUT_UNDEFINED;
        barrier.newLayout     = VK_IMAGE_LAYOUT_DEPTH_ATTACHMENT_OPTIMAL;
    }
    else {
        barrier.srcStageMask  = VK_PIPELINE_STAGE_2_EARLY_FRAGMENT_TESTS_BIT | VK_PIPELINE_STAGE_2_LATE_FRAGMENT_TESTS_BIT;
        barrier.srcAccessMask = VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
        barrier.dstStageMask  = VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT | VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT;
        barrier.dstAccessMask = VK_ACCESS_2_SHADER_SAMPLED_READ_BIT;
        barrier.oldLayout     = VK_IMAGE_LAYOUT_DEPTH_ATTACHMENT_OPTIMAL;
        barrier.newLayout     = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    }

    VkDependencyInfo dependencyInfo = {
        .sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO,
        .imageMemoryBarrierCount = 1,
        .pImageMemoryBarriers = &barrier
    };
    vkCmdPipelineBarrier2(commandBuffer, &dependencyInfo);
}

/* Clears one depth layer and draws every shadow caster into it with `viewProj`. */
static void VANILLA_VK_renderDepthLayer(VkCommandBuffer commandBuffer, VkImageView layerView, uint32_t extent, const mat4* viewProj) {

    VkBuffer vertexBuffers[] = { vulkan.globalVertexBuffer };
    VkDeviceSize offsets[] = { 0 };

    /* Normal (not flipped) viewport: uv = ndc * 0.5 + 0.5 in the shader. */
    VkViewport viewport = {
        .x = 0.0f, .y = 0.0f,
        .width = (float)extent, .height = (float)extent,
        .minDepth = 0.0f, .maxDepth = 1.0f
    };
    VkRect2D scissor = { .offset = { 0, 0 }, .extent = { extent, extent } };

    /* Only the casters inside this layer's frustum (outside any rendering). */
    VANILLA_VK_cullPass(commandBuffer, viewProj, true);

    VkRenderingAttachmentInfo depthAttachment = {
        .sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO,
        .imageView = layerView,
        .imageLayout = VK_IMAGE_LAYOUT_DEPTH_ATTACHMENT_OPTIMAL,
        .resolveMode = VK_RESOLVE_MODE_NONE,
        .loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR,
        .storeOp = VK_ATTACHMENT_STORE_OP_STORE,
        .clearValue.depthStencil = { 1.0f, 0 }
    };

    VkRenderingInfo renderingInfo = {
        .sType = VK_STRUCTURE_TYPE_RENDERING_INFO,
        .renderArea = { .offset = { 0, 0 }, .extent = { extent, extent } },
        .layerCount = 1,
        .colorAttachmentCount = 0,
        .pDepthAttachment = &depthAttachment
    };

    vkCmdBeginRendering(commandBuffer, &renderingInfo);

    VANILLA_CameraPush push = {0};
    push.viewProjection = *viewProj;

    vkCmdPushConstants(
        commandBuffer, vulkan.pipelineLayout,
        VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT,
        0, sizeof(VANILLA_CameraPush), &push);

    vkCmdSetViewport(commandBuffer, 0, 1, &viewport);
    vkCmdSetScissor(commandBuffer, 0, 1, &scissor);

    vkCmdBindVertexBuffers(commandBuffer, 0, 1, vertexBuffers, offsets);
    vkCmdBindIndexBuffer(commandBuffer, vulkan.globalIndexBuffer, 0, VK_INDEX_TYPE_UINT32);

    vkCmdBindDescriptorSets(
        commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS,
        vulkan.pipelineLayout, 0, 1, &vulkan.descriptorSet, 0, NULL);

    /* Opaque casters: depth only. Mask casters: alpha-tested. Blend (transparent) entities cast no shadow. */
    for (uint32_t mode = VANILLA_ALPHAMODE_OPAQUE; mode <= VANILLA_ALPHAMODE_MASK; mode++) {
        bool bound = false;

        for (uint16_t p = 0; p < vulkan.pipelineCount; p++) {
            if (!pipelineInfos[p].castShadows) { continue; }

            const uint32_t drawCount = VANILLA_VK_indirectCount(p, mode);
            if (drawCount == 0) { continue; }

            if (!bound) {
                vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS,
                    mode == VANILLA_ALPHAMODE_MASK ? shadowMaskPipeline : shadowPipeline);
                bound = true;
            }

            VANILLA_VK_drawList(commandBuffer, p, mode);
        }
    }

    vkCmdEndRendering(commandBuffer);
}

void VANILLA_VK_recordShadowPass(VkCommandBuffer commandBuffer) {
    if (shadowPipeline == VK_NULL_HANDLE) { return; }

    /* Directional cascades */
    if (shadowActive) {
        VANILLA_VK_depthBarrier(commandBuffer, shadowImage, shadowLayers, true);

        for (uint32_t c = 0; c < shadowLayers; c++) {
            VANILLA_VK_renderDepthLayer(commandBuffer, shadowLayerViews[c], shadowExtent, &cascadeMatrices[c]);
        }

        VANILLA_VK_depthBarrier(commandBuffer, shadowImage, shadowLayers, false);
    }

    /* Point / spot faces */
    if (localActiveCount > 0) {
        VANILLA_VK_depthBarrier(commandBuffer, localImage, localLayers, true);

        for (uint32_t i = 0; i < localActiveCount; i++) {
            for (uint32_t f = 0; f < localFaceCounts[i]; f++) {
                const uint32_t layer = i * VANILLA_LOCAL_SHADOW_FACES + f;
                VANILLA_VK_renderDepthLayer(commandBuffer, localLayerViews[layer], localExtent, &localMatrices[layer]);
            }
        }

        /* Layers not drawn this frame (spot faces 1-5, disabled lights) are undefined but never sampled. */
        VANILLA_VK_depthBarrier(commandBuffer, localImage, localLayers, false);
    }
}
