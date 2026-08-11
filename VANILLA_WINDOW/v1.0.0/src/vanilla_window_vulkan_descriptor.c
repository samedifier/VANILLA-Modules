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
#include <stdio.h>
#include <stdint.h>

VkDescriptorSetLayout VANILLA_CreateDescriptorSetLayout(void) {
    VkDescriptorSetLayoutBinding bindings[7] = {0};

    bindings[0].binding = 0;
    bindings[0].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    bindings[0].descriptorCount = 1;
    bindings[0].stageFlags = VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_COMPUTE_BIT;

    bindings[1].binding = 1;
    bindings[1].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    bindings[1].descriptorCount = 1;
    bindings[1].stageFlags = VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT;

    bindings[2].binding = 2;
    bindings[2].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    bindings[2].descriptorCount = 1;
    bindings[2].stageFlags = VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_COMPUTE_BIT;

    bindings[3].binding = 3;
    bindings[3].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    bindings[3].descriptorCount = 1;
    bindings[3].stageFlags = VK_SHADER_STAGE_VERTEX_BIT;

    bindings[4].binding = 4;
    bindings[4].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    bindings[4].descriptorCount = 1;
    bindings[4].stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;

    bindings[5].binding = 5;
    bindings[5].descriptorType = VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE;
    bindings[5].descriptorCount = textureLimit;
    bindings[5].stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;

    bindings[6].binding = 6;
    bindings[6].descriptorType = VK_DESCRIPTOR_TYPE_SAMPLER;
    bindings[6].descriptorCount = samplerLimit;
    bindings[6].stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;

    VkDescriptorBindingFlags bindingFlags[7] = {
        0,
        0,
        0,
        0,
        0,

        VK_DESCRIPTOR_BINDING_PARTIALLY_BOUND_BIT |
        VK_DESCRIPTOR_BINDING_UPDATE_AFTER_BIND_BIT,

        VK_DESCRIPTOR_BINDING_PARTIALLY_BOUND_BIT |
        VK_DESCRIPTOR_BINDING_UPDATE_AFTER_BIND_BIT
    };

    VkDescriptorSetLayoutBindingFlagsCreateInfo bindingFlagsInfo = {
        .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_BINDING_FLAGS_CREATE_INFO,
        .bindingCount = 7,
        .pBindingFlags = bindingFlags
    };

    VkDescriptorSetLayoutCreateInfo layoutInfo = {0};
    layoutInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
    layoutInfo.bindingCount = 7;
    layoutInfo.pBindings = bindings;
    layoutInfo.flags = VK_DESCRIPTOR_SET_LAYOUT_CREATE_UPDATE_AFTER_BIND_POOL_BIT;
    layoutInfo.pNext = &bindingFlagsInfo;

    VkDescriptorSetLayout layout;

    if (vkCreateDescriptorSetLayout(
            vulkan.device,
            &layoutInfo,
            NULL,
            &layout) != VK_SUCCESS)
    {
        return VK_NULL_HANDLE;
    }

    vulkan.descriptorSetLayout = layout;

    return layout;
}

int VANILLA_CreateDescriptor(void) {
    VkDescriptorPoolSize poolSizes[3] = {0};

    poolSizes[0].type = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    poolSizes[0].descriptorCount = 5;

    poolSizes[1].type = VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE;
    poolSizes[1].descriptorCount = textureLimit;

    poolSizes[2].type = VK_DESCRIPTOR_TYPE_SAMPLER;
    poolSizes[2].descriptorCount = samplerLimit;

    VkDescriptorPoolCreateInfo poolInfo = {0};
    poolInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
    poolInfo.flags = VK_DESCRIPTOR_POOL_CREATE_UPDATE_AFTER_BIND_BIT;
    poolInfo.poolSizeCount = 3;
    poolInfo.pPoolSizes = poolSizes;
    poolInfo.maxSets = 1;

    if (vkCreateDescriptorPool(
            vulkan.device,
            &poolInfo,
            NULL,
            &vulkan.descriptorPool) != VK_SUCCESS) {
        return 1;
    }

    VkDescriptorSetAllocateInfo allocInfo = {0};
    allocInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
    allocInfo.descriptorPool = vulkan.descriptorPool;
    allocInfo.descriptorSetCount = 1;
    allocInfo.pSetLayouts = &vulkan.descriptorSetLayout;

    if (vkAllocateDescriptorSets(
            vulkan.device,
            &allocInfo,
            &vulkan.descriptorSet) != VK_SUCCESS) {
        return 1;
    }

    VkDescriptorBufferInfo transformInfo = {
        .buffer = vulkan.globalTransformBuffer,
        .offset = 0,
        .range = VK_WHOLE_SIZE
    };

    VkDescriptorBufferInfo lookupInfo = {
        .buffer = vulkan.modelLookupBuffer,
        .offset = 0,
        .range = VK_WHOLE_SIZE
    };

    VkDescriptorBufferInfo vertexInfo = {
        .buffer = vulkan.globalVertexBuffer,
        .offset = 0,
        .range = VK_WHOLE_SIZE
    };

    VkDescriptorBufferInfo materialSlotInfo = {
        .buffer = vulkan.materialSlotBuffer,
        .offset = 0,
        .range = VK_WHOLE_SIZE
    };

    VkDescriptorBufferInfo materialInfo = {
        .buffer = vulkan.materialBuffer,
        .offset = 0,
        .range = VK_WHOLE_SIZE
    };

    vulkan.textureInfos = VANILLA_mmap(sizeof(VkDescriptorImageInfo) * textureLimit);
    vulkan.samplerInfos = VANILLA_mmap(sizeof(VkDescriptorImageInfo) * samplerLimit);

    if (vulkan.textureInfos == VANILLA_MAP_FAILED || vulkan.samplerInfos == VANILLA_MAP_FAILED) {
        if (vulkan.textureInfos != VANILLA_MAP_FAILED) {
            VANILLA_munmap(vulkan.textureInfos, sizeof(VkDescriptorImageInfo) * textureLimit);
        }

        if (vulkan.samplerInfos != VANILLA_MAP_FAILED) {
            VANILLA_munmap(vulkan.samplerInfos, sizeof(VkDescriptorImageInfo) * samplerLimit);
        }

        return 1;
    }

    for (uint32_t i = 0; i < textureLimit; i++) {
        if (vulkan.textures[i].imageView == VK_NULL_HANDLE) {
            vulkan.textureInfos[i].imageView = vulkan.defaultTexture.imageView;
        } else {
            vulkan.textureInfos[i].imageView = vulkan.textures[i].imageView;
        }

        vulkan.textureInfos[i].sampler = vulkan.defaultSampler;
        vulkan.textureInfos[i].imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    }

    for (uint32_t i = 0; i < samplerLimit; i++) {
        if (vulkan.samplers[i] == VK_NULL_HANDLE) {
            vulkan.samplerInfos[i].sampler = vulkan.defaultSampler;
        } else {
            vulkan.samplerInfos[i].sampler = vulkan.samplers[i];
        }

        vulkan.samplerInfos[i].imageView = VK_NULL_HANDLE;
        vulkan.samplerInfos[i].imageLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    }


    VkWriteDescriptorSet writes[7] = {0};

    writes[0].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    writes[0].dstSet = vulkan.descriptorSet;
    writes[0].dstBinding = 0;
    writes[0].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    writes[0].descriptorCount = 1;
    writes[0].pBufferInfo = &transformInfo;

    writes[1].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    writes[1].dstSet = vulkan.descriptorSet;
    writes[1].dstBinding = 1;
    writes[1].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    writes[1].descriptorCount = 1;
    writes[1].pBufferInfo = &lookupInfo;

    writes[2].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    writes[2].dstSet = vulkan.descriptorSet;
    writes[2].dstBinding = 2;
    writes[2].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    writes[2].descriptorCount = 1;
    writes[2].pBufferInfo = &vertexInfo;

    writes[3].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    writes[3].dstSet = vulkan.descriptorSet;
    writes[3].dstBinding = 3;
    writes[3].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    writes[3].descriptorCount = 1;
    writes[3].pBufferInfo = &materialSlotInfo;

    writes[4].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    writes[4].dstSet = vulkan.descriptorSet;
    writes[4].dstBinding = 4;
    writes[4].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    writes[4].descriptorCount = 1;
    writes[4].pBufferInfo = &materialInfo;

    writes[5].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    writes[5].dstSet = vulkan.descriptorSet;
    writes[5].dstBinding = 5;
    writes[5].descriptorType = VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE;
    writes[5].descriptorCount = textureLimit;
    writes[5].pImageInfo = vulkan.textureInfos;
    
    writes[6].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    writes[6].dstSet = vulkan.descriptorSet;
    writes[6].dstBinding = 6;
    writes[6].descriptorType = VK_DESCRIPTOR_TYPE_SAMPLER;
    writes[6].descriptorCount = samplerLimit;
    writes[6].pImageInfo = vulkan.samplerInfos;

    vkUpdateDescriptorSets(
        vulkan.device,
        7,
        writes,
        0,
        NULL
    );

    return 0;
}

VkPipelineVertexInputStateCreateInfo VANILLA_makePipelineVertexInput(VkVertexInputBindingDescription* outBindingDesc, VkVertexInputAttributeDescription* outAttrDescs) {
    outBindingDesc->binding = 0;
    outBindingDesc->stride = stride;
    outBindingDesc->inputRate = VK_VERTEX_INPUT_RATE_VERTEX;

    for (uint32_t i = 0; i < vertexLayout.attributeCount; i++) {
        VkFormat format = VK_FORMAT_R32G32B32A32_SFLOAT;
        VANILLA_VertexFormat vformat = vertexLayout.attributes[i].format;

        switch (vformat){
            case VANILLA_VERTEXFORMAT_R32_FLOAT:
                format = VK_FORMAT_R32_SFLOAT;
                break;
        
            case VANILLA_VERTEXFORMAT_R32G32_FLOAT:
                format = VK_FORMAT_R32G32_SFLOAT;
                break;
        
            case VANILLA_VERTEXFORMAT_R32G32B32_FLOAT:
                format = VK_FORMAT_R32G32B32_SFLOAT;
                break;
        
            case VANILLA_VERTEXFORMAT_R32G32B32A32_FLOAT:
                format = VK_FORMAT_R32G32B32A32_SFLOAT;
                break;
        
            default:
                printf("[VANILLA] invalid vertex format\n");
                format = VK_FORMAT_R32G32B32A32_SFLOAT;
        }

        outAttrDescs[i].binding = 0;
        outAttrDescs[i].location = vertexLayout.attributes[i].location;
        outAttrDescs[i].format = format;
        outAttrDescs[i].offset = vertexLayout.attributes[i].vanillaOffset;
    }

    VkPipelineVertexInputStateCreateInfo info = {0};
    info.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
    info.vertexBindingDescriptionCount = 1;
    info.pVertexBindingDescriptions = outBindingDesc;
    info.vertexAttributeDescriptionCount = vertexLayout.attributeCount;
    info.pVertexAttributeDescriptions = outAttrDescs;

    return info;
}

void VANILLA_CleanupDescriptorLayout(void) {
    if (vulkan.descriptorSetLayout != VK_NULL_HANDLE) {
        vkDestroyDescriptorSetLayout(vulkan.device, vulkan.descriptorSetLayout, NULL);
        vulkan.descriptorSetLayout = VK_NULL_HANDLE;
    }

    if (vulkan.textureInfos != VANILLA_MAP_FAILED) { VANILLA_munmap(vulkan.textureInfos, sizeof(VkDescriptorImageInfo) * textureLimit); }
    if (vulkan.samplerInfos != VANILLA_MAP_FAILED) { VANILLA_munmap(vulkan.samplerInfos, sizeof(VkDescriptorImageInfo) * samplerLimit); }
}