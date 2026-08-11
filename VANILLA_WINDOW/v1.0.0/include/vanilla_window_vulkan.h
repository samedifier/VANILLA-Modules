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

#ifndef VANILLA_WINDOW_VULKAN_H
#define VANILLA_WINDOW_VULKAN_H

#include "configuration.h"
#include "primitives.h"
#include "main.h"

#include <X11/Xlib.h>
#include <vulkan/vulkan.h>
#include <vulkan/vulkan_xlib.h>
#include <vulkan/vulkan_wayland.h>

typedef struct {
    VkInstance instance;

    VkSurfaceKHR surface;

    VkPhysicalDevice physicalDevice;
    VkDevice         device;

    uint32_t graphicsQueueFamily;
    uint32_t presentQueueFamily;

    VkQueue graphicsQueue;
    VkQueue presentQueue;

    VkSwapchainKHR swapchain;

    VkFormat   swapchainFormat;
    VkExtent2D swapchainExtent;

    uint32_t imageCount;

    VkImage*     images;
    VkImageView* imageViews;

    VkImage        depthImage;
    VkDeviceMemory depthImageMemory;
    VkImageView    depthImageView;

    VkPipelineLayout pipelineLayout;
    VkPipeline pipeline;
    
    VkCommandPool commandPool;
    VkCommandBuffer* commandBuffers;

    VkSemaphore imageAvailableSemaphore;
    VkSemaphore renderFinishedSemaphore;

    VkFence inFlightFence;

    ///

    VkBuffer       globalTransformBuffer;
    VkDeviceMemory globalTransformMemory;
    void* mappedTransformBufferPtr;

    VkBuffer       modelLookupBuffer;
    VkDeviceMemory modelLookupMemory;
    void* mappedModelLookupPtr;

    VkBuffer       globalVertexBuffer;
    VkDeviceMemory globalVertexMemory;
    void* mappedVertexBufferPtr;

    VkBuffer       globalIndexBuffer;
    VkDeviceMemory globalIndexMemory;
    void* mappedIndexBufferPtr;

    //

    VkBuffer       uploadGlobalTransformBuffer;
    VkDeviceMemory uploadGlobalTransformMemory;
    void* mappedUploadTransformBufferPtr;
    VkDeviceSize uploadTransformBufferSize;

    VkBufferCopy transformBufferCopy;

    VkBuffer       uploadModelLookupBuffer;
    VkDeviceMemory uploadModelLookupMemory;
    void* mappedUploadModelLookupPtr;
    VkDeviceSize uploadModelLookupSize;

    VkBufferCopy modelLookupBufferCopy;

    VkBuffer       uploadGlobalVertexBuffer; //
    VkDeviceMemory uploadGlobalVertexMemory;
    void* mappedUploadVertexBufferPtr;
    VkDeviceSize uploadVertexSize;

    VkBufferCopy vertexBufferCopy;

    VkBuffer       uploadGlobalIndexBuffer; //
    VkDeviceMemory uploadGlobalIndexMemory;
    void* mappedUploadIndexBufferPtr;
    VkDeviceSize uploadIndexSize;

    VkBufferCopy indexBufferCopy;

    uint64_t maxVertexCount;
    uint64_t maxIndexCount;
    
    //

    uint32_t currentVertexOffset;
    uint32_t currentIndexOffset;

    VkDescriptorSetLayout descriptorSetLayout;
    VkDescriptorPool      descriptorPool;
    VkDescriptorSet       descriptorSet;

    uint32_t* vramModelList;
    uint32_t  vramActiveCount;

    VkBuffer indirectCommandBuffer;
    VkDeviceMemory indirectCommandBufferMemory;
    VkDrawIndirectCommand* mappedIndirectCommands;

    ///

    VANILLA_Texture* textures;

    uint32_t* textureFreeSlots;
    uint16_t* textureGenerations;

    uint32_t textureFreeSlotCount;

    VkBuffer       textureUploadBuffer;
    VkDeviceMemory textureUploadMemory;
    void* mappedTextureUploadPtr;

    //

    uint32_t maxSamplerAnisotropy;

    VkSampler* samplers;

    uint32_t* samplerFreeSlots;
    uint16_t* samplerGenerations;

    uint32_t samplerFreeSlotCount;

    //
    
    VANILLA_Texture defaultTexture;
    VkSampler defaultSampler;

    VkDescriptorImageInfo* textureInfos;
    VkDescriptorImageInfo* samplerInfos;
    
    //

    VkBuffer materialBuffer;
    VkDeviceMemory materialMemory;

    VkBuffer materialUploadBuffer;
    VkDeviceMemory materialUploadMemory;

    void* mappedMaterialBufferPtr;

    uint32_t* materialFreeSlots;
    uint16_t* materialGenerations;

    uint32_t materialFreeSlotCount;

    VkBufferCopy materialCopy;
    uint32_t currentMaterialOffset;

    uint32_t maxmcslot;

    VkBuffer materialSlotBuffer;
    VkDeviceMemory materialSlotMemory;

    void* mappedMaterialSlotBufferPtr;
} VANILLA_Vulkan;

extern VANILLA_Vulkan vulkan;

#endif // VANILLA_WINDOW_VULKAN_H