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
 * Anti-aliasing: one global setting for every graphics pipeline.
 *
 *   VANILLA_setAntiAliasing(type, samples)   before or after start
 *
 * MSAA: every pipeline renders into multisampled targets that are resolved into the
 *   normal images. HDR pipelines keep a multisampled HDR image and are resolved by the
 *   tone mapping pass (tone map per sample, then average), so very bright edges stay
 *   smooth. Changing the sample count at runtime rebuilds the pipelines and targets at
 *   the start of the next frame (one short hitch).
 * FXAA: screen-space pass on the tone mapped HDR (scene) pipelines only, so UI text is
 *   never blurred. Turning it on/off is free.
 */

#include "vanilla_window_vulkan_utils.h"
#include <stdint.h>
#include <stdbool.h>

/* --------------------------------------------------------- */
/* Settings                                                   */
/* --------------------------------------------------------- */

static VANILLA_AntiAliasing requestedType    = VANILLA_AA_NONE;
static uint32_t             requestedSamples = 1;
static bool                 aaDirty          = false;

static VANILLA_AntiAliasing appliedType      = VANILLA_AA_NONE;
static VkSampleCountFlagBits appliedSamples  = VK_SAMPLE_COUNT_1_BIT;

static bool typeUsesMsaa(VANILLA_AntiAliasing type) {
    return type == VANILLA_AA_MSAA || type == VANILLA_AA_MSAA_FXAA;
}

static bool typeUsesFxaa(VANILLA_AntiAliasing type) {
    return type == VANILLA_AA_FXAA || type == VANILLA_AA_MSAA_FXAA;
}

VANILLA_EXPORT void VANILLA_setAntiAliasing(VANILLA_AntiAliasing type, VANILLA_SampleCount samples) {
    if (type != VANILLA_AA_NONE && type != VANILLA_AA_MSAA &&
        type != VANILLA_AA_FXAA && type != VANILLA_AA_MSAA_FXAA) { return; }

    uint32_t s = typeUsesMsaa(type) ? (uint32_t)samples : 1;
    if (s == 0 || (s & (s - 1)) != 0) { s = 4; }   /* not a power of two: sensible default */

    requestedType    = type;
    requestedSamples = s;
    aaDirty          = true;
}

VANILLA_EXPORT VANILLA_AntiAliasing VANILLA_getAntiAliasing(void) { return appliedType; }
VANILLA_EXPORT VANILLA_SampleCount  VANILLA_getAntiAliasingSamples(void) { return (VANILLA_SampleCount)appliedSamples; }

VkSampleCountFlagBits VANILLA_VK_msaaSamples(void) { return appliedSamples; }
bool VANILLA_VK_fxaaEnabled(void) { return typeUsesFxaa(appliedType); }

/* Highest sample count <= wanted that color, depth and sampled MS images all support. */
static VkSampleCountFlagBits VANILLA_VK_supportedSamples(uint32_t wanted) {
    VkPhysicalDeviceProperties properties;
    vkGetPhysicalDeviceProperties(vulkan.physicalDevice, &properties);

    const VkSampleCountFlags supported =
        properties.limits.framebufferColorSampleCounts &
        properties.limits.framebufferDepthSampleCounts &
        properties.limits.sampledImageColorSampleCounts;

    for (uint32_t s = wanted; s > 1; s >>= 1) {
        if (supported & s) { return (VkSampleCountFlagBits)s; }
    }
    return VK_SAMPLE_COUNT_1_BIT;
}

/* Turns the request into the applied state. Returns true when the sample count changed. */
static bool VANILLA_VK_resolveRequest(void) {
    const VkSampleCountFlagBits samples = VANILLA_VK_supportedSamples(requestedSamples);
    const bool changed = (samples != appliedSamples);

    appliedType    = requestedType;
    appliedSamples = samples;
    aaDirty        = false;

    if (typeUsesMsaa(requestedType) && (uint32_t)samples != requestedSamples) {
        printf("[VANILLA WINDOW] MSAA x%u not supported, using x%u\n", requestedSamples, (uint32_t)samples);
    }
    return changed;
}

/* Called once in VANILLA_VK_init, before the pipelines are created. */
void VANILLA_VK_initAntiAliasing(void) {
    VANILLA_VK_resolveRequest();
}

/* --------------------------------------------------------- */
/* Multisampled targets                                       */
/* --------------------------------------------------------- */

typedef struct {
    VkImage        image;
    VkDeviceMemory memory;
    VkImageView    view;
} VANILLA_MsaaTarget;

static VANILLA_MsaaTarget  msaaDepth          = {0};
static VANILLA_MsaaTarget  msaaSwapchainColor = {0};   /* direct path (no post-processing) */
static VANILLA_MsaaTarget* msaaColors         = NULL;  /* per pipeline, post-processing path, non-HDR */

static int VANILLA_VK_createMsaaTarget(VANILLA_MsaaTarget* target, VkFormat format, VkImageUsageFlags usage, VkImageAspectFlags aspect) {
    VkImageCreateInfo imageInfo = {
        .sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO,
        .imageType = VK_IMAGE_TYPE_2D,
        .format = format,
        .extent = { vulkan.swapchainExtent.width, vulkan.swapchainExtent.height, 1 },
        .mipLevels = 1,
        .arrayLayers = 1,
        .samples = appliedSamples,
        .tiling = VK_IMAGE_TILING_OPTIMAL,
        .usage = usage,
        .sharingMode = VK_SHARING_MODE_EXCLUSIVE,
        .initialLayout = VK_IMAGE_LAYOUT_UNDEFINED
    };

    if (vkCreateImage(vulkan.device, &imageInfo, NULL, &target->image) != VK_SUCCESS) {
        target->image = VK_NULL_HANDLE;
        return 1;
    }

    VkMemoryRequirements requirements;
    vkGetImageMemoryRequirements(vulkan.device, target->image, &requirements);

    target->memory = VANILLA_gmmap(&requirements, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
    if (target->memory == VK_NULL_HANDLE) { return 1; }
    if (vkBindImageMemory(vulkan.device, target->image, target->memory, 0) != VK_SUCCESS) { return 1; }

    VkImageViewCreateInfo viewInfo = {
        .sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO,
        .image = target->image,
        .viewType = VK_IMAGE_VIEW_TYPE_2D,
        .format = format,
        .subresourceRange = { aspect, 0, 1, 0, 1 }
    };

    if (vkCreateImageView(vulkan.device, &viewInfo, NULL, &target->view) != VK_SUCCESS) {
        target->view = VK_NULL_HANDLE;
        return 1;
    }

    return 0;
}

static void VANILLA_VK_destroyMsaaTarget(VANILLA_MsaaTarget* target) {
    if (target->view)   { vkDestroyImageView(vulkan.device, target->view, NULL); }
    if (target->image)  { vkDestroyImage(vulkan.device, target->image, NULL); }
    if (target->memory) { VANILLA_gmunmap(target->memory); }
    *target = (VANILLA_MsaaTarget){0};
}

/* The MSAA depth buffer keeps one layout forever: move it there once. */
static int VANILLA_VK_prepareMsaaDepth(void) {
    VkCommandBuffer commandBuffer = VANILLA_VK_beginSingleTimeCommands();
    if (commandBuffer == VK_NULL_HANDLE) { return 1; }

    VkImageMemoryBarrier2 barrier = {
        .sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2,
        .srcStageMask = VK_PIPELINE_STAGE_2_NONE,
        .srcAccessMask = VK_ACCESS_2_NONE,
        .dstStageMask = VK_PIPELINE_STAGE_2_EARLY_FRAGMENT_TESTS_BIT | VK_PIPELINE_STAGE_2_LATE_FRAGMENT_TESTS_BIT,
        .dstAccessMask = VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_READ_BIT | VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT,
        .oldLayout = VK_IMAGE_LAYOUT_UNDEFINED,
        .newLayout = VK_IMAGE_LAYOUT_DEPTH_ATTACHMENT_OPTIMAL,
        .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
        .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
        .image = msaaDepth.image,
        .subresourceRange = { VK_IMAGE_ASPECT_DEPTH_BIT, 0, 1, 0, 1 }
    };

    VkDependencyInfo dependencyInfo = {
        .sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO,
        .imageMemoryBarrierCount = 1,
        .pImageMemoryBarriers = &barrier
    };
    vkCmdPipelineBarrier2(commandBuffer, &dependencyInfo);

    return VANILLA_VK_endSingleTimeCommands(commandBuffer);
}

void VANILLA_VK_destroyMsaaTargets(void) {
    VANILLA_VK_destroyMsaaTarget(&msaaDepth);
    VANILLA_VK_destroyMsaaTarget(&msaaSwapchainColor);

    if (msaaColors != NULL) {
        for (uint16_t p = 0; p < vulkan.pipelineCount; p++) {
            VANILLA_VK_destroyMsaaTarget(&msaaColors[p]);
        }
        VANILLA_munmap(msaaColors, sizeof(VANILLA_MsaaTarget) * vulkan.pipelineCount);
        msaaColors = NULL;
    }
}

int VANILLA_VK_createMsaaTargets(void) {
    if (appliedSamples == VK_SAMPLE_COUNT_1_BIT) { return 0; }   /* nothing to create */

    if (VANILLA_VK_createMsaaTarget(&msaaDepth, VK_FORMAT_D32_SFLOAT,
            VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT,   /* read by the Hi-Z build */
            VK_IMAGE_ASPECT_DEPTH_BIT)) { goto fail; }
    if (VANILLA_VK_prepareMsaaDepth()) { goto fail; }

    if (enablePostProcessing) {
        msaaColors = VANILLA_mmap(sizeof(VANILLA_MsaaTarget) * vulkan.pipelineCount);
        if (msaaColors == VANILLA_MAP_FAILED) { msaaColors = NULL; goto fail; }

        /* HDR pipelines render into their own multisampled HDR image (hdr module). */
        for (uint16_t p = 0; p < vulkan.pipelineCount; p++) {
            if (VANILLA_VK_isHdrPipeline(p)) { continue; }
            if (VANILLA_VK_createMsaaTarget(&msaaColors[p], vulkan.swapchainFormat,
                    VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_TRANSIENT_ATTACHMENT_BIT,
                    VK_IMAGE_ASPECT_COLOR_BIT)) { goto fail; }
        }
    }
    else {
        if (VANILLA_VK_createMsaaTarget(&msaaSwapchainColor, vulkan.swapchainFormat,
                VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_TRANSIENT_ATTACHMENT_BIT,
                VK_IMAGE_ASPECT_COLOR_BIT)) { goto fail; }
    }

    return 0;

fail:
    printf("[VANILLA WINDOW] MSAA target creation failed\n");
    VANILLA_VK_destroyMsaaTargets();
    return 1;
}

VkImageView VANILLA_VK_msaaDepthView(void)          { return msaaDepth.view; }
VkImage     VANILLA_VK_msaaDepthImage(void)         { return msaaDepth.image; }
VkImage     VANILLA_VK_msaaSwapchainImage(void)     { return msaaSwapchainColor.image; }
VkImageView VANILLA_VK_msaaSwapchainView(void)      { return msaaSwapchainColor.view; }
VkImage     VANILLA_VK_msaaColorImage(uint16_t p)   { return (msaaColors && p < vulkan.pipelineCount) ? msaaColors[p].image : VK_NULL_HANDLE; }
VkImageView VANILLA_VK_msaaColorView(uint16_t p)    { return (msaaColors && p < vulkan.pipelineCount) ? msaaColors[p].view  : VK_NULL_HANDLE; }

/* --------------------------------------------------------- */
/* Runtime changes                                            */
/* --------------------------------------------------------- */

/*
 * Called at the start of every frame (module loop). FXAA-only changes are free;
 * a new sample count waits for the GPU and rebuilds pipelines + targets.
 */
int VANILLA_VK_applyAntiAliasingChanges(void) {
    if (!aaDirty) { return 0; }

    if (!VANILLA_VK_resolveRequest()) { return 0; }   /* same sample count: nothing to rebuild */

    if (vkDeviceWaitIdle(vulkan.device) != VK_SUCCESS) { return 1; }

    VANILLA_VK_destroySkyPipeline();
    VANILLA_VK_destroyGridPipeline();
    VANILLA_VK_destroyHdr();
    VANILLA_VK_destroyMsaaTargets();
    VANILLA_VK_destroyPipeline();

    if (VANILLA_VK_createPipeline())    { return 1; }
    if (VANILLA_VK_createMsaaTargets()) { return 1; }
    if (VANILLA_VK_initHdr())           { return 1; }
    if (VANILLA_VK_createSkyPipeline()) { return 1; }   /* sample count follows MSAA */
    if (VANILLA_VK_createGridPipeline()) { return 1; }
    VANILLA_VK_recreateHiZ();                            /* the MSAA depth it reads was recreated */

    printf("[VANILLA WINDOW] anti-aliasing: MSAA x%u%s\n",
        (uint32_t)appliedSamples, typeUsesFxaa(appliedType) ? " + FXAA" : "");
    return 0;
}
