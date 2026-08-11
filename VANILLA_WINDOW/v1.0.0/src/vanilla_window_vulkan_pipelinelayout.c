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

int VANILLA_VK_createPipelineLayout(void) {
    VkPipelineLayoutCreateInfo createInfo = {
        .sType                  = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO,
        .setLayoutCount         = 1, 
        .pSetLayouts            = &vulkan.descriptorSetLayout,
        .pushConstantRangeCount = 0,
        .pPushConstantRanges    = NULL
    };

    if (vkCreatePipelineLayout(vulkan.device, &createInfo, NULL, &vulkan.pipelineLayout) != VK_SUCCESS) { 
        return 1; 
    }
    return 0;
}

void VANILLA_VK_destroyPipelineLayout(void) {
    vkDestroyPipelineLayout(
        vulkan.device,
        vulkan.pipelineLayout,
        NULL
    );
    vulkan.pipelineLayout = VK_NULL_HANDLE;
}