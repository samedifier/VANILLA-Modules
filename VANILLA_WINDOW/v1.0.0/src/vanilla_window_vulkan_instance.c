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

#include <GLFW/glfw3.h>

#include <string.h>
#include <stdbool.h>

VANILLA_Vulkan vulkan;

int VANILLA_VK_createInstance(void) {
    uint32_t glfwExtensionCount = 0;

    const char** glfwExtensions = glfwGetRequiredInstanceExtensions(&glfwExtensionCount);
    if (!glfwExtensions || glfwExtensionCount == 0) { return 1; }

    uint32_t extensionCount = 0;

    vkEnumerateInstanceExtensionProperties(
        NULL,
        &extensionCount,
        NULL
    );

    VkExtensionProperties* extensions = VANILLA_mmap(sizeof(VkExtensionProperties) * extensionCount);

    if (extensions == VANILLA_MAP_FAILED || !extensions) { return 1; }

    vkEnumerateInstanceExtensionProperties(
        NULL,
        &extensionCount,
        extensions
    );

    for (uint32_t i = 0; i < glfwExtensionCount; i++) {
        bool found = false;

        for (uint32_t j = 0; j < extensionCount; j++) {
            if (!strcmp(glfwExtensions[i], extensions[j].extensionName)) {
                found = true;
                break;
            }
        }

        if (!found) {
            VANILLA_munmap(extensions, sizeof(VkExtensionProperties) * extensionCount);
            return 1;
        }
    }

    VANILLA_munmap(extensions, sizeof(VkExtensionProperties) * extensionCount);

    VkApplicationInfo applicationInfo = {
        .sType = VK_STRUCTURE_TYPE_APPLICATION_INFO,
        .pApplicationName = applicationName,
        .applicationVersion = VK_MAKE_VERSION(majorVersion, minorVersion, patchVersion),
        .pEngineName = "ANYCORE256",
        .engineVersion = VK_MAKE_VERSION(1, 0, 0),
        .apiVersion = VK_API_VERSION_1_3
    };

    uint32_t requiredExtensionCount = glfwExtensionCount;

    const char** requiredExtensions = VANILLA_mmap(sizeof(char*) * requiredExtensionCount);

    if (requiredExtensions == VANILLA_MAP_FAILED || !requiredExtensions) {
        return 1;
    }

    for (uint32_t i = 0; i < glfwExtensionCount; i++) { requiredExtensions[i] = glfwExtensions[i]; }

    VkInstanceCreateInfo createInfo = {
        .sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO,
        .pApplicationInfo = &applicationInfo,
        .enabledExtensionCount = requiredExtensionCount,
        .ppEnabledExtensionNames = requiredExtensions
    };

    createInfo.enabledLayerCount = 0;
    createInfo.ppEnabledLayerNames = NULL;

    VkResult result = vkCreateInstance(&createInfo, NULL, &vulkan.instance);

    VANILLA_munmap(requiredExtensions, sizeof(char*) * requiredExtensionCount);

    if (result != VK_SUCCESS) { return 1; }

    return 0;
}


void VANILLA_VK_destroyInstance(void) {
    if (vulkan.instance) {
        vkDestroyInstance(vulkan.instance, NULL);

        vulkan.instance = VK_NULL_HANDLE;
    }
}