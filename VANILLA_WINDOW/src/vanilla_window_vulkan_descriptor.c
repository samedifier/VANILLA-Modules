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

int VANILLA_CreateDescriptorSetLayout(void) {
    const uint32_t bindingCount = VANILLA_BINDING_USER_BUFFERS + vulkan.userBufferCount;
    VkDescriptorSetLayoutBinding bindings[VANILLA_BINDING_USER_BUFFERS + VANILLA_USER_BUFFER_LIMIT] = {0};

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

    bindings[7].binding = 7;
    bindings[7].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    bindings[7].descriptorCount = 1;
    bindings[7].stageFlags = VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT;

    /* Lighting */
    bindings[VANILLA_BINDING_LIGHTS].binding = VANILLA_BINDING_LIGHTS;
    bindings[VANILLA_BINDING_LIGHTS].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    bindings[VANILLA_BINDING_LIGHTS].descriptorCount = 1;
    bindings[VANILLA_BINDING_LIGHTS].stageFlags = VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT;

    bindings[VANILLA_BINDING_LIGHTING_INFO].binding = VANILLA_BINDING_LIGHTING_INFO;
    bindings[VANILLA_BINDING_LIGHTING_INFO].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    bindings[VANILLA_BINDING_LIGHTING_INFO].descriptorCount = 1;
    bindings[VANILLA_BINDING_LIGHTING_INFO].stageFlags = VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT;

    bindings[VANILLA_BINDING_LIGHT_VOLUME].binding = VANILLA_BINDING_LIGHT_VOLUME;
    bindings[VANILLA_BINDING_LIGHT_VOLUME].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    bindings[VANILLA_BINDING_LIGHT_VOLUME].descriptorCount = 3;
    bindings[VANILLA_BINDING_LIGHT_VOLUME].stageFlags = VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT;

    bindings[VANILLA_BINDING_SHADOW_MAP].binding = VANILLA_BINDING_SHADOW_MAP;
    bindings[VANILLA_BINDING_SHADOW_MAP].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    bindings[VANILLA_BINDING_SHADOW_MAP].descriptorCount = 1;
    bindings[VANILLA_BINDING_SHADOW_MAP].stageFlags = VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT;

    bindings[VANILLA_BINDING_SHADOW_DEPTH].binding = VANILLA_BINDING_SHADOW_DEPTH;
    bindings[VANILLA_BINDING_SHADOW_DEPTH].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    bindings[VANILLA_BINDING_SHADOW_DEPTH].descriptorCount = 1;
    bindings[VANILLA_BINDING_SHADOW_DEPTH].stageFlags = VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT;

    bindings[VANILLA_BINDING_LOCAL_SHADOW_MAP].binding = VANILLA_BINDING_LOCAL_SHADOW_MAP;
    bindings[VANILLA_BINDING_LOCAL_SHADOW_MAP].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    bindings[VANILLA_BINDING_LOCAL_SHADOW_MAP].descriptorCount = 1;
    bindings[VANILLA_BINDING_LOCAL_SHADOW_MAP].stageFlags = VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT;

    bindings[VANILLA_BINDING_LOCAL_SHADOW_DEPTH].binding = VANILLA_BINDING_LOCAL_SHADOW_DEPTH;
    bindings[VANILLA_BINDING_LOCAL_SHADOW_DEPTH].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    bindings[VANILLA_BINDING_LOCAL_SHADOW_DEPTH].descriptorCount = 1;
    bindings[VANILLA_BINDING_LOCAL_SHADOW_DEPTH].stageFlags = VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT;

    bindings[VANILLA_BINDING_ENV_SPECULAR].binding = VANILLA_BINDING_ENV_SPECULAR;
    bindings[VANILLA_BINDING_ENV_SPECULAR].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    bindings[VANILLA_BINDING_ENV_SPECULAR].descriptorCount = 1;
    bindings[VANILLA_BINDING_ENV_SPECULAR].stageFlags = VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT;

    bindings[VANILLA_BINDING_BRDF_LUT].binding = VANILLA_BINDING_BRDF_LUT;
    bindings[VANILLA_BINDING_BRDF_LUT].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    bindings[VANILLA_BINDING_BRDF_LUT].descriptorCount = 1;
    bindings[VANILLA_BINDING_BRDF_LUT].stageFlags = VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT;

    bindings[VANILLA_BINDING_AERIAL].binding = VANILLA_BINDING_AERIAL;
    bindings[VANILLA_BINDING_AERIAL].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    bindings[VANILLA_BINDING_AERIAL].descriptorCount = 1;
    bindings[VANILLA_BINDING_AERIAL].stageFlags = VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT;

    for (uint32_t i = 0; i < vulkan.userBufferCount; i++) {
        bindings[VANILLA_BINDING_USER_BUFFERS + i].binding = VANILLA_BINDING_USER_BUFFERS + i;
        bindings[VANILLA_BINDING_USER_BUFFERS + i].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
        bindings[VANILLA_BINDING_USER_BUFFERS + i].descriptorCount = 1;
        bindings[VANILLA_BINDING_USER_BUFFERS + i].stageFlags = VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT;
    }

    VkDescriptorBindingFlags bindingFlags[VANILLA_BINDING_USER_BUFFERS + VANILLA_USER_BUFFER_LIMIT] = {0};
    bindingFlags[5] = VK_DESCRIPTOR_BINDING_PARTIALLY_BOUND_BIT | VK_DESCRIPTOR_BINDING_UPDATE_AFTER_BIND_BIT;
    bindingFlags[6] = VK_DESCRIPTOR_BINDING_PARTIALLY_BOUND_BIT | VK_DESCRIPTOR_BINDING_UPDATE_AFTER_BIND_BIT;

    VkDescriptorSetLayoutBindingFlagsCreateInfo bindingFlagsInfo = {
        .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_BINDING_FLAGS_CREATE_INFO,
        .bindingCount = bindingCount,
        .pBindingFlags = bindingFlags
    };

    VkDescriptorSetLayoutCreateInfo layoutInfo = {0};
    layoutInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
    layoutInfo.bindingCount = bindingCount;
    layoutInfo.pBindings = bindings;
    layoutInfo.flags = VK_DESCRIPTOR_SET_LAYOUT_CREATE_UPDATE_AFTER_BIND_POOL_BIT;
    layoutInfo.pNext = &bindingFlagsInfo;

    VkDescriptorSetLayout layout;
    if (vkCreateDescriptorSetLayout(
        vulkan.device,
        &layoutInfo,
        NULL,
        &layout) != VK_SUCCESS) { return 1; }

    vulkan.descriptorSetLayout = layout;
    return 0;
}

int VANILLA_CreatePostProcessingDescriptorSetLayout(void) {
    VkDescriptorSetLayoutBinding bindings[2] = {0};

    bindings[0].binding = 0;
    bindings[0].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    bindings[0].descriptorCount = vulkan.pipelineCount;
    bindings[0].stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;

    bindings[1].binding = 1;
    bindings[1].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    bindings[1].descriptorCount = 2;
    bindings[1].stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;

    VkDescriptorSetLayoutCreateInfo layoutInfo = {0};
    layoutInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
    layoutInfo.bindingCount = 2;
    layoutInfo.pBindings = bindings;

    VkDescriptorSetLayout layout;
    if (vkCreateDescriptorSetLayout(
        vulkan.device,
        &layoutInfo,
        NULL,
        &layout) != VK_SUCCESS) { return 1; }

    vulkan.postProcessingDescriptorSetLayout = layout;
    return 0;
}

int VANILLA_CreateDescriptor(void) {
    VkDescriptorPoolSize poolSizes[4] = {0};

    /* 0,1,2,3,4,7 + lights + lighting info + user buffers */
    poolSizes[0].type = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    poolSizes[0].descriptorCount = 8 + vulkan.userBufferCount;

    /* light volume (3) + cascade compare/depth (2) + local compare/depth (2) */
    poolSizes[3].type = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    poolSizes[3].descriptorCount = 10;  /* light volume 3, shadows 4, environment 3 */

    poolSizes[1].type = VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE;
    poolSizes[1].descriptorCount = textureLimit;

    poolSizes[2].type = VK_DESCRIPTOR_TYPE_SAMPLER;
    poolSizes[2].descriptorCount = samplerLimit;

    VkDescriptorPoolCreateInfo poolInfo = {0};
    poolInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
    poolInfo.flags = VK_DESCRIPTOR_POOL_CREATE_UPDATE_AFTER_BIND_BIT;
    poolInfo.poolSizeCount = 4;
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

        vulkan.textureInfos[i].sampler = VK_NULL_HANDLE;
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

    VkDescriptorBufferInfo userBufferDescriptorInfos[VANILLA_USER_BUFFER_LIMIT] = {0};
    for (uint32_t i = 0; i < vulkan.userBufferCount; i++) {
        userBufferDescriptorInfos[i] = (VkDescriptorBufferInfo){
            .buffer = vulkan.userBuffers[i],
            .offset = 0,
            .range = VK_WHOLE_SIZE
        };
    }

    VkWriteDescriptorSet writes[VANILLA_BINDING_USER_BUFFERS + VANILLA_USER_BUFFER_LIMIT] = {0};

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

    VkDescriptorBufferInfo aliveFlagsInfo = {
        .buffer = vulkan.aliveFlagsBuffer,
        .offset = 0,
        .range = VK_WHOLE_SIZE
    };

    writes[7].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    writes[7].dstSet = vulkan.descriptorSet;
    writes[7].dstBinding = 7;
    writes[7].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    writes[7].descriptorCount = 1;
    writes[7].pBufferInfo = &aliveFlagsInfo;

    VkDescriptorBufferInfo lightsInfo = {
        .buffer = VANILLA_VK_getLightBuffer(),
        .offset = 0,
        .range = VK_WHOLE_SIZE
    };

    VkDescriptorBufferInfo lightingInfoInfo = {
        .buffer = VANILLA_VK_getLightingInfoBuffer(),
        .offset = 0,
        .range = VK_WHOLE_SIZE
    };

    VkDescriptorImageInfo lightVolumeInfos[3];
    VANILLA_VK_getLightVolumeImageInfos(lightVolumeInfos);

    writes[VANILLA_BINDING_LIGHTS].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    writes[VANILLA_BINDING_LIGHTS].dstSet = vulkan.descriptorSet;
    writes[VANILLA_BINDING_LIGHTS].dstBinding = VANILLA_BINDING_LIGHTS;
    writes[VANILLA_BINDING_LIGHTS].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    writes[VANILLA_BINDING_LIGHTS].descriptorCount = 1;
    writes[VANILLA_BINDING_LIGHTS].pBufferInfo = &lightsInfo;

    writes[VANILLA_BINDING_LIGHTING_INFO].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    writes[VANILLA_BINDING_LIGHTING_INFO].dstSet = vulkan.descriptorSet;
    writes[VANILLA_BINDING_LIGHTING_INFO].dstBinding = VANILLA_BINDING_LIGHTING_INFO;
    writes[VANILLA_BINDING_LIGHTING_INFO].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    writes[VANILLA_BINDING_LIGHTING_INFO].descriptorCount = 1;
    writes[VANILLA_BINDING_LIGHTING_INFO].pBufferInfo = &lightingInfoInfo;

    writes[VANILLA_BINDING_LIGHT_VOLUME].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    writes[VANILLA_BINDING_LIGHT_VOLUME].dstSet = vulkan.descriptorSet;
    writes[VANILLA_BINDING_LIGHT_VOLUME].dstBinding = VANILLA_BINDING_LIGHT_VOLUME;
    writes[VANILLA_BINDING_LIGHT_VOLUME].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    writes[VANILLA_BINDING_LIGHT_VOLUME].descriptorCount = 3;
    writes[VANILLA_BINDING_LIGHT_VOLUME].pImageInfo = lightVolumeInfos;

    VkDescriptorImageInfo shadowCompareInfo;
    VkDescriptorImageInfo shadowDepthInfo;
    VANILLA_VK_getShadowImageInfos(&shadowCompareInfo, &shadowDepthInfo);

    writes[VANILLA_BINDING_SHADOW_MAP].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    writes[VANILLA_BINDING_SHADOW_MAP].dstSet = vulkan.descriptorSet;
    writes[VANILLA_BINDING_SHADOW_MAP].dstBinding = VANILLA_BINDING_SHADOW_MAP;
    writes[VANILLA_BINDING_SHADOW_MAP].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    writes[VANILLA_BINDING_SHADOW_MAP].descriptorCount = 1;
    writes[VANILLA_BINDING_SHADOW_MAP].pImageInfo = &shadowCompareInfo;

    writes[VANILLA_BINDING_SHADOW_DEPTH].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    writes[VANILLA_BINDING_SHADOW_DEPTH].dstSet = vulkan.descriptorSet;
    writes[VANILLA_BINDING_SHADOW_DEPTH].dstBinding = VANILLA_BINDING_SHADOW_DEPTH;
    writes[VANILLA_BINDING_SHADOW_DEPTH].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    writes[VANILLA_BINDING_SHADOW_DEPTH].descriptorCount = 1;
    writes[VANILLA_BINDING_SHADOW_DEPTH].pImageInfo = &shadowDepthInfo;

    VkDescriptorImageInfo localCompareInfo;
    VkDescriptorImageInfo localDepthInfo;
    VANILLA_VK_getLocalShadowImageInfos(&localCompareInfo, &localDepthInfo);

    writes[VANILLA_BINDING_LOCAL_SHADOW_MAP].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    writes[VANILLA_BINDING_LOCAL_SHADOW_MAP].dstSet = vulkan.descriptorSet;
    writes[VANILLA_BINDING_LOCAL_SHADOW_MAP].dstBinding = VANILLA_BINDING_LOCAL_SHADOW_MAP;
    writes[VANILLA_BINDING_LOCAL_SHADOW_MAP].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    writes[VANILLA_BINDING_LOCAL_SHADOW_MAP].descriptorCount = 1;
    writes[VANILLA_BINDING_LOCAL_SHADOW_MAP].pImageInfo = &localCompareInfo;

    writes[VANILLA_BINDING_LOCAL_SHADOW_DEPTH].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    writes[VANILLA_BINDING_LOCAL_SHADOW_DEPTH].dstSet = vulkan.descriptorSet;
    writes[VANILLA_BINDING_LOCAL_SHADOW_DEPTH].dstBinding = VANILLA_BINDING_LOCAL_SHADOW_DEPTH;
    writes[VANILLA_BINDING_LOCAL_SHADOW_DEPTH].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    writes[VANILLA_BINDING_LOCAL_SHADOW_DEPTH].descriptorCount = 1;
    writes[VANILLA_BINDING_LOCAL_SHADOW_DEPTH].pImageInfo = &localDepthInfo;

    VkDescriptorImageInfo envSpecularInfo;
    VkDescriptorImageInfo brdfLutInfo;
    VkDescriptorImageInfo aerialInfo;
    VANILLA_VK_getEnvironmentImageInfos(&envSpecularInfo, &brdfLutInfo, &aerialInfo);

    writes[VANILLA_BINDING_ENV_SPECULAR].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    writes[VANILLA_BINDING_ENV_SPECULAR].dstSet = vulkan.descriptorSet;
    writes[VANILLA_BINDING_ENV_SPECULAR].dstBinding = VANILLA_BINDING_ENV_SPECULAR;
    writes[VANILLA_BINDING_ENV_SPECULAR].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    writes[VANILLA_BINDING_ENV_SPECULAR].descriptorCount = 1;
    writes[VANILLA_BINDING_ENV_SPECULAR].pImageInfo = &envSpecularInfo;

    writes[VANILLA_BINDING_BRDF_LUT].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    writes[VANILLA_BINDING_BRDF_LUT].dstSet = vulkan.descriptorSet;
    writes[VANILLA_BINDING_BRDF_LUT].dstBinding = VANILLA_BINDING_BRDF_LUT;
    writes[VANILLA_BINDING_BRDF_LUT].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    writes[VANILLA_BINDING_BRDF_LUT].descriptorCount = 1;
    writes[VANILLA_BINDING_BRDF_LUT].pImageInfo = &brdfLutInfo;

    writes[VANILLA_BINDING_AERIAL].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    writes[VANILLA_BINDING_AERIAL].dstSet = vulkan.descriptorSet;
    writes[VANILLA_BINDING_AERIAL].dstBinding = VANILLA_BINDING_AERIAL;
    writes[VANILLA_BINDING_AERIAL].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    writes[VANILLA_BINDING_AERIAL].descriptorCount = 1;
    writes[VANILLA_BINDING_AERIAL].pImageInfo = &aerialInfo;

    for (uint32_t i = 0; i < vulkan.userBufferCount; i++) {
        writes[VANILLA_BINDING_USER_BUFFERS + i].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        writes[VANILLA_BINDING_USER_BUFFERS + i].dstSet = vulkan.descriptorSet;
        writes[VANILLA_BINDING_USER_BUFFERS + i].dstBinding = VANILLA_BINDING_USER_BUFFERS + i;
        writes[VANILLA_BINDING_USER_BUFFERS + i].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
        writes[VANILLA_BINDING_USER_BUFFERS + i].descriptorCount = 1;
        writes[VANILLA_BINDING_USER_BUFFERS + i].pBufferInfo = &userBufferDescriptorInfos[i];
    }

    vkUpdateDescriptorSets(
        vulkan.device,
        VANILLA_BINDING_USER_BUFFERS + vulkan.userBufferCount,
        writes,
        0,
        NULL
    );

    return 0;
}

int VANILLA_CreatePostProcessingDescriptor(void) {
    if (vulkan.pipelineCount == 0) { return 0; }

    VkDescriptorPoolSize poolSize = {0};
    poolSize.type = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    poolSize.descriptorCount = vulkan.pipelineCount + 2;

    VkDescriptorPoolCreateInfo poolInfo = {0};
    poolInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
    poolInfo.poolSizeCount = 1;
    poolInfo.pPoolSizes = &poolSize;
    poolInfo.maxSets = 1;

    if (vkCreateDescriptorPool(
        vulkan.device,
        &poolInfo,
        NULL,
        &vulkan.postProcessingDescriptorPool) != VK_SUCCESS) { return 1; }

    VkDescriptorSetAllocateInfo allocInfo = {0};
    allocInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
    allocInfo.descriptorPool = vulkan.postProcessingDescriptorPool;
    allocInfo.descriptorSetCount = 1;
    allocInfo.pSetLayouts = &vulkan.postProcessingDescriptorSetLayout;

    if (vkAllocateDescriptorSets(
        vulkan.device,
        &allocInfo,
        &vulkan.postProcessingDescriptorSet) != VK_SUCCESS) { return 1; }

    VANILLA_UpdatePostProcessingDescriptor();
    return 0;
}

/* Points the pp set at the current pipeline / pp image views (after creation and after a resize). */
void VANILLA_UpdatePostProcessingDescriptor(void) {
    if (vulkan.postProcessingDescriptorSet == VK_NULL_HANDLE) { return; }

    VkDescriptorImageInfo* pipelineImageInfos = VANILLA_mmap(sizeof(VkDescriptorImageInfo) * vulkan.pipelineCount);
    if (pipelineImageInfos == VANILLA_MAP_FAILED) { return; }

    VkDescriptorImageInfo* postProcessingImageInfos = VANILLA_mmap(sizeof(VkDescriptorImageInfo) * 2);
    if (postProcessingImageInfos == VANILLA_MAP_FAILED) {
        VANILLA_munmap(pipelineImageInfos, sizeof(VkDescriptorImageInfo) * vulkan.pipelineCount);
        return;
    }

    for (uint16_t i = 0; i < vulkan.pipelineCount; i++) {
        pipelineImageInfos[i].sampler = vulkan.defaultSampler;
        pipelineImageInfos[i].imageView = vulkan.pipelineImageViews[i];
        pipelineImageInfos[i].imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    }
    for (uint32_t i = 0; i < 2; i++) {
        postProcessingImageInfos[i].sampler = vulkan.defaultSampler;
        postProcessingImageInfos[i].imageView = vulkan.postProcessingImageViews[i];
        postProcessingImageInfos[i].imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    }

    VkWriteDescriptorSet writes[2] = {0};

    writes[0].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    writes[0].dstSet = vulkan.postProcessingDescriptorSet;
    writes[0].dstBinding = 0;
    writes[0].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    writes[0].descriptorCount = vulkan.pipelineCount;
    writes[0].pImageInfo = pipelineImageInfos;

    writes[1].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    writes[1].dstSet = vulkan.postProcessingDescriptorSet;
    writes[1].dstBinding = 1;
    writes[1].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    writes[1].descriptorCount = 2;
    writes[1].pImageInfo = postProcessingImageInfos;

    vkUpdateDescriptorSets(
        vulkan.device,
        2,
        writes,
        0,
        NULL
    );

    VANILLA_munmap(pipelineImageInfos, sizeof(VkDescriptorImageInfo) * vulkan.pipelineCount);
    VANILLA_munmap(postProcessingImageInfos, sizeof(VkDescriptorImageInfo) * 2);
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
            case VANILLA_VERTEXFORMAT_R8G8B8A8_UNORM:
                format = VK_FORMAT_R8G8B8A8_UNORM;
                break;
            case VANILLA_VERTEXFORMAT_R32G32B32A32_UINT:
                format = VK_FORMAT_R32G32B32A32_UINT;
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

void VANILLA_CleanupDescriptor(void) {
    if (vulkan.descriptorPool != VK_NULL_HANDLE) {
        vkDestroyDescriptorPool(
            vulkan.device,
            vulkan.descriptorPool,
            NULL
        );

        vulkan.descriptorPool = VK_NULL_HANDLE;
        vulkan.descriptorSet = VK_NULL_HANDLE;
    }
    if (vulkan.descriptorSetLayout != VK_NULL_HANDLE) {
        vkDestroyDescriptorSetLayout(vulkan.device, vulkan.descriptorSetLayout, NULL);
        vulkan.descriptorSetLayout = VK_NULL_HANDLE;
    }

    if (vulkan.textureInfos != VANILLA_MAP_FAILED) { VANILLA_munmap(vulkan.textureInfos, sizeof(VkDescriptorImageInfo) * textureLimit); }
    if (vulkan.samplerInfos != VANILLA_MAP_FAILED) { VANILLA_munmap(vulkan.samplerInfos, sizeof(VkDescriptorImageInfo) * samplerLimit); }
}

void VANILLA_CleanupPostProcessingDescriptor(void) {
    if (vulkan.postProcessingDescriptorPool != VK_NULL_HANDLE) {
        vkDestroyDescriptorPool(
            vulkan.device,
            vulkan.postProcessingDescriptorPool,
            NULL
        );
        vulkan.postProcessingDescriptorPool = VK_NULL_HANDLE;
        vulkan.postProcessingDescriptorSet = VK_NULL_HANDLE;
    }
    if (vulkan.postProcessingDescriptorSetLayout != VK_NULL_HANDLE) {
        vkDestroyDescriptorSetLayout(
            vulkan.device,
            vulkan.postProcessingDescriptorSetLayout,
            NULL
        );
        vulkan.postProcessingDescriptorSetLayout = VK_NULL_HANDLE;
    }
}