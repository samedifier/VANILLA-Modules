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

int VANILLA_VK_createSurface(void) {
    if (glfwCreateWindowSurface(
            vulkan.instance,
            g_glfw.window,
            NULL,
            &vulkan.surface
        ) != VK_SUCCESS) { return 1; }

    return 0;
}

void VANILLA_VK_destroySurface(void) {
    if (vulkan.surface == VK_NULL_HANDLE) { return; }

    vkDestroySurfaceKHR(vulkan.instance, vulkan.surface, NULL);
    vulkan.surface = VK_NULL_HANDLE;
}