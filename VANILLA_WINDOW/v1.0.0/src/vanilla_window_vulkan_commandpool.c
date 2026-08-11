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

int VANILLA_VK_createCommandPool(void) {
    VkCommandPoolCreateInfo createInfo = {
        .sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO,
        .queueFamilyIndex = vulkan.graphicsQueueFamily,
        .flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT
    };

    VkResult result =
        vkCreateCommandPool(
            vulkan.device,
            &createInfo,
            NULL,
            &vulkan.commandPool
        );

    if(result != VK_SUCCESS) { return 1; }
    
    return 0;
}

void VANILLA_VK_destroyCommandPool(void) {
    vkDestroyCommandPool(
        vulkan.device,
        vulkan.commandPool,
        NULL
    );

    vulkan.commandPool = VK_NULL_HANDLE;
}