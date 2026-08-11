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
#include <string.h>

static bool findQueueFamilies(VkPhysicalDevice device, uint32_t* graphicsFamily, uint32_t* presentFamily) {
    uint32_t queueFamilyCount = 0;

    vkGetPhysicalDeviceQueueFamilyProperties(
        device,
        &queueFamilyCount,
        NULL
    );

    VkQueueFamilyProperties* queueFamilies = VANILLA_mmap(sizeof(VkQueueFamilyProperties) * queueFamilyCount);

    if (queueFamilies == VANILLA_MAP_FAILED || !queueFamilies) {
        return false;
    }

    vkGetPhysicalDeviceQueueFamilyProperties(
        device,
        &queueFamilyCount,
        queueFamilies
    );

    bool graphicsFound = false;
    bool presentFound  = false;

    for (uint32_t i = 0; i < queueFamilyCount; i++) {
        if (queueFamilies[i].queueFlags & VK_QUEUE_GRAPHICS_BIT) {
            *graphicsFamily = i;
            graphicsFound = true;
        }

        VkBool32 presentSupport = VK_FALSE;

        vkGetPhysicalDeviceSurfaceSupportKHR(
            device,
            i,
            vulkan.surface,
            &presentSupport
        );

        if (presentSupport) {
            *presentFamily = i;
            presentFound = true;
        }

        if (graphicsFound && presentFound) {
            break;
        }
    }

    VANILLA_munmap(queueFamilies, sizeof(VkQueueFamilyProperties) * queueFamilyCount);

    return graphicsFound && presentFound;
}

static bool hasSwapchainSupport(VkPhysicalDevice device) {
    uint32_t extensionCount = 0;

    vkEnumerateDeviceExtensionProperties(
        device,
        NULL,
        &extensionCount,
        NULL
    );

    VkExtensionProperties* extensions = VANILLA_mmap(sizeof(VkExtensionProperties) * extensionCount);
    if (extensions == VANILLA_MAP_FAILED || !extensions) { return false; }

    vkEnumerateDeviceExtensionProperties(
        device,
        NULL,
        &extensionCount,
        extensions
    );

    bool found = false;

    for (uint32_t i = 0; i < extensionCount; i++) {
        if (strcmp(extensions[i].extensionName, VK_KHR_SWAPCHAIN_EXTENSION_NAME)) {
            found = true;
            break;
        }
    }

    VANILLA_munmap(extensions, sizeof(VkExtensionProperties) * extensionCount);

    return found;
}

int VANILLA_VK_pickPhysicalDevice(void) {
    uint32_t deviceCount = 0;

    vkEnumeratePhysicalDevices(
        vulkan.instance,
        &deviceCount,
        NULL
    );

    if (!deviceCount) { return 1; }

    VkPhysicalDevice* devices = VANILLA_mmap(sizeof(VkPhysicalDevice) * deviceCount);

    if (devices == VANILLA_MAP_FAILED || !devices) {
        return 1;
    }

    vkEnumeratePhysicalDevices(
        vulkan.instance,
        &deviceCount,
        devices
    );

    VkPhysicalDevice selectedDevice = VK_NULL_HANDLE;
    VkPhysicalDevice fallbackDevice = VK_NULL_HANDLE;

    uint32_t selectedGraphicsFamily = 0;
    uint32_t selectedPresentFamily  = 0;

    uint32_t fallbackGraphicsFamily = 0;
    uint32_t fallbackPresentFamily  = 0;

    for (uint32_t i = 0; i < deviceCount; i++) {
        uint32_t graphicsFamily = 0;
        uint32_t presentFamily  = 0;

        if (!findQueueFamilies(devices[i], &graphicsFamily, &presentFamily)) { continue; }
        if (!hasSwapchainSupport(devices[i])) { continue; }

        VkPhysicalDeviceProperties properties;

        vkGetPhysicalDeviceProperties(devices[i], &properties);
        if (properties.apiVersion < VK_API_VERSION_1_3) { continue; }

        VkPhysicalDeviceDescriptorIndexingFeatures descriptorIndexing = {
            .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_DESCRIPTOR_INDEXING_FEATURES
        };

        VkPhysicalDeviceFeatures2 features = {
            .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2,
            .pNext = &descriptorIndexing
        };

        vkGetPhysicalDeviceFeatures2(devices[i], &features);

        if (!descriptorIndexing.runtimeDescriptorArray ||
            !descriptorIndexing.shaderSampledImageArrayNonUniformIndexing ||
            !descriptorIndexing.descriptorBindingPartiallyBound) { continue; }

        if (fallbackDevice == VK_NULL_HANDLE) {
            fallbackDevice = devices[i];
            fallbackGraphicsFamily = graphicsFamily;
            fallbackPresentFamily = presentFamily;
        }

        if (properties.deviceType == VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU) {
            selectedDevice = devices[i];
            selectedGraphicsFamily = graphicsFamily;
            selectedPresentFamily = presentFamily;
            break;
        }
    }

    VANILLA_munmap(devices, sizeof(VkPhysicalDevice) * deviceCount);

    if (selectedDevice != VK_NULL_HANDLE) {
        vulkan.physicalDevice = selectedDevice;
        vulkan.graphicsQueueFamily = selectedGraphicsFamily;
        vulkan.presentQueueFamily = selectedPresentFamily;
    }
    else if (fallbackDevice != VK_NULL_HANDLE) {
        vulkan.physicalDevice = fallbackDevice;
        vulkan.graphicsQueueFamily = fallbackGraphicsFamily;
        vulkan.presentQueueFamily = fallbackPresentFamily;
    }
    else {
        return 1;
    }

    VkPhysicalDeviceProperties properties;

    vkGetPhysicalDeviceProperties(
        vulkan.physicalDevice,
        &properties
    );

    vulkan.maxSamplerAnisotropy = (uint32_t)properties.limits.maxSamplerAnisotropy;


    return 0;
}

int VANILLA_VK_createLogicalDevice(void) {
    float priority = 1.0f;

    VkDeviceQueueCreateInfo queueInfos[2];
    uint32_t queueCount = 0;

    queueInfos[queueCount++] =
        (VkDeviceQueueCreateInfo) {
            .sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO,
            .queueFamilyIndex = vulkan.graphicsQueueFamily,
            .queueCount = 1,
            .pQueuePriorities = &priority
        };

    if (vulkan.graphicsQueueFamily != vulkan.presentQueueFamily) {
        queueInfos[queueCount++] =
            (VkDeviceQueueCreateInfo) {
                .sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO,
                .queueFamilyIndex = vulkan.presentQueueFamily,
                .queueCount = 1,
                .pQueuePriorities = &priority
            };
    }

    const char* extensions[] = {
        VK_KHR_SWAPCHAIN_EXTENSION_NAME
    };

    VkPhysicalDeviceDynamicRenderingFeatures dynamicRendering = {
        .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_DYNAMIC_RENDERING_FEATURES,
        .dynamicRendering = VK_TRUE
    };

    VkPhysicalDeviceDescriptorIndexingFeatures descriptorIndexing = {
        .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_DESCRIPTOR_INDEXING_FEATURES,
        .runtimeDescriptorArray = VK_TRUE,
        .shaderSampledImageArrayNonUniformIndexing = VK_TRUE,
        .descriptorBindingPartiallyBound = VK_TRUE,
        .descriptorBindingSampledImageUpdateAfterBind = VK_TRUE,
        .runtimeDescriptorArray = VK_TRUE
    };

    dynamicRendering.pNext = &descriptorIndexing;

    VkDeviceCreateInfo createInfo = {
        .sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO,
        .queueCreateInfoCount = queueCount,
        .pQueueCreateInfos = queueInfos,
        .enabledExtensionCount = 1,
        .ppEnabledExtensionNames = extensions,
        .pNext = &dynamicRendering
    };

    if (vkCreateDevice(
            vulkan.physicalDevice,
            &createInfo,
            NULL,
            &vulkan.device
        ) != VK_SUCCESS ) {
        return 1;
    }

    vkGetDeviceQueue(
        vulkan.device,
        vulkan.graphicsQueueFamily,
        0,
        &vulkan.graphicsQueue
    );

    vkGetDeviceQueue(
        vulkan.device,
        vulkan.presentQueueFamily,
        0,
        &vulkan.presentQueue
    );

    return 0;
}

void VANILLA_VK_destroyLogicalDevice(void) {
    if (vulkan.device == VK_NULL_HANDLE) { return; }

    vkDestroyDevice(vulkan.device, NULL);
    vulkan.device = VK_NULL_HANDLE;
}