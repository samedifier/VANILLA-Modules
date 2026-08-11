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
#include "main.h"

int VANILLA_VK_createSyncObjects(void) {
    VkSemaphoreCreateInfo semaphoreInfo = {
        .sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO
    };

    VkFenceCreateInfo fenceInfo = {
        .sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO,
        .flags = VK_FENCE_CREATE_SIGNALED_BIT
    };

    VkResult result;

    result =
        vkCreateSemaphore(
            vulkan.device,
            &semaphoreInfo,
            NULL,
            &vulkan.imageAvailableSemaphore
        );

    if(result != VK_SUCCESS) { return 1; }

    result =
        vkCreateSemaphore(
            vulkan.device,
            &semaphoreInfo,
            NULL,
            &vulkan.renderFinishedSemaphore
        );

    if(result != VK_SUCCESS) {
        vkDestroySemaphore(
            vulkan.device,
            vulkan.imageAvailableSemaphore,
            NULL
        );

        return 1;
    }

    result =
        vkCreateFence(
            vulkan.device,
            &fenceInfo,
            NULL,
            &vulkan.inFlightFence
        );

    if(result != VK_SUCCESS) {
        vkDestroySemaphore(
            vulkan.device,
            vulkan.renderFinishedSemaphore,
            NULL
        );

        vkDestroySemaphore(
            vulkan.device,
            vulkan.imageAvailableSemaphore,
            NULL
        );

        return 1;
    }

    return 0;
}

void VANILLA_VK_destroySyncObjects(void) {
    vkDestroyFence(
        vulkan.device,
        vulkan.inFlightFence,
        NULL
    );

    vkDestroySemaphore(
        vulkan.device,
        vulkan.renderFinishedSemaphore,
        NULL
    );

    vkDestroySemaphore(
        vulkan.device,
        vulkan.imageAvailableSemaphore,
        NULL
    );

    vulkan.inFlightFence = VK_NULL_HANDLE;
    vulkan.renderFinishedSemaphore = VK_NULL_HANDLE;
    vulkan.imageAvailableSemaphore = VK_NULL_HANDLE;
}