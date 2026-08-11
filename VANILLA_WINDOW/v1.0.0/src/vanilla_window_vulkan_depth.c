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

int VANILLA_VK_createDepthResources(void) {
    VkImageCreateInfo imageInfo = {
        .sType         = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO,
        .imageType     = VK_IMAGE_TYPE_2D,
        .extent.width  = vulkan.swapchainExtent.width,
        .extent.height = vulkan.swapchainExtent.height,
        .extent.depth  = 1,
        .mipLevels     = 1,
        .arrayLayers   = 1,
        .format        = VK_FORMAT_D32_SFLOAT,
        .tiling        = VK_IMAGE_TILING_OPTIMAL,
        .initialLayout = VK_IMAGE_LAYOUT_UNDEFINED,
        .usage         = VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT,
        .samples       = VK_SAMPLE_COUNT_1_BIT,
        .sharingMode   = VK_SHARING_MODE_EXCLUSIVE
    };

    if (vkCreateImage(vulkan.device, &imageInfo, NULL, &vulkan.depthImage) != VK_SUCCESS) { return 1; }

    VkMemoryRequirements memRequirements;
    vkGetImageMemoryRequirements(vulkan.device, vulkan.depthImage, &memRequirements);

    VkMemoryAllocateInfo allocInfo = {
        .sType           = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO,
        .allocationSize  = memRequirements.size,
        .memoryTypeIndex = VANILLA_FindMemoryType(vulkan.physicalDevice, memRequirements.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT)
    };

    if (vkAllocateMemory(vulkan.device, &allocInfo, NULL, &vulkan.depthImageMemory) != VK_SUCCESS) { return 1; }

    vkBindImageMemory(vulkan.device, vulkan.depthImage, vulkan.depthImageMemory, 0);

    VkImageViewCreateInfo viewInfo = {
        .sType    = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO,
        .image    = vulkan.depthImage,
        .viewType = VK_IMAGE_VIEW_TYPE_2D,
        .format   = VK_FORMAT_D32_SFLOAT,
        .subresourceRange = {
            .aspectMask     = VK_IMAGE_ASPECT_DEPTH_BIT,
            .baseMipLevel   = 0,
            .levelCount     = 1,
            .baseArrayLayer = 0,
            .layerCount     = 1
        }
    };

    if (vkCreateImageView(vulkan.device, &viewInfo, NULL, &vulkan.depthImageView) != VK_SUCCESS) { return 1; }

    return 0;
}

void VANILLA_VK_destroyDepthResources(void) {
    if (vulkan.depthImageView)   { vkDestroyImageView(vulkan.device, vulkan.depthImageView, NULL); }
    if (vulkan.depthImage)       { vkDestroyImage(vulkan.device, vulkan.depthImage, NULL); }
    if (vulkan.depthImageMemory) { vkFreeMemory(vulkan.device, vulkan.depthImageMemory, NULL); }
    
    vulkan.depthImageView = VK_NULL_HANDLE;
    vulkan.depthImage = VK_NULL_HANDLE;
    vulkan.depthImageMemory = VK_NULL_HANDLE;
}