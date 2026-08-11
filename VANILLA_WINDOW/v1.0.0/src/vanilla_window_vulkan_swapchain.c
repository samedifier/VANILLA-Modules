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

    if (formats == VANILLA_MAP_FAILED || !formats) { return VANILLA_ERR_ALLOC_FAILED; }

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
    if (presentModes == VANILLA_MAP_FAILED || !presentModes) { return VANILLA_ERR_ALLOC_FAILED; }

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

    VkExtent2D extent = {
        .width  = windowWidth,
        .height = windowHeight
    };

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

        return VANILLA_ERR_ALLOC_FAILED;
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

    if (vulkan.imageViews == VANILLA_MAP_FAILED || !vulkan.imageViews) { return VANILLA_ERR_ALLOC_FAILED; }

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

int VANILLA_VK_recreateSwapchain(void) {
    VkResult result = vkDeviceWaitIdle(vulkan.device);

    if (result != VK_SUCCESS) {
        return 1;
    }

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

    return 0;
}