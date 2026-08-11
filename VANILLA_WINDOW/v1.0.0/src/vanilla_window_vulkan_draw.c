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

int VANILLA_VK_drawFrame(void) {
    VkResult result;

    result =
        vkWaitForFences(
            vulkan.device,
            1,
            &vulkan.inFlightFence,
            VK_TRUE,
            UINT64_MAX
        );

    if(result != VK_SUCCESS) { return 1; }

    uint32_t imageIndex;

    result =
        vkAcquireNextImageKHR(
            vulkan.device,
            vulkan.swapchain,
            UINT64_MAX,
            vulkan.imageAvailableSemaphore,
            VK_NULL_HANDLE,
            &imageIndex
        );

    if(result != VK_SUCCESS) { return 1; }

    result = vkResetFences(vulkan.device, 1, &vulkan.inFlightFence);
    if(result != VK_SUCCESS) { return 1; }

    if(VANILLA_VK_recordCommandBuffer(imageIndex, vulkan.vramActiveCount)) { return 1; }

    VkPipelineStageFlags waitStages[] = {
        VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT
    };

    VkSubmitInfo submitInfo = {
        .sType = VK_STRUCTURE_TYPE_SUBMIT_INFO,
        .waitSemaphoreCount = 1,
        .pWaitSemaphores = &vulkan.imageAvailableSemaphore,
        .pWaitDstStageMask = waitStages,
        .commandBufferCount = 1,
        .pCommandBuffers = &vulkan.commandBuffers[imageIndex],
        .signalSemaphoreCount = 1,
        .pSignalSemaphores = &vulkan.renderFinishedSemaphore
    };

    result =
        vkQueueSubmit(
            vulkan.graphicsQueue,
            1,
            &submitInfo,
            vulkan.inFlightFence
        );

    if(result != VK_SUCCESS) { return 1; }

    VkPresentInfoKHR presentInfo = {
        .sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR,
        .waitSemaphoreCount = 1,
        .pWaitSemaphores = &vulkan.renderFinishedSemaphore,
        .swapchainCount = 1,
        .pSwapchains = &vulkan.swapchain,
        .pImageIndices = &imageIndex
    };

    result = vkQueuePresentKHR(vulkan.presentQueue, &presentInfo);

    if(result != VK_SUCCESS) { return 1; }

    return VANILLA_SUCCESS;
}