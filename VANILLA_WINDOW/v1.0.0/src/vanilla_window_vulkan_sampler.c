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

uint32_t samplerLimit = 32;

int VANILLA_initSamplers(void) {
    vulkan.samplers = VANILLA_mmap(samplerLimit * sizeof(VkSampler));
    if (vulkan.samplers == NULL) { return 1; }

    memset(vulkan.samplers, 0, samplerLimit * sizeof(VkSampler));

    vulkan.samplerGenerations = VANILLA_mmap(samplerLimit * sizeof(uint16_t));
    if (vulkan.samplerGenerations == NULL) {
        VANILLA_munmap(vulkan.samplers, samplerLimit * sizeof(VkSampler));
        vulkan.samplers = NULL;
        return 1;
    }

    memset(vulkan.samplerGenerations, 0, samplerLimit * sizeof(uint16_t));

    vulkan.samplerFreeSlots = VANILLA_mmap(samplerLimit * sizeof(uint32_t));
    if (vulkan.samplerFreeSlots == NULL) {
        VANILLA_munmap(vulkan.samplers, samplerLimit * sizeof(VkSampler));
        VANILLA_munmap(vulkan.samplerGenerations, samplerLimit * sizeof(uint16_t));

        vulkan.samplers = NULL;
        vulkan.samplerGenerations = NULL;

        return 1;
    }

    for (uint32_t i = 0; i < samplerLimit; i++) { vulkan.samplerFreeSlots[i] = i; }
    vulkan.samplerFreeSlotCount = samplerLimit;

    return 0;
}

void VANILLA_destroySamplers(void) {
    if (vulkan.samplers != NULL)
    {
        for (uint32_t i = 0; i < samplerLimit; i++)
        {
            if (vulkan.samplers[i] != VK_NULL_HANDLE)
            {
                vkDestroySampler(
                    vulkan.device,
                    vulkan.samplers[i],
                    NULL
                );
            }
        }

        VANILLA_munmap(vulkan.samplers, samplerLimit * sizeof(VkSampler));

        vulkan.samplers = NULL;
    }

    if (vulkan.samplerGenerations != NULL)
    {
        VANILLA_munmap(vulkan.samplerGenerations, samplerLimit * sizeof(uint16_t));

        vulkan.samplerGenerations = NULL;
    }

    if (vulkan.samplerFreeSlots != NULL)
    {
        VANILLA_munmap(vulkan.samplerFreeSlots, samplerLimit * sizeof(uint32_t));

        vulkan.samplerFreeSlots = NULL;
    }

    vulkan.samplerFreeSlotCount = 0;
}

static VkFilter VANILLA_VK_getFilter(VANILLA_Filter filter) {
    switch (filter) {
        case VANILLA_FILTER_NEAREST:
            return VK_FILTER_NEAREST;

        case VANILLA_FILTER_LINEAR:
            return VK_FILTER_LINEAR;

        default:
            return VK_FILTER_NEAREST;
    }
}

static VkSamplerMipmapMode VANILLA_VK_getMipmapFilter(VANILLA_MipmapFilter filter) {
    switch (filter) {
        case VANILLA_MIPMAPFILTER_NEAREST:
            return VK_SAMPLER_MIPMAP_MODE_NEAREST;

        case VANILLA_MIPMAPFILTER_LINEAR:
            return VK_SAMPLER_MIPMAP_MODE_LINEAR;

        default:
            return VK_SAMPLER_MIPMAP_MODE_NEAREST;
    }
}

static VkSamplerAddressMode VANILLA_VK_getAddressMode(VANILLA_AddressMode mode) {
    switch (mode) {
        case VANILLA_ADDRESS_REPEAT:
            return VK_SAMPLER_ADDRESS_MODE_REPEAT;

        case VANILLA_ADDRESS_CLAMP_TO_EDGE:
            return VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;

        case VANILLA_ADDRESS_CLAMP_TO_BORDER:
            return VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_BORDER;

        case VANILLA_ADDRESS_MIRRORED_REPEAT:
            return VK_SAMPLER_ADDRESS_MODE_MIRRORED_REPEAT;

        default:
            return VK_SAMPLER_ADDRESS_MODE_REPEAT;
    }
}

static VkBorderColor VANILLA_VK_getBorderColor(VANILLA_BorderColor color) {
    switch (color) {
        case VANILLA_BORDER_FLOAT_TRANSPARENT_BLACK:
            return VK_BORDER_COLOR_FLOAT_TRANSPARENT_BLACK;

        case VANILLA_BORDER_INT_TRANSPARENT_BLACK:
            return VK_BORDER_COLOR_INT_TRANSPARENT_BLACK;

        case VANILLA_BORDER_FLOAT_OPAQUE_BLACK:
            return VK_BORDER_COLOR_FLOAT_OPAQUE_BLACK;

        case VANILLA_BORDER_INT_OPAQUE_BLACK:
            return VK_BORDER_COLOR_INT_OPAQUE_BLACK;

        case VANILLA_BORDER_FLOAT_OPAQUE_WHITE:
            return VK_BORDER_COLOR_FLOAT_OPAQUE_WHITE;

        case VANILLA_BORDER_INT_OPAQUE_WHITE:
            return VK_BORDER_COLOR_INT_OPAQUE_WHITE;

        default:
            return VK_BORDER_COLOR_FLOAT_OPAQUE_BLACK;
    }
}

static int VANILLA_VK_createSampler(VkSampler* sampler, const VANILLA_SamplerCreateInfo* createInfo) {
    VkSamplerCreateInfo vkCreateInfo = {
        .sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO,

        .magFilter =
            VANILLA_VK_getFilter(createInfo->magFilter),

        .minFilter =
            VANILLA_VK_getFilter(createInfo->minFilter),

        .mipmapMode =
            VANILLA_VK_getMipmapFilter(createInfo->mipmapFilter),

        .addressModeU =
            VANILLA_VK_getAddressMode(createInfo->addressU),

        .addressModeV =
            VANILLA_VK_getAddressMode(createInfo->addressV),

        .addressModeW =
            VANILLA_VK_getAddressMode(createInfo->addressW),

        .mipLodBias = 0.0f,

        .anisotropyEnable =
            (createInfo->maxAnisotropy > 0) ? VK_TRUE : VK_FALSE,

        .maxAnisotropy =
            (createInfo->maxAnisotropy > 0)
                ? VANILLA_minf(
                    (float)createInfo->maxAnisotropy,
                    vulkan.maxSamplerAnisotropy)
                : 1.0f,

        .compareEnable = VK_FALSE,
        .compareOp = VK_COMPARE_OP_ALWAYS,

        .minLod = 0.0f,
        .maxLod = VK_LOD_CLAMP_NONE,

        .borderColor =
            VANILLA_VK_getBorderColor(createInfo->borderColor),

        .unnormalizedCoordinates = VK_FALSE
    };

    VkResult result =
        vkCreateSampler(
            vulkan.device,
            &vkCreateInfo,
            NULL,
            sampler
        );

    if (result != VK_SUCCESS)
    {
        return 1;
    }

    return 0;
}

int createDefaultSampler() {
    const VANILLA_SamplerCreateInfo createInfo = {
        .magFilter = VANILLA_FILTER_NEAREST,
        .minFilter = VANILLA_FILTER_NEAREST,
        .addressU = VANILLA_ADDRESS_REPEAT,
        .addressV = VANILLA_ADDRESS_REPEAT,
        .addressW = VANILLA_ADDRESS_REPEAT,
        .maxAnisotropy = 0,
        .borderColor = VANILLA_BORDER_FLOAT_OPAQUE_BLACK
    };

    VANILLA_RESULT result = VANILLA_VK_createSampler(&vulkan.defaultSampler, &createInfo);
    if (result != VANILLA_SUCCESS) { return 1; }

    return 0;
}

VANILLA_EXPORT VANILLA_RESULT VANILLA_createSampler(const VANILLA_SamplerCreateInfo* createInfo, SamplerID* outSamplerID) {
    if (vulkan.samplerFreeSlotCount == 0) { return VANILLA_ERR_CAPACITY_REACHED; }
    uint32_t slot = vulkan.samplerFreeSlots[--vulkan.samplerFreeSlotCount];

    if (VANILLA_VK_createSampler(&vulkan.samplers[slot], createInfo)) {
        vulkan.samplerFreeSlots[vulkan.samplerFreeSlotCount++] = slot;

        return VANILLA_ERR_CREATE_SAMPLER;
    }

    *outSamplerID = (SamplerID){ slot, ++vulkan.samplerGenerations[slot] };
    return VANILLA_SUCCESS;
}

VANILLA_EXPORT VANILLA_RESULT VANILLA_destroySampler(SamplerID samplerID) {
    if (samplerID.slot >= samplerLimit) { return VANILLA_ERR_INVALID_SAMPLER; }
    if (vulkan.samplerGenerations[samplerID.slot] != samplerID.generation) { return VANILLA_ERR_INVALID_SAMPLER; }

    vkDestroySampler(
        vulkan.device,
        vulkan.samplers[samplerID.slot],
        NULL
    );

    vulkan.samplers[samplerID.slot] = VK_NULL_HANDLE;
    vulkan.samplerFreeSlots[vulkan.samplerFreeSlotCount++] = samplerID.slot;

    return VANILLA_SUCCESS;
}