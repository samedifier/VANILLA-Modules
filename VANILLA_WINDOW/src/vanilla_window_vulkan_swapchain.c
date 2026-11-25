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

#include "vanilla_window_vulkan_utils.h"
#include "vanilla_window_glfw.h"

/* Set when acquire / present report OUT_OF_DATE or SUBOPTIMAL; loop() recreates before the next frame. */
bool VANILLA_VK_swapchainOutOfDate = false;

/*
 * Surface size: the surface's own extent when it reports one (X11, Windows), otherwise the framebuffer
 * size (Wayland), clamped to what the surface allows. glfwSetWindowSize is asynchronous, so this can
 * still be the old size right after a resize request; loop() notices the change on a later frame.
 */
static VkExtent2D VANILLA_VK_chooseExtent(const VkSurfaceCapabilitiesKHR* capabilities) {
    if (capabilities->currentExtent.width != UINT32_MAX) {
        return capabilities->currentExtent;
    }

    int width = 0, height = 0;
    glfwGetFramebufferSize(g_glfw.window, &width, &height);
    if (width <= 0 || height <= 0) { width = (int)windowWidth; height = (int)windowHeight; }

    VkExtent2D extent = { (uint32_t)width, (uint32_t)height };
    if (extent.width  < capabilities->minImageExtent.width)  { extent.width  = capabilities->minImageExtent.width; }
    if (extent.height < capabilities->minImageExtent.height) { extent.height = capabilities->minImageExtent.height; }
    if (extent.width  > capabilities->maxImageExtent.width)  { extent.width  = capabilities->maxImageExtent.width; }
    if (extent.height > capabilities->maxImageExtent.height) { extent.height = capabilities->maxImageExtent.height; }
    return extent;
}

int VANILLA_VK_createSwapchain(VkSwapchainKHR oldSwapchain) {
    VkSurfaceCapabilitiesKHR capabilities;

    vkGetPhysicalDeviceSurfaceCapabilitiesKHR(
        vulkan.physicalDevice,
        vulkan.surface,
        &capabilities
    );

    uint32_t formatCount = 0;

    vkGetPhysicalDeviceSurfaceFormatsKHR(
        vulkan.physicalDevice,
        vulkan.surface,
        &formatCount,
        NULL
    );

    if (!formatCount) { return 1; }

    VkSurfaceFormatKHR* formats = VANILLA_mmap(sizeof(VkSurfaceFormatKHR) * formatCount);

    if (formats == VANILLA_MAP_FAILED || !formats) { return 1; }

    vkGetPhysicalDeviceSurfaceFormatsKHR(
        vulkan.physicalDevice,
        vulkan.surface,
        &formatCount,
        formats
    );

    VkSurfaceFormatKHR surfaceFormat = formats[0];

    VANILLA_munmap(formats, sizeof(VkSurfaceFormatKHR) * formatCount);

    uint32_t presentModeCount = 0;

    vkGetPhysicalDeviceSurfacePresentModesKHR(
        vulkan.physicalDevice,
        vulkan.surface,
        &presentModeCount,
        NULL
    );

    if (!presentModeCount) { return 1; }

    VkPresentModeKHR* presentModes = VANILLA_mmap(sizeof(VkPresentModeKHR) * presentModeCount);
    if (presentModes == VANILLA_MAP_FAILED || !presentModes) { return 1; }

    vkGetPhysicalDeviceSurfacePresentModesKHR(
        vulkan.physicalDevice,
        vulkan.surface,
        &presentModeCount,
        presentModes
    );
    
    VkPresentModeKHR presentMode = VK_PRESENT_MODE_IMMEDIATE_KHR;
    if (enableVSync) { presentMode = VK_PRESENT_MODE_FIFO_KHR; }

    VANILLA_munmap(presentModes, sizeof(VkPresentModeKHR) * presentModeCount);

    uint32_t imageCount = capabilities.minImageCount + 1;
    if (capabilities.maxImageCount > 0 && imageCount > capabilities.maxImageCount) { imageCount = capabilities.maxImageCount; }

    const VkExtent2D extent = VANILLA_VK_chooseExtent(&capabilities);
    if (extent.width == 0 || extent.height == 0) { return 1; }   /* minimized: no swapchain possible */

    VkSwapchainCreateInfoKHR createInfo = {
        .sType = VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR,
        .surface = vulkan.surface,
        .minImageCount = imageCount,
        .imageFormat = surfaceFormat.format,
        .imageColorSpace = surfaceFormat.colorSpace,
        .imageExtent = extent,
        .imageArrayLayers = 1,
        .imageUsage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT,
        .imageSharingMode = VK_SHARING_MODE_EXCLUSIVE,
        .preTransform = capabilities.currentTransform,
        .compositeAlpha = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR,
        .presentMode = presentMode,
        .clipped = VK_TRUE,
        .oldSwapchain = oldSwapchain
    };


    VkSwapchainKHR newSwapchain = VK_NULL_HANDLE;
    VkResult result = vkCreateSwapchainKHR(vulkan.device, &createInfo, NULL, &newSwapchain);

    if (result != VK_SUCCESS) { return 1; }

    if (oldSwapchain != VK_NULL_HANDLE) {
        vkDestroySwapchainKHR(
            vulkan.device,
            oldSwapchain,
            NULL
        );
    }

    vulkan.swapchain = newSwapchain;
    vulkan.swapchainFormat = surfaceFormat.format;
    vulkan.swapchainExtent = extent;

    vkGetSwapchainImagesKHR(
        vulkan.device,
        vulkan.swapchain,
        &imageCount,
        NULL
    );

    vulkan.imageCount = imageCount;

    vulkan.images = VANILLA_mmap(sizeof(VkImage) * imageCount);

    if (vulkan.images == VANILLA_MAP_FAILED || !vulkan.images) {
        vkDestroySwapchainKHR(
            vulkan.device,
            vulkan.swapchain,
            NULL
        );

        vulkan.swapchain = VK_NULL_HANDLE;
        vulkan.imageCount = 0;
        return 1;
    }

    vkGetSwapchainImagesKHR(
        vulkan.device,
        vulkan.swapchain,
        &imageCount,
        vulkan.images
    );

    return 0;
}

int VANILLA_VK_createImageViews(void) {
    vulkan.imageViews = VANILLA_mmap(sizeof(VkImageView) * vulkan.imageCount);

    if (vulkan.imageViews == VANILLA_MAP_FAILED || !vulkan.imageViews) { return 1; }

    for (uint32_t i = 0; i < vulkan.imageCount; i++) {
        VkImageViewCreateInfo createInfo = {
            .sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO,
            .image = vulkan.images[i],
            .viewType = VK_IMAGE_VIEW_TYPE_2D,
            .format = vulkan.swapchainFormat,

            .components = {
                .r = VK_COMPONENT_SWIZZLE_IDENTITY,
                .g = VK_COMPONENT_SWIZZLE_IDENTITY,
                .b = VK_COMPONENT_SWIZZLE_IDENTITY,
                .a = VK_COMPONENT_SWIZZLE_IDENTITY
            },

            .subresourceRange = {
                .aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
                .baseMipLevel = 0,
                .levelCount = 1,
                .baseArrayLayer = 0,
                .layerCount = 1
            }
        };

        VkResult result =
            vkCreateImageView(
                vulkan.device,
                &createInfo,
                NULL,
                &vulkan.imageViews[i]
            );

        if (result != VK_SUCCESS) {
            for (uint32_t j = 0; j < i; j++) {
                vkDestroyImageView(vulkan.device, vulkan.imageViews[j], NULL);
            }

            VANILLA_munmap(vulkan.imageViews, sizeof(VkImageView) * vulkan.imageCount);
            vulkan.imageViews = NULL;

            return 1;
        }
    }

    return 0;
}

void VANILLA_VK_destroyImageViews(void) {
    if (!vulkan.imageViews) { return; }

    for (uint32_t i = 0; i < vulkan.imageCount; i++) {
        vkDestroyImageView(
            vulkan.device,
            vulkan.imageViews[i],
            NULL
        );
    }

    VANILLA_munmap(vulkan.imageViews, sizeof(VkImageView) * vulkan.imageCount);

    vulkan.imageViews = NULL;
}


void VANILLA_VK_destroySwapchain(void) {
    if (vulkan.images && vulkan.imageCount) {
        VANILLA_munmap(vulkan.images, sizeof(VkImage) * vulkan.imageCount);

        vulkan.images = NULL;
    }

    vulkan.imageCount = 0;

    if (vulkan.swapchain != VK_NULL_HANDLE) {
        vkDestroySwapchainKHR(
            vulkan.device,
            vulkan.swapchain,
            NULL
        );

        vulkan.swapchain = VK_NULL_HANDLE;
    }
}

/*
 * Everything sized like the swapchain, except the swapchain itself and the depth image:
 * HDR (+ FXAA) images, MSAA targets, post-processing pipeline / pp images and the pp descriptor set.
 * Same order as VANILLA_VK_init.
 */
static int VANILLA_VK_recreateSizeDependentResources(void) {
    VANILLA_VK_destroyHdr();
    VANILLA_VK_destroyMsaaTargets();
    if (enablePostProcessing) {
        VANILLA_VK_destroyPostProcessingImages();
        VANILLA_VK_destroyPipelineImages();
    }

    if (VANILLA_VK_initHdr())           { return 1; }
    if (VANILLA_VK_createMsaaTargets()) { return 1; }

    if (enablePostProcessing) {
        if (VANILLA_VK_createPipelineImages())       { return 1; }
        if (VANILLA_VK_createPostProcessingImages()) { return 1; }
        VANILLA_UpdatePostProcessingDescriptor();    /* no-op before the first loop() created it */
    }

    return 0;
}

int VANILLA_VK_recreateSwapchain(void) {
    VkResult result = vkDeviceWaitIdle(vulkan.device);

    if (result != VK_SUCCESS) {
        return 1;
    }

    /* Minimized (0 x 0): keep the old swapchain, try again on a later frame. */
    VkSurfaceCapabilitiesKHR capabilities;
    vkGetPhysicalDeviceSurfaceCapabilitiesKHR(vulkan.physicalDevice, vulkan.surface, &capabilities);
    const VkExtent2D target = VANILLA_VK_chooseExtent(&capabilities);
    if (target.width == 0 || target.height == 0) { return 1; }

    const VkExtent2D oldExtent = vulkan.swapchainExtent;

    /* One command buffer per swapchain image: freed with the old count, created with the new one. */
    VANILLA_VK_destroyCommandBuffers();

    VANILLA_VK_destroyDepthResources();
    VANILLA_VK_destroyImageViews();

    if (vulkan.images && vulkan.imageCount) {
        VANILLA_munmap(vulkan.images, sizeof(VkImage) * vulkan.imageCount);
        vulkan.images = NULL;
    }

    vulkan.imageCount = 0;

    VkSwapchainKHR oldSwapchain = vulkan.swapchain;
    if (VANILLA_VK_createSwapchain(oldSwapchain)) { return 1; }

    if (VANILLA_VK_createImageViews()) {
        VANILLA_VK_destroyImageViews();
        VANILLA_VK_destroySwapchain();

        return 1;
    }

    if (VANILLA_VK_createDepthResources()) {
        VANILLA_VK_destroyDepthResources();
        VANILLA_VK_destroyImageViews();
        VANILLA_VK_destroySwapchain();

        return 1;
    }

    if (VANILLA_VK_createCommandBuffers()) { return 1; }

    if (vulkan.swapchainExtent.width != oldExtent.width || vulkan.swapchainExtent.height != oldExtent.height) {
        if (VANILLA_VK_recreateSizeDependentResources()) {
            printf("[VANILLA WINDOW] resize: rebuilding render targets failed\n");
            return 1;
        }
        printf("[VANILLA WINDOW] resized to %ux%u\n", vulkan.swapchainExtent.width, vulkan.swapchainExtent.height);
    }

    /* The depth image is recreated on every swapchain rebuild (even VSync): the occlusion pyramid's reads too. */
    VANILLA_VK_recreateHiZ();

    VANILLA_VK_swapchainOutOfDate = false;
    return 0;
}