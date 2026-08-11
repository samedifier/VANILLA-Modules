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

VANILLA_MemoryMode memoryMode = VANILLA_MEMORY_HYBRID;

uint32_t VANILLA_FindMemoryType(VkPhysicalDevice physicalDevice, uint32_t typeFilter, VkMemoryPropertyFlags properties) {
    VkPhysicalDeviceMemoryProperties memProperties;
    vkGetPhysicalDeviceMemoryProperties(physicalDevice, &memProperties);

    for (uint32_t i = 0; i < memProperties.memoryTypeCount; i++) {
        if ((typeFilter & (1 << i)) && (memProperties.memoryTypes[i].propertyFlags & properties) == properties) {
            return i;
        }
    }
    return 0xFFFFFFFF; 
}

VkDeviceMemory VANILLA_gmmap(const VkMemoryRequirements* requirements, VkMemoryPropertyFlags properties) {
    uint32_t memoryTypeIndex = VANILLA_FindMemoryType(vulkan.physicalDevice, requirements->memoryTypeBits, properties);
    if (memoryTypeIndex == UINT32_MAX) { return VK_NULL_HANDLE; }

    VkMemoryAllocateInfo allocateInfo = {
        .sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO,
        .allocationSize = requirements->size,
        .memoryTypeIndex = memoryTypeIndex
    };

    VkDeviceMemory memory = VK_NULL_HANDLE;
    if (vkAllocateMemory(vulkan.device, &allocateInfo, NULL, &memory) != VK_SUCCESS) { return VK_NULL_HANDLE; }

    return memory;
}

void VANILLA_gmunmap(VkDeviceMemory memory) {
    if (memory != VK_NULL_HANDLE) { vkFreeMemory( vulkan.device, memory, NULL); }
}