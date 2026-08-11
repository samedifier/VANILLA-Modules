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

#ifndef VANILLA_WINDOW_VULKAN_UTILS_H
#define VANILLA_WINDOW_VULKAN_UTILS_H

#include "vanilla_window_vulkan.h"
#include <stdio.h>
#include <string.h>

uint32_t VANILLA_FindMemoryType(VkPhysicalDevice physicalDevice, uint32_t typeFilter, VkMemoryPropertyFlags properties);
VkDeviceMemory VANILLA_gmmap(const VkMemoryRequirements* requirements, VkMemoryPropertyFlags properties);
void VANILLA_gmunmap(VkDeviceMemory memory);

int VANILLA_VK_createInstance(void);
void VANILLA_VK_destroyInstance(void);

int VANILLA_VK_createSurface(void);
void VANILLA_VK_destroySurface(void);

int VANILLA_VK_pickPhysicalDevice(void);
int VANILLA_VK_createLogicalDevice(void);
void VANILLA_VK_destroyLogicalDevice(void);

int VANILLA_VK_createSwapchain(VkSwapchainKHR oldSwapchain);
int VANILLA_VK_recreateSwapchain(void);
void VANILLA_VK_destroySwapchain(void);

int VANILLA_VK_createImageViews(void);
void VANILLA_VK_destroyImageViews(void);

int VANILLA_VK_createPipelineLayout(void);
void VANILLA_VK_destroyPipelineLayout(void);

VkPipelineVertexInputStateCreateInfo VANILLA_makePipelineVertexInput(VkVertexInputBindingDescription* outBindingDesc, VkVertexInputAttributeDescription* outAttrDescs);

int VANILLA_VK_createPipeline(void);
void VANILLA_VK_destroyPipeline(void);

int VANILLA_VK_createCommandPool(void);
void VANILLA_VK_destroyCommandPool(void);

int VANILLA_VK_createCommandBuffers(void);
int VANILLA_VK_recordCommandBuffer(uint32_t imageIndex, uint32_t activeEntityCount);
void VANILLA_VK_destroyCommandBuffers(void);

VkCommandBuffer VANILLA_VK_beginSingleTimeCommands(void);
int VANILLA_VK_endSingleTimeCommands(VkCommandBuffer commandBuffer);

int VANILLA_VK_createSyncObjects(void);
void VANILLA_VK_destroySyncObjects(void);

int VANILLA_VK_drawFrame(void);

int VANILLA_InitializePersistentBuffers(VkDevice device, VkPhysicalDevice physicalDevice);
void VANILLA_CleanupPersistentBuffers(VkDevice device);
void VANILLA_ExtractAndPushModel(uint32_t modelIndex, void* anycoreModelBuffer, uint32_t vertexCount, void* anycoreIndexBuffer, uint32_t indexCount, uint32_t maxModelIndex, VANILLA_VertexLayout* layout);
void VANILLA_redoModel(uint32_t modelIndex, void* anycoreModelBuffer, uint32_t vertexCount, void* anycoreIndexBuffer, uint32_t indexCount, VANILLA_VertexLayout* layout);

VkDescriptorSetLayout VANILLA_CreateDescriptorSetLayout(void);
int VANILLA_CreateDescriptor(void);
void VANILLA_CleanupDescriptorLayout(void);

int VANILLA_VK_initIndirectBuffer(void);
void VANILLA_VK_cleanupIndirectBuffer(void);

int VANILLA_VK_createDepthResources(void);
void VANILLA_VK_destroyDepthResources(void);

///

int createDefaultTexture();
int VANILLA_VK_textureinit(void);
void VANILLA_VK_texturefree(void);

int createDefaultSampler();
int VANILLA_initSamplers(void);
void VANILLA_destroySamplers(void);

//

int VANILLA_VK_createMaterialBuffer(void);
void VANILLA_VK_destroyMaterialBuffer(void);

static inline int VANILLA_VK_init() {
    if (VANILLA_VK_createInstance()) { return 1; }
    if (VANILLA_VK_createSurface()) { return 1; }
    if (VANILLA_VK_pickPhysicalDevice()) { return 1; }
    if (VANILLA_VK_createLogicalDevice()) { return 1; }
    if (VANILLA_VK_textureinit()) { return 1; }
    if (VANILLA_initSamplers()) { return 1; }
    if (VANILLA_VK_createMaterialBuffer()) { return 1; }
    if (VANILLA_VK_createSwapchain(VK_NULL_HANDLE)) { return 1; }
    if (VANILLA_VK_createImageViews()) { return 1; }
    if (VANILLA_InitializePersistentBuffers(vulkan.device, vulkan.physicalDevice)) { return 1; }

    uint32_t maxTotalEntities = stream->chunkLimit * CHUNKSIZE;
    size_t allocationSize = maxTotalEntities * sizeof(uint32_t);

    vulkan.vramModelList = (uint32_t*)VANILLA_mmap(allocationSize);

    if (vulkan.vramModelList == NULL) {
        printf("[VULKAN] vrammodellist\n");
        return 1;
    }

    memset(vulkan.vramModelList, 0, allocationSize);
    vulkan.vramActiveCount = 0;

    vulkan.descriptorSetLayout = VANILLA_CreateDescriptorSetLayout();

    if (VANILLA_VK_createPipelineLayout()) { return 1; }
    if (VANILLA_VK_createPipeline()) { return 1; }
    if (VANILLA_VK_createDepthResources()) { return 1; }
    if (VANILLA_VK_createCommandPool()) { return 1; }
    if (VANILLA_VK_initIndirectBuffer()) { return 1; }
    if (VANILLA_VK_createCommandBuffers()) { return 1; }
    if (VANILLA_VK_createSyncObjects()) { return 1; }

    printf("[VANILLA WINDOW] initalized without an error\n");

    return 0;
}
    
#endif // VANILLA_WINDOW_VULKAN_UTILS_H