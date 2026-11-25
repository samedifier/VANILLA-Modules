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

uint32_t textureLimit     = 1024;
uint64_t textureSizeLimit = (64 * 1024 * 1024);

static bool didsettexturelimit = false;
static bool didsettexturesizelimit = false;

static int VANILLA_VK_createTextureUploadBuffer(void) {
    VkBufferCreateInfo bufferCreateInfo = {
        .sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO,
        .size = textureSizeLimit,
        .usage = VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
        .sharingMode = VK_SHARING_MODE_EXCLUSIVE
    };

    if (vkCreateBuffer(vulkan.device, &bufferCreateInfo, NULL, &vulkan.textureUploadBuffer) != VK_SUCCESS) { return 1; }

    VkMemoryRequirements memoryRequirements;
    vkGetBufferMemoryRequirements(vulkan.device, vulkan.textureUploadBuffer, &memoryRequirements);

    vulkan.textureUploadMemory = VANILLA_gmmap(&memoryRequirements, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
    if (vulkan.textureUploadMemory == VK_NULL_HANDLE) {
        vkDestroyBuffer(vulkan.device, vulkan.textureUploadBuffer, NULL);
        vulkan.textureUploadBuffer = VK_NULL_HANDLE;
        return 1;
    }

    if (vkBindBufferMemory(
        vulkan.device,
        vulkan.textureUploadBuffer,
        vulkan.textureUploadMemory,
        0) != VK_SUCCESS) {

        VANILLA_gmunmap(vulkan.textureUploadMemory);
        vkDestroyBuffer(vulkan.device, vulkan.textureUploadBuffer, NULL);

        vulkan.textureUploadMemory = VK_NULL_HANDLE;
        vulkan.textureUploadBuffer = VK_NULL_HANDLE;
        return 1;
    }

    if (vkMapMemory(
        vulkan.device,
        vulkan.textureUploadMemory,
        0,
        textureSizeLimit,
        0,
        &vulkan.mappedTextureUploadPtr) != VK_SUCCESS) {

        VANILLA_gmunmap(vulkan.textureUploadMemory);
        vkDestroyBuffer(vulkan.device, vulkan.textureUploadBuffer, NULL);

        vulkan.textureUploadMemory = VK_NULL_HANDLE;
        vulkan.textureUploadBuffer = VK_NULL_HANDLE;
        return 1;
    }

    return 0;
}

static void VANILLA_VK_destroyTextureUploadBuffer(void) {
    if (vulkan.mappedTextureUploadPtr != NULL) {
        vkUnmapMemory(vulkan.device, vulkan.textureUploadMemory);
        vulkan.mappedTextureUploadPtr = NULL;
    }
    if (vulkan.textureUploadMemory != VK_NULL_HANDLE) {
        VANILLA_gmunmap(vulkan.textureUploadMemory);
        vulkan.textureUploadMemory = VK_NULL_HANDLE;
    }
    if (vulkan.textureUploadBuffer != VK_NULL_HANDLE) {
        vkDestroyBuffer(vulkan.device, vulkan.textureUploadBuffer, NULL);
        vulkan.textureUploadBuffer = VK_NULL_HANDLE;
    }
}

int VANILLA_VK_textureinit(void) {
    vulkan.textures = VANILLA_mmap(sizeof(VANILLA_Texture) * textureLimit);
    if (!vulkan.textures) { return 1; }

    vulkan.textureFreeSlots = VANILLA_mmap(sizeof(uint32_t) * textureLimit);
    if (!vulkan.textureFreeSlots) {
        VANILLA_munmap(vulkan.textures, sizeof(VANILLA_Texture) * textureLimit);
        return 1;
    }

    vulkan.textureGenerations = VANILLA_mmap(sizeof(uint16_t) * textureLimit);
    if (!vulkan.textureGenerations) {
        VANILLA_munmap(vulkan.textureFreeSlots, sizeof(uint32_t) * textureLimit);
        VANILLA_munmap(vulkan.textures, sizeof(VANILLA_Texture) * textureLimit);
        return 1;
    }

    memset(vulkan.textures, 0, sizeof(VANILLA_Texture) * textureLimit);
    memset(vulkan.textureGenerations, 0, sizeof(uint16_t) * textureLimit);

    for (uint32_t i = 0; i < textureLimit; i++) { vulkan.textureFreeSlots[i] = i; }

    if (VANILLA_VK_createTextureUploadBuffer()) {
        VANILLA_munmap(vulkan.textureGenerations, sizeof(uint16_t) * textureLimit);
        VANILLA_munmap(vulkan.textureFreeSlots, sizeof(uint32_t) * textureLimit);
        VANILLA_munmap(vulkan.textures, sizeof(VANILLA_Texture) * textureLimit);
        return 1;
    }

    vulkan.textureFreeSlotCount = textureLimit;
    return 0;
}

void VANILLA_VK_texturefree(void) {
    for (uint32_t i = 0; i < textureLimit; i++) {
        if (vulkan.textures[i].imageView) {
            vkDestroyImageView(vulkan.device, vulkan.textures[i].imageView, NULL);
        }
        if (vulkan.textures[i].image) {
            vkDestroyImage(vulkan.device, vulkan.textures[i].image, NULL);
        }
        if (vulkan.textures[i].memory) {
            vkFreeMemory(vulkan.device, vulkan.textures[i].memory, NULL);
        }
    }

    VANILLA_munmap(vulkan.textureGenerations, sizeof(uint16_t)        * textureLimit);
    VANILLA_munmap(vulkan.textureFreeSlots,   sizeof(uint32_t)        * textureLimit);
    VANILLA_munmap(vulkan.textures,           sizeof(VANILLA_Texture) * textureLimit);

    VANILLA_VK_destroyTextureUploadBuffer();
}

static VkFormat VANILLA_VK_getFormat(VANILLA_TextureFormat format) {
    switch (format) {
        case VANILLA_TEXTUREFORMAT_R8_UNORM:
            return VK_FORMAT_R8_UNORM;
        case VANILLA_TEXTUREFORMAT_RG8_UNORM:
            return VK_FORMAT_R8G8_UNORM;
        case VANILLA_TEXTUREFORMAT_RGB8_UNORM:
            return VK_FORMAT_R8G8B8A8_UNORM;   /* expanded to RGBA8 on upload */
        case VANILLA_TEXTUREFORMAT_RGBA8_UNORM:
            return VK_FORMAT_R8G8B8A8_UNORM;
        case VANILLA_TEXTUREFORMAT_R8_SRGB:
            return VK_FORMAT_R8_SRGB;
        case VANILLA_TEXTUREFORMAT_RG8_SRGB:
            return VK_FORMAT_R8G8_SRGB;
        case VANILLA_TEXTUREFORMAT_RGB8_SRGB:
            return VK_FORMAT_R8G8B8A8_SRGB;    /* expanded to RGBA8 on upload */
        case VANILLA_TEXTUREFORMAT_RGBA8_SRGB:
            return VK_FORMAT_R8G8B8A8_SRGB;
        case VANILLA_TEXTUREFORMAT_R16_FLOAT:
            return VK_FORMAT_R16_SFLOAT;
        case VANILLA_TEXTUREFORMAT_RG16_FLOAT:
            return VK_FORMAT_R16G16_SFLOAT;
        case VANILLA_TEXTUREFORMAT_RGBA16_FLOAT:
            return VK_FORMAT_R16G16B16A16_SFLOAT;
        case VANILLA_TEXTUREFORMAT_R32_FLOAT:
            return VK_FORMAT_R32_SFLOAT;
        case VANILLA_TEXTUREFORMAT_RG32_FLOAT:
            return VK_FORMAT_R32G32_SFLOAT;
        case VANILLA_TEXTUREFORMAT_RGBA32_FLOAT:
            return VK_FORMAT_R32G32B32A32_SFLOAT;
        case VANILLA_TEXTUREFORMAT_DEPTH32_FLOAT:
            return VK_FORMAT_D32_SFLOAT;
        case VANILLA_TEXTUREFORMAT_DEPTH24_STENCIL8:
            return VK_FORMAT_D24_UNORM_S8_UINT;
        default:
            return VK_FORMAT_UNDEFINED;
    }
}

/* Bytes per texel of the caller's data (RGB8 is 3 here, 4 on the GPU). */
static uint32_t VANILLA_VK_texelSize(VANILLA_TextureFormat format) {
    switch (format) {
        case VANILLA_TEXTUREFORMAT_R8_UNORM:
        case VANILLA_TEXTUREFORMAT_R8_SRGB:          return 1;
        case VANILLA_TEXTUREFORMAT_RG8_UNORM:
        case VANILLA_TEXTUREFORMAT_RG8_SRGB:
        case VANILLA_TEXTUREFORMAT_R16_FLOAT:        return 2;
        case VANILLA_TEXTUREFORMAT_RGB8_UNORM:
        case VANILLA_TEXTUREFORMAT_RGB8_SRGB:        return 3;
        case VANILLA_TEXTUREFORMAT_RGBA8_UNORM:
        case VANILLA_TEXTUREFORMAT_RGBA8_SRGB:
        case VANILLA_TEXTUREFORMAT_RG16_FLOAT:
        case VANILLA_TEXTUREFORMAT_R32_FLOAT:
        case VANILLA_TEXTUREFORMAT_DEPTH32_FLOAT:
        case VANILLA_TEXTUREFORMAT_DEPTH24_STENCIL8: return 4;
        case VANILLA_TEXTUREFORMAT_RGBA16_FLOAT:
        case VANILLA_TEXTUREFORMAT_RG32_FLOAT:       return 8;
        case VANILLA_TEXTUREFORMAT_RGBA32_FLOAT:     return 16;
        default:                                     return 0;
    }
}

static bool VANILLA_VK_isRGB8(VANILLA_TextureFormat format) {
    return format == VANILLA_TEXTUREFORMAT_RGB8_UNORM || format == VANILLA_TEXTUREFORMAT_RGB8_SRGB;
}

static bool VANILLA_VK_isDepthFormat(VANILLA_TextureFormat format) {
    return format == VANILLA_TEXTUREFORMAT_DEPTH32_FLOAT || format == VANILLA_TEXTUREFORMAT_DEPTH24_STENCIL8;
}

static uint32_t VANILLA_VK_mipExtent(uint32_t size, uint32_t level) {
    uint32_t e = size >> level;
    return e ? e : 1;
}

/* How a texture is created and uploaded, decided before any Vulkan object exists. */
typedef struct {
    uint32_t levels;        /* image mip levels */
    uint32_t copiedLevels;  /* levels copied from the caller (PROVIDED: all, else 1) */
    bool     generate;      /* blit the remaining levels on the GPU */
    bool     linearBlit;    /* format supports linear filtering for the blits */
    uint32_t srcTexel;      /* bytes per texel in the caller's data */
    uint32_t gpuTexel;      /* bytes per texel in the upload buffer */
    uint64_t srcBytes;      /* bytes read from createInfo->data */
    uint64_t uploadBytes;   /* bytes written into the upload buffer */
} VANILLA_VK_TexturePlan;

static VANILLA_WINDOW_RESULT VANILLA_VK_planTexture(const VANILLA_TextureCreateInfo* createInfo, VANILLA_VK_TexturePlan* plan) {
    if (createInfo->width == 0 || createInfo->height == 0) { return VANILLA_WINDOW_ERR_INVALID_ARGUMENT; }

    uint32_t texel = VANILLA_VK_texelSize(createInfo->format);
    if (texel == 0) { return VANILLA_WINDOW_ERR_INVALID_ARGUMENT; }

    uint32_t largest = createInfo->width > createInfo->height ? createInfo->width : createInfo->height;
    uint32_t full = 1;
    while (largest >>= 1) { full++; }

    *plan = (VANILLA_VK_TexturePlan){ .levels = 1, .copiedLevels = 1, .srcTexel = texel, .gpuTexel = texel };
    if (VANILLA_VK_isRGB8(createInfo->format)) { plan->gpuTexel = 4; }

    bool depth = VANILLA_VK_isDepthFormat(createInfo->format);

    if (createInfo->mipmapMode == VANILLA_MIPMAPMODE_GENERATE && !depth) {
        /* mipLevels 0 = full chain */
        uint32_t levels = (createInfo->mipLevels == 0 || createInfo->mipLevels > full) ? full : createInfo->mipLevels;

        VkFormatProperties properties;
        vkGetPhysicalDeviceFormatProperties(vulkan.physicalDevice, VANILLA_VK_getFormat(createInfo->format), &properties);
        VkFormatFeatureFlags features = properties.optimalTilingFeatures;

        /* No blit support for this format: keep only the base level (sampling stays valid). */
        if (levels > 1 && (features & VK_FORMAT_FEATURE_BLIT_SRC_BIT) && (features & VK_FORMAT_FEATURE_BLIT_DST_BIT)) {
            plan->levels     = levels;
            plan->generate   = true;
            plan->linearBlit = (features & VK_FORMAT_FEATURE_SAMPLED_IMAGE_FILTER_LINEAR_BIT) != 0;
        }
    }
    else if (createInfo->mipmapMode == VANILLA_MIPMAPMODE_PROVIDED && !depth) {
        /* data holds every level, largest first, tightly packed */
        uint32_t levels = (createInfo->mipLevels == 0) ? 1 : createInfo->mipLevels;
        if (levels > full) { return VANILLA_WINDOW_ERR_INVALID_ARGUMENT; }

        plan->levels       = levels;
        plan->copiedLevels = levels;
    }

    for (uint32_t i = 0; i < plan->copiedLevels; i++) {
        uint64_t texels = (uint64_t)VANILLA_VK_mipExtent(createInfo->width, i) * VANILLA_VK_mipExtent(createInfo->height, i);
        plan->srcBytes    += texels * plan->srcTexel;
        plan->uploadBytes += texels * plan->gpuTexel;
    }

    if (plan->uploadBytes > textureSizeLimit) { return VANILLA_WINDOW_ERR_TEXTURE_SIZE_LIMIT; }
    if (createInfo->dataSize < plan->srcBytes) { return VANILLA_WINDOW_ERR_INVALID_ARGUMENT; }

    return VANILLA_WINDOW_SUCCESS;
}

static VANILLA_WINDOW_RESULT VANILLA_VK_createImage(VANILLA_Texture* texture, const VANILLA_TextureCreateInfo* createInfo, const VANILLA_VK_TexturePlan* plan) {
    VkImageCreateInfo imageInfo = {
        .sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO,
        .imageType = VK_IMAGE_TYPE_2D,
        .extent = {
            .width  = createInfo->width,
            .height = createInfo->height,
            .depth  = 1
        },
        .mipLevels = plan->levels,
        .arrayLayers = 1,
        .format = VANILLA_VK_getFormat(createInfo->format),
        .tiling = VK_IMAGE_TILING_OPTIMAL,
        .initialLayout = VK_IMAGE_LAYOUT_UNDEFINED,
        .usage = VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT | (plan->generate ? VK_IMAGE_USAGE_TRANSFER_SRC_BIT : 0),
        .sharingMode = VK_SHARING_MODE_EXCLUSIVE,
        .samples = VK_SAMPLE_COUNT_1_BIT,
        .flags = 0
    };

    if (vkCreateImage(
        vulkan.device,
        &imageInfo,
        NULL,
        &texture->image) != VK_SUCCESS) {
        return 1;
    }

    VkMemoryRequirements requirements;
    vkGetImageMemoryRequirements(
        vulkan.device,
        texture->image,
        &requirements
    );

    VkMemoryPropertyFlags properties = VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT;
    texture->memory = VANILLA_gmmap(&requirements, properties);

    if (texture->memory == VK_NULL_HANDLE) {
        vkDestroyImage(vulkan.device, texture->image, NULL);
        texture->image = VK_NULL_HANDLE;
        return 1;
    }

    if (vkBindImageMemory(
        vulkan.device,
        texture->image,
        texture->memory,
        0) != VK_SUCCESS) {

        VANILLA_gmunmap(texture->memory);
        vkDestroyImage(vulkan.device, texture->image, NULL);

        texture->memory = VK_NULL_HANDLE;
        texture->image = VK_NULL_HANDLE;

        return 1;
    }

    texture->width = createInfo->width;
    texture->height = createInfo->height;
    texture->format = createInfo->format;
    texture->mipLevels = plan->levels;

    return VANILLA_WINDOW_SUCCESS;
}

static VANILLA_WINDOW_RESULT VANILLA_VK_createImageView(VANILLA_Texture* texture) {
    VkImageAspectFlags aspectMask;

    switch (texture->format) {
        case VANILLA_TEXTUREFORMAT_DEPTH32_FLOAT:
            aspectMask = VK_IMAGE_ASPECT_DEPTH_BIT;
            break;
        case VANILLA_TEXTUREFORMAT_DEPTH24_STENCIL8:
            aspectMask = VK_IMAGE_ASPECT_DEPTH_BIT | VK_IMAGE_ASPECT_STENCIL_BIT;
            break;
        default:
            aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
            break;
    }

    VkImageViewCreateInfo imageViewInfo = {
        .sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO,
        .image = texture->image,
        .viewType = VK_IMAGE_VIEW_TYPE_2D,
        .format = VANILLA_VK_getFormat(texture->format),

        .components = {
            .r = VK_COMPONENT_SWIZZLE_IDENTITY,
            .g = VK_COMPONENT_SWIZZLE_IDENTITY,
            .b = VK_COMPONENT_SWIZZLE_IDENTITY,
            .a = VK_COMPONENT_SWIZZLE_IDENTITY
        },

        .subresourceRange = {
            .aspectMask = aspectMask,

            .baseMipLevel = 0,
            .levelCount = texture->mipLevels,

            .baseArrayLayer = 0,
            .layerCount = 1
        }
    };

    if (vkCreateImageView(
        vulkan.device,
        &imageViewInfo,
        NULL,
        &texture->imageView) != VK_SUCCESS) { return 1; }

    return VANILLA_WINDOW_SUCCESS;
}

static void VANILLA_VK_textureBarrier(VkCommandBuffer cmd, VkImage image, uint32_t baseLevel, uint32_t levelCount,
                                      VkImageLayout oldLayout, VkImageLayout newLayout,
                                      VkAccessFlags srcAccess, VkAccessFlags dstAccess,
                                      VkPipelineStageFlags srcStage, VkPipelineStageFlags dstStage) {
    VkImageMemoryBarrier barrier = {
        .sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER,
        .srcAccessMask = srcAccess,
        .dstAccessMask = dstAccess,
        .oldLayout = oldLayout,
        .newLayout = newLayout,
        .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
        .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
        .image = image,
        .subresourceRange = {
            .aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
            .baseMipLevel = baseLevel,
            .levelCount = levelCount,
            .baseArrayLayer = 0,
            .layerCount = 1
        }
    };

    vkCmdPipelineBarrier(cmd, srcStage, dstStage, 0, 0, NULL, 0, NULL, 1, &barrier);
}

/*
 * One command buffer: copy the provided level(s), then (GENERATE) blit each level from the previous one,
 * and leave every level in SHADER_READ_ONLY_OPTIMAL.
 */
static int VANILLA_VK_uploadTexture(VANILLA_Texture* texture, const VANILLA_TextureCreateInfo* createInfo, const VANILLA_VK_TexturePlan* plan) {
    /* Fill the upload buffer (RGB8 gets an opaque alpha byte per texel). */
    if (plan->srcTexel == plan->gpuTexel) {
        memcpy(vulkan.mappedTextureUploadPtr, createInfo->data, plan->srcBytes);
    }
    else {
        const uint8_t* src = (const uint8_t*)createInfo->data;
        uint8_t*       dst = (uint8_t*)vulkan.mappedTextureUploadPtr;
        uint64_t texels = plan->srcBytes / 3;

        for (uint64_t i = 0; i < texels; i++) {
            dst[0] = src[0]; dst[1] = src[1]; dst[2] = src[2]; dst[3] = 0xFF;
            src += 3; dst += 4;
        }
    }

    VkCommandBuffer cmd = VANILLA_VK_beginSingleTimeCommands();
    if (cmd == VK_NULL_HANDLE) { return 1; }

    VkImage image = texture->image;
    uint32_t levels = plan->levels;

    VANILLA_VK_textureBarrier(cmd, image, 0, levels,
        VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
        0, VK_ACCESS_TRANSFER_WRITE_BIT,
        VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT);

    VkBufferImageCopy regions[32];
    VkDeviceSize offset = 0;

    for (uint32_t i = 0; i < plan->copiedLevels; i++) {
        uint32_t w = VANILLA_VK_mipExtent(texture->width, i);
        uint32_t h = VANILLA_VK_mipExtent(texture->height, i);

        regions[i] = (VkBufferImageCopy){
            .bufferOffset = offset,
            .bufferRowLength = 0,      /* tightly packed */
            .bufferImageHeight = 0,
            .imageSubresource = { VK_IMAGE_ASPECT_COLOR_BIT, i, 0, 1 },
            .imageOffset = { 0, 0, 0 },
            .imageExtent = { w, h, 1 }
        };

        offset += (VkDeviceSize)w * h * plan->gpuTexel;
    }

    vkCmdCopyBufferToImage(cmd, vulkan.textureUploadBuffer, image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, plan->copiedLevels, regions);

    if (plan->generate) {
        for (uint32_t i = 1; i < levels; i++) {
            /* level i-1 is complete: read it for the blit */
            VANILLA_VK_textureBarrier(cmd, image, i - 1, 1,
                VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                VK_ACCESS_TRANSFER_WRITE_BIT, VK_ACCESS_TRANSFER_READ_BIT,
                VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT);

            VkImageBlit blit = {
                .srcSubresource = { VK_IMAGE_ASPECT_COLOR_BIT, i - 1, 0, 1 },
                .srcOffsets = { { 0, 0, 0 }, { (int32_t)VANILLA_VK_mipExtent(texture->width, i - 1), (int32_t)VANILLA_VK_mipExtent(texture->height, i - 1), 1 } },
                .dstSubresource = { VK_IMAGE_ASPECT_COLOR_BIT, i, 0, 1 },
                .dstOffsets = { { 0, 0, 0 }, { (int32_t)VANILLA_VK_mipExtent(texture->width, i), (int32_t)VANILLA_VK_mipExtent(texture->height, i), 1 } }
            };

            vkCmdBlitImage(cmd,
                image, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                1, &blit, plan->linearBlit ? VK_FILTER_LINEAR : VK_FILTER_NEAREST);

            VANILLA_VK_textureBarrier(cmd, image, i - 1, 1,
                VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
                VK_ACCESS_TRANSFER_READ_BIT, VK_ACCESS_SHADER_READ_BIT,
                VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT | VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT);
        }

        VANILLA_VK_textureBarrier(cmd, image, levels - 1, 1,
            VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
            VK_ACCESS_TRANSFER_WRITE_BIT, VK_ACCESS_SHADER_READ_BIT,
            VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT | VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT);
    }
    else {
        VANILLA_VK_textureBarrier(cmd, image, 0, levels,
            VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
            VK_ACCESS_TRANSFER_WRITE_BIT, VK_ACCESS_SHADER_READ_BIT,
            VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT | VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT);
    }

    return VANILLA_VK_endSingleTimeCommands(cmd);
}

int createDefaultTexture(void) {
    uint32_t pixels[1] = {
        0xFFFFFFFF
    };

    const VANILLA_TextureCreateInfo createInfo = {
        .width = 1,
        .height = 1,
        .format = VANILLA_TEXTUREFORMAT_RGBA8_UNORM,
        .mipmapMode = VANILLA_MIPMAPMODE_PROVIDED,
        .mipLevels = 1,
        .data = pixels,
        .dataSize = sizeof(pixels)
    };

    VANILLA_Texture* texture = &vulkan.defaultTexture;

    VANILLA_VK_TexturePlan plan;
    if (VANILLA_VK_planTexture(&createInfo, &plan) != VANILLA_WINDOW_SUCCESS) { return 1; }

    VANILLA_WINDOW_RESULT result = VANILLA_VK_createImage(texture, &createInfo, &plan);
    if (result != VANILLA_WINDOW_SUCCESS) { return 1; }

    result = VANILLA_VK_createImageView(texture);
    if (result != VANILLA_WINDOW_SUCCESS) {
        VANILLA_gmunmap(texture->memory);
        vkDestroyImage(vulkan.device, texture->image, NULL);

        return 1;
    }

    result = VANILLA_VK_uploadTexture(texture, &createInfo, &plan);
    if (result != VANILLA_WINDOW_SUCCESS) {
        vkDestroyImageView(vulkan.device, texture->imageView, NULL);
        VANILLA_gmunmap(texture->memory);
        vkDestroyImage(vulkan.device, texture->image, NULL);

        return 1;
    }

    texture->isValid = true;
    return 0;
}

VANILLA_EXPORT VANILLA_WINDOW_RESULT VANILLA_createTexture(const VANILLA_TextureCreateInfo* createInfo, TextureID* outTextureID) {
    if (didstart == false) { return VANILLA_WINDOW_ERR_DIDNT_START; }
    if (createInfo == NULL || outTextureID == NULL || createInfo->data == NULL) { return VANILLA_WINDOW_ERR_NULL_POINTER; }
    if (vulkan.textureFreeSlotCount == 0) { return VANILLA_WINDOW_ERR_CAPACITY_REACHED; }

    VANILLA_VK_TexturePlan plan;
    VANILLA_WINDOW_RESULT planResult = VANILLA_VK_planTexture(createInfo, &plan);
    if (planResult != VANILLA_WINDOW_SUCCESS) { return planResult; }

    uint32_t slot = vulkan.textureFreeSlots[--vulkan.textureFreeSlotCount];
    VANILLA_Texture* texture = &vulkan.textures[slot];

    VANILLA_WINDOW_RESULT result = VANILLA_VK_createImage(texture, createInfo, &plan);
    if (result != VANILLA_WINDOW_SUCCESS) {
        vulkan.textureFreeSlots[vulkan.textureFreeSlotCount++] = slot;
        return VANILLA_WINDOW_ERR_CREATE_TEXTURE;
    }

    result = VANILLA_VK_createImageView(texture);
    if (result != VANILLA_WINDOW_SUCCESS) {
        VANILLA_gmunmap(texture->memory);
        vkDestroyImage(vulkan.device, texture->image, NULL);

        texture->memory    = vulkan.defaultTexture.memory;
        texture->image     = vulkan.defaultTexture.image;

        vulkan.textureFreeSlots[vulkan.textureFreeSlotCount++] = slot;

        return VANILLA_WINDOW_ERR_CREATE_TEXTURE;
    }

    result = VANILLA_VK_uploadTexture(texture, createInfo, &plan);
    if (result != VANILLA_WINDOW_SUCCESS) {
        vkDestroyImageView(vulkan.device, texture->imageView, NULL);
        VANILLA_gmunmap(texture->memory);
        vkDestroyImage(vulkan.device, texture->image, NULL);

        texture->imageView = vulkan.defaultTexture.imageView;
        texture->memory    = vulkan.defaultTexture.memory;
        texture->image     = vulkan.defaultTexture.image;

        vulkan.textureFreeSlots[vulkan.textureFreeSlotCount++] = slot;

        return VANILLA_WINDOW_ERR_CREATE_TEXTURE;
    }

    texture->isValid = true;
    *outTextureID = (TextureID){ slot, ++vulkan.textureGenerations[slot] };

    return VANILLA_WINDOW_SUCCESS;
}

VANILLA_EXPORT VANILLA_WINDOW_RESULT VANILLA_destroyTexture(TextureID textureID) {
    if (didstart == false) { return VANILLA_WINDOW_ERR_DIDNT_START; }
    if (textureID.slot >= textureLimit) { return VANILLA_WINDOW_ERR_INVALID_TEXTURE; }
    if (vulkan.textureGenerations[textureID.slot] != textureID.generation) { return VANILLA_WINDOW_ERR_INVALID_TEXTURE; }

    VANILLA_Texture* texture = &vulkan.textures[textureID.slot];

    if (texture->isValid == false) { return VANILLA_WINDOW_ERR_INVALID_TEXTURE; }

    vkDestroyImageView(
        vulkan.device,
        texture->imageView,
        NULL
    );

    VANILLA_gmunmap(texture->memory);

    vkDestroyImage(
        vulkan.device,
        texture->image,
        NULL
    );

    texture->imageView = vulkan.defaultTexture.imageView;
    texture->memory    = vulkan.defaultTexture.memory;
    texture->image     = vulkan.defaultTexture.image;

    texture->isValid = false;
    vulkan.textureFreeSlots[vulkan.textureFreeSlotCount++] = textureID.slot;

    return VANILLA_WINDOW_SUCCESS;
}

VANILLA_EXPORT void VANILLA_setTextureLimit(uint32_t limit) {
    if (didstart || didsettexturelimit) { return; }
    if (limit <= 0) { limit = 1; }

    textureLimit = limit;
    didsettexturelimit = true;
}

VANILLA_EXPORT void VANILLA_setTextureSizeLimit(uint32_t limit) {
    if (didstart || didsettexturesizelimit) { return; }
    if (limit <= 0) { limit = 1; }

    textureSizeLimit = limit;
    didsettexturesizelimit = true;
}