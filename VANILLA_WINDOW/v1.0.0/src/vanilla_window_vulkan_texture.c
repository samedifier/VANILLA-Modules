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

static int VANILLA_VK_createTextureUploadBuffer(void) {
    VkBufferCreateInfo bufferCreateInfo = {
        .sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO,
        .size = textureSizeLimit,
        .usage = VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
        .sharingMode = VK_SHARING_MODE_EXCLUSIVE
    };

    if (vkCreateBuffer(vulkan.device, &bufferCreateInfo, NULL, &vulkan.textureUploadBuffer) != VK_SUCCESS) {
        return 1;
    }

    VkMemoryRequirements memoryRequirements;
    vkGetBufferMemoryRequirements(vulkan.device, vulkan.textureUploadBuffer, &memoryRequirements);

    vulkan.textureUploadMemory = VANILLA_gmmap(
        &memoryRequirements,
        VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT |
        VK_MEMORY_PROPERTY_HOST_COHERENT_BIT
    );

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
            return VK_FORMAT_R8G8B8_UNORM;

        case VANILLA_TEXTUREFORMAT_RGBA8_UNORM:
            return VK_FORMAT_R8G8B8A8_UNORM;

        case VANILLA_TEXTUREFORMAT_R8_SRGB:
            return VK_FORMAT_R8_SRGB;

        case VANILLA_TEXTUREFORMAT_RG8_SRGB:
            return VK_FORMAT_R8G8_SRGB;

        case VANILLA_TEXTUREFORMAT_RGB8_SRGB:
            return VK_FORMAT_R8G8B8_SRGB;

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

static VANILLA_RESULT VANILLA_VK_createImage(VANILLA_Texture* texture, const VANILLA_TextureCreateInfo* createInfo) {
    VkImageCreateInfo imageInfo = {
        .sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO,

        .imageType = VK_IMAGE_TYPE_2D,

        .extent = {
            .width  = createInfo->width,
            .height = createInfo->height,
            .depth  = 1
        },

        .mipLevels = createInfo->mipLevels,
        .arrayLayers = 1,

        .format = VANILLA_VK_getFormat(createInfo->format),

        .tiling = VK_IMAGE_TILING_OPTIMAL,

        .initialLayout = VK_IMAGE_LAYOUT_UNDEFINED,

        .usage =
            VK_IMAGE_USAGE_TRANSFER_DST_BIT |
            VK_IMAGE_USAGE_SAMPLED_BIT,

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

    texture->memory = VANILLA_gmmap(
        &requirements,
        properties
    );

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
    texture->mipLevels = createInfo->mipLevels;

    return VANILLA_SUCCESS;
}

static VANILLA_RESULT VANILLA_VK_createImageView(VANILLA_Texture* texture) {
    VkImageAspectFlags aspectMask;

    switch (texture->format) {
        case VANILLA_TEXTUREFORMAT_DEPTH32_FLOAT:
            aspectMask = VK_IMAGE_ASPECT_DEPTH_BIT;
            break;

        case VANILLA_TEXTUREFORMAT_DEPTH24_STENCIL8:
            aspectMask =
                VK_IMAGE_ASPECT_DEPTH_BIT |
                VK_IMAGE_ASPECT_STENCIL_BIT;
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
            &texture->imageView) != VK_SUCCESS) {
        return 1;
    }

    return VANILLA_SUCCESS;
}

static int VANILLA_VK_transitionImageLayout(VkImage image, VkImageAspectFlags aspectMask, VkImageLayout oldLayout, VkImageLayout newLayout, uint32_t mipLevels) {
    VkCommandBuffer commandBuffer = VANILLA_VK_beginSingleTimeCommands();
    if (commandBuffer == VK_NULL_HANDLE) {
        return 1;
    }

    VkImageMemoryBarrier barrier = {
        .sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER,

        .oldLayout = oldLayout,
        .newLayout = newLayout,

        .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
        .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,

        .image = image,

        .subresourceRange = {
            .aspectMask = aspectMask,
            .baseMipLevel = 0,
            .levelCount = mipLevels,
            .baseArrayLayer = 0,
            .layerCount = 1
        }
    };

    VkPipelineStageFlags sourceStage;
    VkPipelineStageFlags destinationStage;

    if (oldLayout == VK_IMAGE_LAYOUT_UNDEFINED &&
        newLayout == VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL) {

        barrier.srcAccessMask = 0;
        barrier.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;

        sourceStage = VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT;
        destinationStage = VK_PIPELINE_STAGE_TRANSFER_BIT;
    }
    else if (oldLayout == VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL &&
             newLayout == VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL) {

        barrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
        barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;

        sourceStage = VK_PIPELINE_STAGE_TRANSFER_BIT;
        destinationStage = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
    }
    else {
        vkFreeCommandBuffers(
            vulkan.device,
            vulkan.commandPool,
            1,
            &commandBuffer);

        return 1;
    }

    vkCmdPipelineBarrier(
        commandBuffer,
        sourceStage,
        destinationStage,
        0,
        0, NULL,
        0, NULL,
        1, &barrier
    );

    return VANILLA_VK_endSingleTimeCommands(commandBuffer);
}

static int VANILLA_VK_copyBufferToImage(VkImage image, uint32_t width, uint32_t height) {
    VkCommandBuffer commandBuffer = VANILLA_VK_beginSingleTimeCommands();
    if (commandBuffer == VK_NULL_HANDLE) {
        return 1;
    }

    VkBufferImageCopy region = {
        .bufferOffset = 0,
        .bufferRowLength = width,
        .bufferImageHeight = 0,

        .imageSubresource = {
            .aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
            .mipLevel = 0,
            .baseArrayLayer = 0,
            .layerCount = 1
        },

        .imageOffset = {
            .x = 0,
            .y = 0,
            .z = 0
        },

        .imageExtent = {
            .width = width,
            .height = height,
            .depth = 1
        }
    };

    vkCmdCopyBufferToImage(
        commandBuffer,
        vulkan.textureUploadBuffer,
        image,
        VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
        1,
        &region
    );

    return VANILLA_VK_endSingleTimeCommands(commandBuffer);
}

static int VANILLA_VK_uploadTexture(VANILLA_Texture* texture, const VANILLA_TextureCreateInfo* createInfo) {
    memcpy(
        vulkan.mappedTextureUploadPtr,
        createInfo->data,
        createInfo->dataSize
    );

    VANILLA_RESULT result;

    result = VANILLA_VK_transitionImageLayout(
        texture->image,
        VK_IMAGE_ASPECT_COLOR_BIT,
        VK_IMAGE_LAYOUT_UNDEFINED,
        VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
        texture->mipLevels
    );

    if (result != VANILLA_SUCCESS) {
        return result;
    }

    result = VANILLA_VK_copyBufferToImage(
        texture->image,
        texture->width,
        texture->height
    );

    if (result != VANILLA_SUCCESS) {
        return result;
    }

    result = VANILLA_VK_transitionImageLayout(
        texture->image,
        VK_IMAGE_ASPECT_COLOR_BIT,
        VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
        VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
        texture->mipLevels
    );

    if (result != VANILLA_SUCCESS) {
        return result;
    }

    return 0;
}

int createDefaultTexture() {
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

    VANILLA_RESULT result = VANILLA_VK_createImage(texture, &createInfo);
    if (result != VANILLA_SUCCESS) { return 1; }

    result = VANILLA_VK_createImageView(texture);
    if (result != VANILLA_SUCCESS) {
        VANILLA_gmunmap(texture->memory);
        vkDestroyImage(vulkan.device, texture->image, NULL);

        return 1;
    }

    result = VANILLA_VK_uploadTexture(texture, &createInfo);
    if (result != VANILLA_SUCCESS) {
        vkDestroyImageView(vulkan.device, texture->imageView, NULL);
        VANILLA_gmunmap(texture->memory);
        vkDestroyImage(vulkan.device, texture->image, NULL);

        return 1;
    }

    texture->isValid = true;
    return 0;
}

VANILLA_EXPORT VANILLA_RESULT VANILLA_createTexture(const VANILLA_TextureCreateInfo* createInfo, TextureID* outTextureID) {
    if (vulkan.textureFreeSlotCount == 0) { return VANILLA_ERR_CAPACITY_REACHED; }
    if (createInfo->dataSize > textureSizeLimit) { return VANILLA_ERR_TEXTURE_SIZE_LIMIT; }
    if (createInfo->data == NULL) { return VANILLA_ERR_NULL_POINTER; }

    uint32_t slot = vulkan.textureFreeSlots[--vulkan.textureFreeSlotCount];
    VANILLA_Texture* texture = &vulkan.textures[slot];

    VANILLA_RESULT result = VANILLA_VK_createImage(texture, createInfo);
    if (result != VANILLA_SUCCESS) {
        vulkan.textureFreeSlots[vulkan.textureFreeSlotCount++] = slot;
        return VANILLA_ERR_CREATE_TEXTURE;
    }

    result = VANILLA_VK_createImageView(texture);
    if (result != VANILLA_SUCCESS) {
        VANILLA_gmunmap(texture->memory);
        vkDestroyImage(vulkan.device, texture->image, NULL);

        texture->memory    = vulkan.defaultTexture.memory;
        texture->image     = vulkan.defaultTexture.image;

        vulkan.textureFreeSlots[vulkan.textureFreeSlotCount++] = slot;

        return VANILLA_ERR_CREATE_TEXTURE;
    }

    result = VANILLA_VK_uploadTexture(texture, createInfo);
    if (result != VANILLA_SUCCESS) {
        vkDestroyImageView(vulkan.device, texture->imageView, NULL);
        VANILLA_gmunmap(texture->memory);
        vkDestroyImage(vulkan.device, texture->image, NULL);

        texture->imageView = vulkan.defaultTexture.imageView;
        texture->memory    = vulkan.defaultTexture.memory;
        texture->image     = vulkan.defaultTexture.image;

        vulkan.textureFreeSlots[vulkan.textureFreeSlotCount++] = slot;

        return VANILLA_ERR_CREATE_TEXTURE;
    }

    texture->isValid = true;
    *outTextureID = (TextureID){ slot, ++vulkan.textureGenerations[slot] };

    return VANILLA_SUCCESS;
}

VANILLA_EXPORT VANILLA_RESULT VANILLA_destroyTexture(TextureID textureID) {
    if (textureID.slot >= textureLimit) { return VANILLA_ERR_INVALID_TEXTURE; }
    if (vulkan.textureGenerations[textureID.slot] != textureID.generation) { return VANILLA_ERR_INVALID_TEXTURE; }

    VANILLA_Texture* texture = &vulkan.textures[textureID.slot];

    if (texture->isValid == false) { return VANILLA_ERR_INVALID_TEXTURE; }

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

    return VANILLA_SUCCESS;
}
