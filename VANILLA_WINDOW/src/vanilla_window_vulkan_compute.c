/*
 * VANILLA Modules
 * Copyright (C) 2026 Samed
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License, version 2 only.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 *
 * See the LICENSE file for the full license text.
 */

#include "vanilla_window_vulkan_utils.h"

static VANILLA_ComputeShaderInfo* computeShaderInfos = NULL;
static VANILLA_UserBufferInfo* userBufferInfos = NULL;

static uint32_t* computeGroupSizes = NULL;
static uint32_t  maxComputeGroupCountX = 0;

#define VANILLA_USER_BUFFER_WRITE_LIMIT 256

static VkBuffer       stagingUserBuffers[VANILLA_USER_BUFFER_LIMIT] = {0};
static VkDeviceMemory stagingUserBufferMemories[VANILLA_USER_BUFFER_LIMIT] = {0};
static void*          mappedStagingUserBufferPtrs[VANILLA_USER_BUFFER_LIMIT] = {0};

static void* mappedUserBufferPtrs[VANILLA_USER_BUFFER_LIMIT] = {0};

typedef struct {
    uint32_t index;
    uint32_t offset;
    uint32_t size;
    const void* data;
} VANILLA_UserBufferWrite;

static VANILLA_UserBufferWrite pendingUserBufferWrites[VANILLA_USER_BUFFER_WRITE_LIMIT] = {0};
static uint32_t pendingUserBufferWriteCount = 0;

VANILLA_EXPORT VANILLA_WINDOW_RESULT VANILLA_configureCompute(const VANILLA_ComputeShaderInfo* shaderInfos, uint16_t shaderCount, const VANILLA_UserBufferInfo* bufferInfos, uint16_t bufferCount) {
    if (didstart == true) { return VANILLA_WINDOW_ERR_ALREADY_STARTED; }
    if (computeShaderInfos != NULL || userBufferInfos != NULL) { return VANILLA_WINDOW_SUCCESS; }
    if (shaderCount == 0 || shaderInfos == NULL) { return VANILLA_WINDOW_ERR_NULL_POINTER; }
    if (bufferCount > 0 && bufferInfos == NULL)  { return VANILLA_WINDOW_ERR_NULL_POINTER; }
    if (bufferCount > VANILLA_USER_BUFFER_LIMIT) { return VANILLA_WINDOW_ERR_CAPACITY_REACHED; }

    for (uint16_t i = 0; i < bufferCount; i++) {
        if (bufferInfos[i].size == 0) { return VANILLA_WINDOW_ERR_INVALID_USER_BUFFER; }
    }

    for (uint16_t i = 0; i < shaderCount; i++) {
        const uint32_t groupSize = shaderInfos[i].groupSize;
        if (groupSize == 0 || (groupSize & (groupSize - 1)) != 0) { return VANILLA_WINDOW_ERR_INVALID_COMPUTE_GROUP_SIZE; }
    }

    computeShaderInfos = VANILLA_mmap(sizeof(VANILLA_ComputeShaderInfo) * shaderCount);
    if (computeShaderInfos == VANILLA_MAP_FAILED) {
        computeShaderInfos = NULL;
        return VANILLA_WINDOW_ERR_ALLOC_FAILED;
    }

    if (bufferCount > 0) {
        userBufferInfos = VANILLA_mmap(sizeof(VANILLA_UserBufferInfo) * bufferCount);
        if (userBufferInfos == VANILLA_MAP_FAILED) {
            userBufferInfos = NULL;
            VANILLA_munmap(computeShaderInfos, sizeof(VANILLA_ComputeShaderInfo) * shaderCount);
            computeShaderInfos = NULL;
            return VANILLA_WINDOW_ERR_ALLOC_FAILED;
        }
    }

    vulkan.computePipelines = VANILLA_mmap( sizeof(VkPipeline) * shaderCount);
    if (vulkan.computePipelines == VANILLA_MAP_FAILED) {
        vulkan.computePipelines = NULL;

        if (userBufferInfos != NULL) {
            VANILLA_munmap(userBufferInfos, sizeof(VANILLA_UserBufferInfo) * bufferCount);
            userBufferInfos = NULL;
        }

        VANILLA_munmap(computeShaderInfos, sizeof(VANILLA_ComputeShaderInfo) * shaderCount);
        computeShaderInfos = NULL;

        return VANILLA_WINDOW_ERR_ALLOC_FAILED;
    }

    memcpy(computeShaderInfos, shaderInfos, sizeof(VANILLA_ComputeShaderInfo) * shaderCount);
    if (bufferCount > 0) {
        memcpy(userBufferInfos, bufferInfos, sizeof(VANILLA_UserBufferInfo) * bufferCount);
    }

    vulkan.computePipelineCount = shaderCount;
    vulkan.userBufferCount = bufferCount;

    return VANILLA_WINDOW_SUCCESS;
}

int VANILLA_VK_createComputeDescriptorSetLayout(void) {
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
    bindings[7].stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;

    /* Lighting */
    bindings[VANILLA_BINDING_LIGHTS].binding = VANILLA_BINDING_LIGHTS;
    bindings[VANILLA_BINDING_LIGHTS].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    bindings[VANILLA_BINDING_LIGHTS].descriptorCount = 1;
    bindings[VANILLA_BINDING_LIGHTS].stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;

    bindings[VANILLA_BINDING_LIGHTING_INFO].binding = VANILLA_BINDING_LIGHTING_INFO;
    bindings[VANILLA_BINDING_LIGHTING_INFO].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    bindings[VANILLA_BINDING_LIGHTING_INFO].descriptorCount = 1;
    bindings[VANILLA_BINDING_LIGHTING_INFO].stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;

    bindings[VANILLA_BINDING_LIGHT_VOLUME].binding = VANILLA_BINDING_LIGHT_VOLUME;
    bindings[VANILLA_BINDING_LIGHT_VOLUME].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    bindings[VANILLA_BINDING_LIGHT_VOLUME].descriptorCount = 3;
    bindings[VANILLA_BINDING_LIGHT_VOLUME].stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;

    bindings[VANILLA_BINDING_SHADOW_MAP].binding = VANILLA_BINDING_SHADOW_MAP;
    bindings[VANILLA_BINDING_SHADOW_MAP].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    bindings[VANILLA_BINDING_SHADOW_MAP].descriptorCount = 1;
    bindings[VANILLA_BINDING_SHADOW_MAP].stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;

    bindings[VANILLA_BINDING_SHADOW_DEPTH].binding = VANILLA_BINDING_SHADOW_DEPTH;
    bindings[VANILLA_BINDING_SHADOW_DEPTH].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    bindings[VANILLA_BINDING_SHADOW_DEPTH].descriptorCount = 1;
    bindings[VANILLA_BINDING_SHADOW_DEPTH].stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;

    bindings[VANILLA_BINDING_LOCAL_SHADOW_MAP].binding = VANILLA_BINDING_LOCAL_SHADOW_MAP;
    bindings[VANILLA_BINDING_LOCAL_SHADOW_MAP].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    bindings[VANILLA_BINDING_LOCAL_SHADOW_MAP].descriptorCount = 1;
    bindings[VANILLA_BINDING_LOCAL_SHADOW_MAP].stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;

    bindings[VANILLA_BINDING_LOCAL_SHADOW_DEPTH].binding = VANILLA_BINDING_LOCAL_SHADOW_DEPTH;
    bindings[VANILLA_BINDING_LOCAL_SHADOW_DEPTH].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    bindings[VANILLA_BINDING_LOCAL_SHADOW_DEPTH].descriptorCount = 1;
    bindings[VANILLA_BINDING_LOCAL_SHADOW_DEPTH].stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;

    bindings[VANILLA_BINDING_ENV_SPECULAR].binding = VANILLA_BINDING_ENV_SPECULAR;
    bindings[VANILLA_BINDING_ENV_SPECULAR].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    bindings[VANILLA_BINDING_ENV_SPECULAR].descriptorCount = 1;
    bindings[VANILLA_BINDING_ENV_SPECULAR].stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;

    bindings[VANILLA_BINDING_BRDF_LUT].binding = VANILLA_BINDING_BRDF_LUT;
    bindings[VANILLA_BINDING_BRDF_LUT].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    bindings[VANILLA_BINDING_BRDF_LUT].descriptorCount = 1;
    bindings[VANILLA_BINDING_BRDF_LUT].stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;

    bindings[VANILLA_BINDING_AERIAL].binding = VANILLA_BINDING_AERIAL;
    bindings[VANILLA_BINDING_AERIAL].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    bindings[VANILLA_BINDING_AERIAL].descriptorCount = 1;
    bindings[VANILLA_BINDING_AERIAL].stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;

    for (uint32_t i = 0; i < vulkan.userBufferCount; i++) {
        bindings[VANILLA_BINDING_USER_BUFFERS + i].binding = VANILLA_BINDING_USER_BUFFERS + i;
        bindings[VANILLA_BINDING_USER_BUFFERS + i].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
        bindings[VANILLA_BINDING_USER_BUFFERS + i].descriptorCount = 1;
        bindings[VANILLA_BINDING_USER_BUFFERS + i].stageFlags = VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_COMPUTE_BIT;
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

    vulkan.computeDescriptorSetLayout = layout;
    return 0;
}

int VANILLA_VK_createComputeDescriptor(void) {
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
        &vulkan.computeDescriptorPool) != VK_SUCCESS) {
        return 1;
    }

    VkDescriptorSetAllocateInfo allocInfo = {0};
    allocInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
    allocInfo.descriptorPool = vulkan.computeDescriptorPool;
    allocInfo.descriptorSetCount = 1;
    allocInfo.pSetLayouts = &vulkan.computeDescriptorSetLayout;

    if (vkAllocateDescriptorSets(
        vulkan.device,
        &allocInfo,
        &vulkan.computeDescriptorSet) != VK_SUCCESS) {
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

    VkDescriptorBufferInfo userBufferInfos[VANILLA_USER_BUFFER_LIMIT] = {0};
    for (uint32_t i = 0; i < vulkan.userBufferCount; i++) {
        userBufferInfos[i] = (VkDescriptorBufferInfo){
            .buffer = vulkan.userBuffers[i],
            .offset = 0,
            .range = VK_WHOLE_SIZE
        };
    }

    VkWriteDescriptorSet writes[VANILLA_BINDING_USER_BUFFERS + VANILLA_USER_BUFFER_LIMIT] = {0};

    writes[0].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    writes[0].dstSet = vulkan.computeDescriptorSet;
    writes[0].dstBinding = 0;
    writes[0].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    writes[0].descriptorCount = 1;
    writes[0].pBufferInfo = &transformInfo;

    writes[1].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    writes[1].dstSet = vulkan.computeDescriptorSet;
    writes[1].dstBinding = 1;
    writes[1].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    writes[1].descriptorCount = 1;
    writes[1].pBufferInfo = &lookupInfo;

    writes[2].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    writes[2].dstSet = vulkan.computeDescriptorSet;
    writes[2].dstBinding = 2;
    writes[2].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    writes[2].descriptorCount = 1;
    writes[2].pBufferInfo = &vertexInfo;

    writes[3].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    writes[3].dstSet = vulkan.computeDescriptorSet;
    writes[3].dstBinding = 3;
    writes[3].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    writes[3].descriptorCount = 1;
    writes[3].pBufferInfo = &materialSlotInfo;

    writes[4].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    writes[4].dstSet = vulkan.computeDescriptorSet;
    writes[4].dstBinding = 4;
    writes[4].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    writes[4].descriptorCount = 1;
    writes[4].pBufferInfo = &materialInfo;

    writes[5].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    writes[5].dstSet = vulkan.computeDescriptorSet;
    writes[5].dstBinding = 5;
    writes[5].descriptorType = VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE;
    writes[5].descriptorCount = textureLimit;
    writes[5].pImageInfo = vulkan.textureInfos;

    writes[6].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    writes[6].dstSet = vulkan.computeDescriptorSet;
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
    writes[7].dstSet = vulkan.computeDescriptorSet;
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
    writes[VANILLA_BINDING_LIGHTS].dstSet = vulkan.computeDescriptorSet;
    writes[VANILLA_BINDING_LIGHTS].dstBinding = VANILLA_BINDING_LIGHTS;
    writes[VANILLA_BINDING_LIGHTS].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    writes[VANILLA_BINDING_LIGHTS].descriptorCount = 1;
    writes[VANILLA_BINDING_LIGHTS].pBufferInfo = &lightsInfo;

    writes[VANILLA_BINDING_LIGHTING_INFO].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    writes[VANILLA_BINDING_LIGHTING_INFO].dstSet = vulkan.computeDescriptorSet;
    writes[VANILLA_BINDING_LIGHTING_INFO].dstBinding = VANILLA_BINDING_LIGHTING_INFO;
    writes[VANILLA_BINDING_LIGHTING_INFO].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    writes[VANILLA_BINDING_LIGHTING_INFO].descriptorCount = 1;
    writes[VANILLA_BINDING_LIGHTING_INFO].pBufferInfo = &lightingInfoInfo;

    writes[VANILLA_BINDING_LIGHT_VOLUME].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    writes[VANILLA_BINDING_LIGHT_VOLUME].dstSet = vulkan.computeDescriptorSet;
    writes[VANILLA_BINDING_LIGHT_VOLUME].dstBinding = VANILLA_BINDING_LIGHT_VOLUME;
    writes[VANILLA_BINDING_LIGHT_VOLUME].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    writes[VANILLA_BINDING_LIGHT_VOLUME].descriptorCount = 3;
    writes[VANILLA_BINDING_LIGHT_VOLUME].pImageInfo = lightVolumeInfos;

    VkDescriptorImageInfo shadowCompareInfo;
    VkDescriptorImageInfo shadowDepthInfo;
    VANILLA_VK_getShadowImageInfos(&shadowCompareInfo, &shadowDepthInfo);

    writes[VANILLA_BINDING_SHADOW_MAP].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    writes[VANILLA_BINDING_SHADOW_MAP].dstSet = vulkan.computeDescriptorSet;
    writes[VANILLA_BINDING_SHADOW_MAP].dstBinding = VANILLA_BINDING_SHADOW_MAP;
    writes[VANILLA_BINDING_SHADOW_MAP].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    writes[VANILLA_BINDING_SHADOW_MAP].descriptorCount = 1;
    writes[VANILLA_BINDING_SHADOW_MAP].pImageInfo = &shadowCompareInfo;

    writes[VANILLA_BINDING_SHADOW_DEPTH].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    writes[VANILLA_BINDING_SHADOW_DEPTH].dstSet = vulkan.computeDescriptorSet;
    writes[VANILLA_BINDING_SHADOW_DEPTH].dstBinding = VANILLA_BINDING_SHADOW_DEPTH;
    writes[VANILLA_BINDING_SHADOW_DEPTH].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    writes[VANILLA_BINDING_SHADOW_DEPTH].descriptorCount = 1;
    writes[VANILLA_BINDING_SHADOW_DEPTH].pImageInfo = &shadowDepthInfo;

    VkDescriptorImageInfo localCompareInfo;
    VkDescriptorImageInfo localDepthInfo;
    VANILLA_VK_getLocalShadowImageInfos(&localCompareInfo, &localDepthInfo);

    writes[VANILLA_BINDING_LOCAL_SHADOW_MAP].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    writes[VANILLA_BINDING_LOCAL_SHADOW_MAP].dstSet = vulkan.computeDescriptorSet;
    writes[VANILLA_BINDING_LOCAL_SHADOW_MAP].dstBinding = VANILLA_BINDING_LOCAL_SHADOW_MAP;
    writes[VANILLA_BINDING_LOCAL_SHADOW_MAP].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    writes[VANILLA_BINDING_LOCAL_SHADOW_MAP].descriptorCount = 1;
    writes[VANILLA_BINDING_LOCAL_SHADOW_MAP].pImageInfo = &localCompareInfo;

    writes[VANILLA_BINDING_LOCAL_SHADOW_DEPTH].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    writes[VANILLA_BINDING_LOCAL_SHADOW_DEPTH].dstSet = vulkan.computeDescriptorSet;
    writes[VANILLA_BINDING_LOCAL_SHADOW_DEPTH].dstBinding = VANILLA_BINDING_LOCAL_SHADOW_DEPTH;
    writes[VANILLA_BINDING_LOCAL_SHADOW_DEPTH].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    writes[VANILLA_BINDING_LOCAL_SHADOW_DEPTH].descriptorCount = 1;
    writes[VANILLA_BINDING_LOCAL_SHADOW_DEPTH].pImageInfo = &localDepthInfo;

    VkDescriptorImageInfo envSpecularInfo;
    VkDescriptorImageInfo brdfLutInfo;
    VkDescriptorImageInfo aerialInfo;
    VANILLA_VK_getEnvironmentImageInfos(&envSpecularInfo, &brdfLutInfo, &aerialInfo);

    writes[VANILLA_BINDING_ENV_SPECULAR].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    writes[VANILLA_BINDING_ENV_SPECULAR].dstSet = vulkan.computeDescriptorSet;
    writes[VANILLA_BINDING_ENV_SPECULAR].dstBinding = VANILLA_BINDING_ENV_SPECULAR;
    writes[VANILLA_BINDING_ENV_SPECULAR].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    writes[VANILLA_BINDING_ENV_SPECULAR].descriptorCount = 1;
    writes[VANILLA_BINDING_ENV_SPECULAR].pImageInfo = &envSpecularInfo;

    writes[VANILLA_BINDING_BRDF_LUT].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    writes[VANILLA_BINDING_BRDF_LUT].dstSet = vulkan.computeDescriptorSet;
    writes[VANILLA_BINDING_BRDF_LUT].dstBinding = VANILLA_BINDING_BRDF_LUT;
    writes[VANILLA_BINDING_BRDF_LUT].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    writes[VANILLA_BINDING_BRDF_LUT].descriptorCount = 1;
    writes[VANILLA_BINDING_BRDF_LUT].pImageInfo = &brdfLutInfo;

    writes[VANILLA_BINDING_AERIAL].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    writes[VANILLA_BINDING_AERIAL].dstSet = vulkan.computeDescriptorSet;
    writes[VANILLA_BINDING_AERIAL].dstBinding = VANILLA_BINDING_AERIAL;
    writes[VANILLA_BINDING_AERIAL].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    writes[VANILLA_BINDING_AERIAL].descriptorCount = 1;
    writes[VANILLA_BINDING_AERIAL].pImageInfo = &aerialInfo;

    for (uint32_t i = 0; i < vulkan.userBufferCount; i++) {
        writes[VANILLA_BINDING_USER_BUFFERS + i].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        writes[VANILLA_BINDING_USER_BUFFERS + i].dstSet = vulkan.computeDescriptorSet;
        writes[VANILLA_BINDING_USER_BUFFERS + i].dstBinding = VANILLA_BINDING_USER_BUFFERS + i;
        writes[VANILLA_BINDING_USER_BUFFERS + i].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
        writes[VANILLA_BINDING_USER_BUFFERS + i].descriptorCount = 1;
        writes[VANILLA_BINDING_USER_BUFFERS + i].pBufferInfo = &userBufferInfos[i];
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

int VANILLA_VK_createComputePipelineLayout(void) {
    VkPipelineLayoutCreateInfo layoutInfo = {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO,
        .setLayoutCount = 1,
        .pSetLayouts = &vulkan.computeDescriptorSetLayout
    };

    if (vkCreatePipelineLayout(
        vulkan.device,
        &layoutInfo,
        NULL,
        &vulkan.computePipelineLayout) != VK_SUCCESS) {
        return 1;
    }
    return 0;
}

static void VANILLA_freeComputeShaderInfos(void) {
    if (computeShaderInfos != NULL) {
        VANILLA_munmap(computeShaderInfos, sizeof(VANILLA_ComputeShaderInfo) * vulkan.computePipelineCount);
        computeShaderInfos = NULL;
    }
}

int VANILLA_VK_createComputePipelines(void) {
    uint16_t createdCount = 0;

    VkPhysicalDeviceProperties properties;
    vkGetPhysicalDeviceProperties(vulkan.physicalDevice, &properties);

    maxComputeGroupCountX = properties.limits.maxComputeWorkGroupCount[0];

    uint32_t maxGroupSize = properties.limits.maxComputeWorkGroupSize[0];
    if (maxGroupSize > properties.limits.maxComputeWorkGroupInvocations) {
        maxGroupSize = properties.limits.maxComputeWorkGroupInvocations;
    }

    computeGroupSizes = VANILLA_mmap(sizeof(uint32_t) * vulkan.computePipelineCount);
    if (computeGroupSizes == VANILLA_MAP_FAILED) {
        computeGroupSizes = NULL;
        VANILLA_freeComputeShaderInfos();
        return 1;
    }

    for (uint16_t i = 0; i < vulkan.computePipelineCount; i++) {
        if (computeShaderInfos[i].groupSize > maxGroupSize || computeShaderInfos[i].groupSize > CHUNKSIZE) {
            printf("[VANILLA WINDOW] compute shader %u group size %u exceeds device limit %u\n",
                i, computeShaderInfos[i].groupSize, maxGroupSize);
            VANILLA_freeComputeShaderInfos();
            return 1;
        }

        computeGroupSizes[i] = computeShaderInfos[i].groupSize;
    }

    for (uint16_t i = 0; i < vulkan.computePipelineCount; i++) {
        VkShaderModule shaderModule = VK_NULL_HANDLE;

        VkShaderModuleCreateInfo shaderInfo = {
            .sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO,
            .codeSize = computeShaderInfos[i].computeShaderSize,
            .pCode = computeShaderInfos[i].computeShader
        };

        if (vkCreateShaderModule(
            vulkan.device,
            &shaderInfo,
            NULL,
            &shaderModule) != VK_SUCCESS) {
            goto fail;
        }

        VkPipelineShaderStageCreateInfo stageInfo = {
            .sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
            .stage = VK_SHADER_STAGE_COMPUTE_BIT,
            .module = shaderModule,
            .pName = "main"
        };

        VkComputePipelineCreateInfo pipelineInfo = {
            .sType = VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO,
            .flags = VK_PIPELINE_CREATE_DISPATCH_BASE_BIT,
            .stage = stageInfo,
            .layout = vulkan.computePipelineLayout,
            .basePipelineIndex = -1
        };

        VkResult result = vkCreateComputePipelines(
            vulkan.device,
            VK_NULL_HANDLE,
            1,
            &pipelineInfo,
            NULL,
            &vulkan.computePipelines[i]
        );

        vkDestroyShaderModule(
            vulkan.device,
            shaderModule,
            NULL
        );

        if (result != VK_SUCCESS) {
            goto fail;
        }

        createdCount++;
    }

    VANILLA_freeComputeShaderInfos();
    return 0;

fail:
    for (uint16_t i = 0; i < createdCount; i++) {
        vkDestroyPipeline(
            vulkan.device,
            vulkan.computePipelines[i],
            NULL
        );

        vulkan.computePipelines[i] = VK_NULL_HANDLE;
    }

    VANILLA_freeComputeShaderInfos();
    return 1;
}

void VANILLA_VK_destroyComputeDescriptor(void) {
    if (vulkan.computeDescriptorPool != VK_NULL_HANDLE) {
        vkDestroyDescriptorPool(
            vulkan.device,
            vulkan.computeDescriptorPool,
            NULL
        );

        vulkan.computeDescriptorPool = VK_NULL_HANDLE;
        vulkan.computeDescriptorSet = VK_NULL_HANDLE;
    }

    if (vulkan.computeDescriptorSetLayout != VK_NULL_HANDLE) {
        vkDestroyDescriptorSetLayout(
            vulkan.device,
            vulkan.computeDescriptorSetLayout,
            NULL
        );

        vulkan.computeDescriptorSetLayout = VK_NULL_HANDLE;
    }
}

void VANILLA_VK_destroyComputePipelines(void) {
    if (vulkan.computePipelines != NULL) {
        for (uint16_t i = 0; i < vulkan.computePipelineCount; i++) {
            if (vulkan.computePipelines[i] != VK_NULL_HANDLE) {
                vkDestroyPipeline(
                    vulkan.device,
                    vulkan.computePipelines[i],
                    NULL
                );

                vulkan.computePipelines[i] = VK_NULL_HANDLE;
            }
        }

        VANILLA_munmap(vulkan.computePipelines, sizeof(VkPipeline) * vulkan.computePipelineCount);
        vulkan.computePipelines = NULL;
    }

    VANILLA_freeComputeShaderInfos();

    if (computeGroupSizes != NULL) {
        VANILLA_munmap(computeGroupSizes, sizeof(uint32_t) * vulkan.computePipelineCount);
        computeGroupSizes = NULL;
    }

    if (vulkan.computePipelineLayout != VK_NULL_HANDLE) {
        vkDestroyPipelineLayout(
            vulkan.device,
            vulkan.computePipelineLayout,
            NULL
        );

        vulkan.computePipelineLayout = VK_NULL_HANDLE;
    }
}

///

static int VANILLA_VK_createUserStagingBuffer(uint16_t index) {
    const VkDeviceSize size = userBufferInfos[index].size;

    VkBufferCreateInfo createInfo = {
        .sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO,
        .size = size,
        .usage = VK_BUFFER_USAGE_TRANSFER_SRC_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT,
        .sharingMode = VK_SHARING_MODE_EXCLUSIVE
    };

    if (vkCreateBuffer(vulkan.device, &createInfo, NULL, &stagingUserBuffers[index]) != VK_SUCCESS) {
        stagingUserBuffers[index] = VK_NULL_HANDLE;
        return 1;
    }

    VkMemoryRequirements requirements;
    vkGetBufferMemoryRequirements(vulkan.device, stagingUserBuffers[index], &requirements);

    stagingUserBufferMemories[index] = VANILLA_gmmap(&requirements,
        VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT | VK_MEMORY_PROPERTY_HOST_CACHED_BIT);

    if (stagingUserBufferMemories[index] == VK_NULL_HANDLE) {
        vkDestroyBuffer(vulkan.device, stagingUserBuffers[index], NULL);

        stagingUserBuffers[index] = VK_NULL_HANDLE;

        return 1;
    }

    if (vkBindBufferMemory(vulkan.device, stagingUserBuffers[index], stagingUserBufferMemories[index], 0) != VK_SUCCESS) {
        VANILLA_gmunmap(stagingUserBufferMemories[index]);

        vkDestroyBuffer(vulkan.device, stagingUserBuffers[index], NULL);

        stagingUserBufferMemories[index] = VK_NULL_HANDLE;
        stagingUserBuffers[index] = VK_NULL_HANDLE;

        return 1;
    }

    if (vkMapMemory(vulkan.device, stagingUserBufferMemories[index], 0, size, 0, &mappedStagingUserBufferPtrs[index]) != VK_SUCCESS) {
        VANILLA_gmunmap(stagingUserBufferMemories[index]);

        vkDestroyBuffer(vulkan.device, stagingUserBuffers[index], NULL);

        mappedStagingUserBufferPtrs[index] = NULL;
        stagingUserBufferMemories[index] = VK_NULL_HANDLE;
        stagingUserBuffers[index] = VK_NULL_HANDLE;

        return 1;
    }

    return 0;
}

static void VANILLA_VK_destroyUserStagingBuffer(uint16_t index) {
    if (mappedStagingUserBufferPtrs[index] != NULL) {
        vkUnmapMemory(vulkan.device, stagingUserBufferMemories[index]);
        mappedStagingUserBufferPtrs[index] = NULL;
    }

    if (stagingUserBuffers[index] != VK_NULL_HANDLE) {
        vkDestroyBuffer(vulkan.device, stagingUserBuffers[index], NULL);
        stagingUserBuffers[index] = VK_NULL_HANDLE;
    }

    if (stagingUserBufferMemories[index] != VK_NULL_HANDLE) {
        VANILLA_gmunmap(stagingUserBufferMemories[index]);
        stagingUserBufferMemories[index] = VK_NULL_HANDLE;
    }
}

int VANILLA_VK_createComputeBuffers(void) {
    if (vulkan.userBufferCount == 0) { return 0; }

    vulkan.userBuffers = VANILLA_mmap(
        sizeof(VkBuffer) * vulkan.userBufferCount
    );

    if (vulkan.userBuffers == VANILLA_MAP_FAILED) {
        vulkan.userBuffers = NULL;
        return 1;
    }

    vulkan.userBufferMemories = VANILLA_mmap(
        sizeof(VkDeviceMemory) * vulkan.userBufferCount
    );

    if (vulkan.userBufferMemories == VANILLA_MAP_FAILED) {
        vulkan.userBufferMemories = NULL;

        VANILLA_munmap(
            vulkan.userBuffers,
            sizeof(VkBuffer) * vulkan.userBufferCount
        );

        vulkan.userBuffers = NULL;
        return 1;
    }

    bool hasInitialUpload = false;

    for (uint16_t i = 0; i < vulkan.userBufferCount; i++) {
        const VANILLA_UserBufferInfo* info = &userBufferInfos[i];
        const bool deviceLocal = (info->memoryMode == VANILLA_USER_BUFFER_DEVICE_LOCAL);

        VkBufferUsageFlags usage = VK_BUFFER_USAGE_STORAGE_BUFFER_BIT;
        VkMemoryPropertyFlags memoryProperties;

        if (deviceLocal) {
            usage |= VK_BUFFER_USAGE_TRANSFER_DST_BIT;
            if (info->runtimeAccess) { usage |= VK_BUFFER_USAGE_TRANSFER_SRC_BIT; }

            memoryProperties = VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT;
        } else {
            memoryProperties =
                VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT |
                VK_MEMORY_PROPERTY_HOST_COHERENT_BIT;

            if (info->runtimeAccess) { memoryProperties |= VK_MEMORY_PROPERTY_HOST_CACHED_BIT; }
        }

        VkBufferCreateInfo bufferInfo = {
            .sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO,
            .size = info->size,
            .usage = usage,
            .sharingMode = VK_SHARING_MODE_EXCLUSIVE
        };

        if (vkCreateBuffer(
            vulkan.device,
            &bufferInfo,
            NULL,
            &vulkan.userBuffers[i]) != VK_SUCCESS) {
            vulkan.userBuffers[i] = VK_NULL_HANDLE;
            goto fail;
        }

        VkMemoryRequirements requirements;
        vkGetBufferMemoryRequirements(
            vulkan.device,
            vulkan.userBuffers[i],
            &requirements
        );

        vulkan.userBufferMemories[i] = VANILLA_gmmap(
            &requirements,
            memoryProperties
        );

        if (vulkan.userBufferMemories[i] == VK_NULL_HANDLE) {
            goto fail;
        }

        if (vkBindBufferMemory(
            vulkan.device,
            vulkan.userBuffers[i],
            vulkan.userBufferMemories[i],
            0) != VK_SUCCESS) {
            goto fail;
        }

        if (deviceLocal) {
            if (info->initialData == NULL && info->runtimeAccess == false) { continue; }

            if (VANILLA_VK_createUserStagingBuffer(i)) { goto fail; }

            if (info->initialData != NULL) {
                memcpy(mappedStagingUserBufferPtrs[i], info->initialData, info->size);
                hasInitialUpload = true;
            }

            continue;
        }

        if (info->initialData == NULL && info->runtimeAccess == false) { continue; }

        void* mappedMemory = NULL;

        if (vkMapMemory(
            vulkan.device,
            vulkan.userBufferMemories[i],
            0,
            info->size,
            0,
            &mappedMemory) != VK_SUCCESS) {
            goto fail;
        }

        if (info->initialData != NULL) {
            memcpy(mappedMemory, info->initialData, info->size);
        }

        if (info->runtimeAccess) {
            mappedUserBufferPtrs[i] = mappedMemory;
        } else {
            vkUnmapMemory(vulkan.device, vulkan.userBufferMemories[i]);
        }
    }

    if (hasInitialUpload) {
        VkCommandBuffer commandBuffer = VANILLA_VK_beginSingleTimeCommands();
        if (commandBuffer == VK_NULL_HANDLE) { goto fail; }

        for (uint16_t i = 0; i < vulkan.userBufferCount; i++) {
            if (userBufferInfos[i].memoryMode != VANILLA_USER_BUFFER_DEVICE_LOCAL) { continue; }
            if (userBufferInfos[i].initialData == NULL) { continue; }

            VkBufferCopy copy = {
                .srcOffset = 0,
                .dstOffset = 0,
                .size = userBufferInfos[i].size
            };

            vkCmdCopyBuffer(commandBuffer, stagingUserBuffers[i], vulkan.userBuffers[i], 1, &copy);
        }

        if (VANILLA_VK_endSingleTimeCommands(commandBuffer)) { goto fail; }
    }

    for (uint16_t i = 0; i < vulkan.userBufferCount; i++) {
        if (userBufferInfos[i].runtimeAccess == false) {
            VANILLA_VK_destroyUserStagingBuffer(i);
        }
    }

    return 0;

fail:
    VANILLA_VK_destroyComputeBuffers();
    return 1;
}

void VANILLA_VK_destroyComputeBuffers(void) {
    pendingUserBufferWriteCount = 0;

    for (uint16_t i = 0; i < vulkan.userBufferCount; i++) {
        VANILLA_VK_destroyUserStagingBuffer(i);

        if (mappedUserBufferPtrs[i] != NULL) {
            vkUnmapMemory(vulkan.device, vulkan.userBufferMemories[i]);
            mappedUserBufferPtrs[i] = NULL;
        }
    }

    if (vulkan.userBuffers != NULL) {
        for (uint16_t i = 0; i < vulkan.userBufferCount; i++) {
            if (vulkan.userBuffers[i] != VK_NULL_HANDLE) {
                vkDestroyBuffer(vulkan.device, vulkan.userBuffers[i], NULL);
                vulkan.userBuffers[i] = VK_NULL_HANDLE;
            }
        }
    }

    if (vulkan.userBufferMemories != NULL) {
        for (uint16_t i = 0; i < vulkan.userBufferCount; i++) {
            if (vulkan.userBufferMemories[i] != VK_NULL_HANDLE) {
                VANILLA_gmunmap(vulkan.userBufferMemories[i]);
                vulkan.userBufferMemories[i] = VK_NULL_HANDLE;
            }
        }
    }

    if (vulkan.userBuffers != NULL) {
        VANILLA_munmap(vulkan.userBuffers, sizeof(VkBuffer) * vulkan.userBufferCount);
        vulkan.userBuffers = NULL;
    }

    if (vulkan.userBufferMemories != NULL) {
        VANILLA_munmap(vulkan.userBufferMemories, sizeof(VkDeviceMemory) * vulkan.userBufferCount);
        vulkan.userBufferMemories = NULL;
    }

    if (userBufferInfos != NULL) {
        VANILLA_munmap(userBufferInfos, sizeof(VANILLA_UserBufferInfo) * vulkan.userBufferCount);
        userBufferInfos = NULL;
    }
}

///

void VANILLA_VK_recordUserBufferWrites(VkCommandBuffer commandBuffer) {
    if (pendingUserBufferWriteCount == 0) { return; }

    bool touched[VANILLA_USER_BUFFER_LIMIT] = {0};

    for (uint32_t w = 0; w < pendingUserBufferWriteCount; w++) {
        const VANILLA_UserBufferWrite* write = &pendingUserBufferWrites[w];
        const uint32_t i = write->index;

        if (userBufferInfos[i].memoryMode == VANILLA_USER_BUFFER_DEVICE_LOCAL) {
            memcpy((uint8_t*)mappedStagingUserBufferPtrs[i] + write->offset, write->data, write->size);

            VkBufferCopy copy = {
                .srcOffset = write->offset,
                .dstOffset = write->offset,
                .size = write->size
            };

            vkCmdCopyBuffer(commandBuffer, stagingUserBuffers[i], vulkan.userBuffers[i], 1, &copy);
        } else {
            memcpy((uint8_t*)mappedUserBufferPtrs[i] + write->offset, write->data, write->size);
        }

        touched[i] = true;
    }

    pendingUserBufferWriteCount = 0;

    VkBufferMemoryBarrier2 barriers[VANILLA_USER_BUFFER_LIMIT];
    uint32_t barrierCount = 0;

    for (uint16_t i = 0; i < vulkan.userBufferCount; i++) {
        if (touched[i] == false) { continue; }

        const bool deviceLocal = (userBufferInfos[i].memoryMode == VANILLA_USER_BUFFER_DEVICE_LOCAL);

        barriers[barrierCount++] = (VkBufferMemoryBarrier2){
            .sType = VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER_2,
            .srcStageMask  = deviceLocal ? VK_PIPELINE_STAGE_2_TRANSFER_BIT    : VK_PIPELINE_STAGE_2_HOST_BIT,
            .srcAccessMask = deviceLocal ? VK_ACCESS_2_TRANSFER_WRITE_BIT      : VK_ACCESS_2_HOST_WRITE_BIT,
            .dstStageMask  = VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT | VK_PIPELINE_STAGE_2_VERTEX_SHADER_BIT | VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT,
            .dstAccessMask = VK_ACCESS_2_SHADER_STORAGE_READ_BIT | VK_ACCESS_2_SHADER_STORAGE_WRITE_BIT,
            .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
            .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
            .buffer = vulkan.userBuffers[i],
            .offset = 0,
            .size = VK_WHOLE_SIZE
        };
    }

    VkDependencyInfo dependencyInfo = {
        .sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO,
        .bufferMemoryBarrierCount = barrierCount,
        .pBufferMemoryBarriers = barriers
    };

    vkCmdPipelineBarrier2(commandBuffer, &dependencyInfo);
}

static VANILLA_WINDOW_RESULT VANILLA_checkUserBufferAccess(uint32_t index, uint32_t offset, uint32_t size) {
    if (didstart == false) { return VANILLA_WINDOW_ERR_DIDNT_START; }
    if (index >= vulkan.userBufferCount) { return VANILLA_WINDOW_ERR_INVALID_USER_BUFFER; }
    if (userBufferInfos[index].runtimeAccess == false) { return VANILLA_WINDOW_ERR_USER_BUFFER_NO_ACCESS; }
    if (size == 0 || (uint64_t)offset + size > userBufferInfos[index].size) { return VANILLA_WINDOW_ERR_USER_BUFFER_RANGE; }

    return VANILLA_WINDOW_SUCCESS;
}

VANILLA_EXPORT VANILLA_WINDOW_RESULT VANILLA_writeUserBuffer(uint32_t index, uint32_t offset, const void* data, uint32_t size) {
    if (data == NULL) { return VANILLA_WINDOW_ERR_NULL_POINTER; }

    VANILLA_WINDOW_RESULT result = VANILLA_checkUserBufferAccess(index, offset, size);
    if (result != VANILLA_WINDOW_SUCCESS) { return result; }

    if (pendingUserBufferWriteCount >= VANILLA_USER_BUFFER_WRITE_LIMIT) { return VANILLA_WINDOW_ERR_CAPACITY_REACHED; }

    pendingUserBufferWrites[pendingUserBufferWriteCount++] = (VANILLA_UserBufferWrite){
        .index = index,
        .offset = offset,
        .size = size,
        .data = data
    };

    return VANILLA_WINDOW_SUCCESS;
}

VANILLA_EXPORT VANILLA_WINDOW_RESULT VANILLA_readUserBuffer(uint32_t index, uint32_t offset, void* out, uint32_t size) {
    if (out == NULL) { return VANILLA_WINDOW_ERR_NULL_POINTER; }

    VANILLA_WINDOW_RESULT result = VANILLA_checkUserBufferAccess(index, offset, size);
    if (result != VANILLA_WINDOW_SUCCESS) { return result; }

    if (vkDeviceWaitIdle(vulkan.device) != VK_SUCCESS) { return VANILLA_WINDOW_ERR_USER_BUFFER_TRANSFER; }

    if (userBufferInfos[index].memoryMode != VANILLA_USER_BUFFER_DEVICE_LOCAL) {
        memcpy(out, (const uint8_t*)mappedUserBufferPtrs[index] + offset, size);
        return VANILLA_WINDOW_SUCCESS;
    }

    VkCommandBuffer commandBuffer = VANILLA_VK_beginSingleTimeCommands();
    if (commandBuffer == VK_NULL_HANDLE) { return VANILLA_WINDOW_ERR_USER_BUFFER_TRANSFER; }

    VkMemoryBarrier2 toTransfer = {
        .sType = VK_STRUCTURE_TYPE_MEMORY_BARRIER_2,
        .srcStageMask = VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT,
        .srcAccessMask = VK_ACCESS_2_MEMORY_WRITE_BIT,
        .dstStageMask = VK_PIPELINE_STAGE_2_TRANSFER_BIT,
        .dstAccessMask = VK_ACCESS_2_TRANSFER_READ_BIT
    };

    VkDependencyInfo toTransferInfo = {
        .sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO,
        .memoryBarrierCount = 1,
        .pMemoryBarriers = &toTransfer
    };

    vkCmdPipelineBarrier2(commandBuffer, &toTransferInfo);

    VkBufferCopy copy = {
        .srcOffset = offset,
        .dstOffset = offset,
        .size = size
    };

    vkCmdCopyBuffer(commandBuffer, vulkan.userBuffers[index], stagingUserBuffers[index], 1, &copy);

    VkMemoryBarrier2 toHost = {
        .sType = VK_STRUCTURE_TYPE_MEMORY_BARRIER_2,
        .srcStageMask = VK_PIPELINE_STAGE_2_TRANSFER_BIT,
        .srcAccessMask = VK_ACCESS_2_TRANSFER_WRITE_BIT,
        .dstStageMask = VK_PIPELINE_STAGE_2_HOST_BIT,
        .dstAccessMask = VK_ACCESS_2_HOST_READ_BIT
    };

    VkDependencyInfo toHostInfo = {
        .sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO,
        .memoryBarrierCount = 1,
        .pMemoryBarriers = &toHost
    };

    vkCmdPipelineBarrier2(commandBuffer, &toHostInfo);

    if (VANILLA_VK_endSingleTimeCommands(commandBuffer)) { return VANILLA_WINDOW_ERR_USER_BUFFER_TRANSFER; }

    memcpy(out, (const uint8_t*)mappedStagingUserBufferPtrs[index] + offset, size);

    return VANILLA_WINDOW_SUCCESS;
}

///

static void VANILLA_VK_computeMemoryBarrier(VkCommandBuffer commandBuffer, VkPipelineStageFlags2 srcStage, VkAccessFlags2 srcAccess, VkPipelineStageFlags2 dstStage, VkAccessFlags2 dstAccess) {
    VkMemoryBarrier2 barrier = {
        .sType = VK_STRUCTURE_TYPE_MEMORY_BARRIER_2,
        .srcStageMask = srcStage,
        .srcAccessMask = srcAccess,
        .dstStageMask = dstStage,
        .dstAccessMask = dstAccess
    };

    VkDependencyInfo dependencyInfo = {
        .sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO,
        .memoryBarrierCount = 1,
        .pMemoryBarriers = &barrier
    };

    vkCmdPipelineBarrier2(commandBuffer, &dependencyInfo);
}

void VANILLA_VK_recordComputeDispatches(VkCommandBuffer commandBuffer) {
    if (vulkan.computePipelineCount == 0) { return; }
    if (vulkan.computeDescriptorSet == VK_NULL_HANDLE) { return; }

    const uint32_t chunkCount = stream->chunkCount;
    if (chunkCount == 0) { return; }

    VANILLA_VK_computeMemoryBarrier(
        commandBuffer,
        VK_PIPELINE_STAGE_2_TRANSFER_BIT | VK_PIPELINE_STAGE_2_HOST_BIT,
        VK_ACCESS_2_TRANSFER_WRITE_BIT | VK_ACCESS_2_HOST_WRITE_BIT,
        VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT,
        VK_ACCESS_2_SHADER_STORAGE_READ_BIT | VK_ACCESS_2_SHADER_STORAGE_WRITE_BIT
    );

    vkCmdBindDescriptorSets(
        commandBuffer,
        VK_PIPELINE_BIND_POINT_COMPUTE,
        vulkan.computePipelineLayout,
        0,
        1,
        &vulkan.computeDescriptorSet,
        0,
        NULL
    );

    for (uint16_t i = 0; i < vulkan.computePipelineCount; i++) {
        if (i > 0) {
            VANILLA_VK_computeMemoryBarrier(
                commandBuffer,
                VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT,
                VK_ACCESS_2_SHADER_STORAGE_WRITE_BIT,
                VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT,
                VK_ACCESS_2_SHADER_STORAGE_READ_BIT | VK_ACCESS_2_SHADER_STORAGE_WRITE_BIT
            );
        }

        vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_COMPUTE, vulkan.computePipelines[i]);

        const uint32_t groupsPerChunk = CHUNKSIZE / computeGroupSizes[i];

        for (uint32_t base = 0; base < chunkCount; base += maxComputeGroupCountX) {
            uint32_t count = chunkCount - base;
            if (count > maxComputeGroupCountX) { count = maxComputeGroupCountX; }

            vkCmdDispatchBase(commandBuffer, base, 0, 0, count, groupsPerChunk, 1);
        }
    }

    VANILLA_VK_computeMemoryBarrier(
        commandBuffer,
        VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT,
        VK_ACCESS_2_SHADER_STORAGE_WRITE_BIT,
        VK_PIPELINE_STAGE_2_DRAW_INDIRECT_BIT |
        VK_PIPELINE_STAGE_2_VERTEX_INPUT_BIT |
        VK_PIPELINE_STAGE_2_VERTEX_SHADER_BIT |
        VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT |
        VK_PIPELINE_STAGE_2_HOST_BIT,
        VK_ACCESS_2_INDIRECT_COMMAND_READ_BIT |
        VK_ACCESS_2_VERTEX_ATTRIBUTE_READ_BIT |
        VK_ACCESS_2_INDEX_READ_BIT |
        VK_ACCESS_2_SHADER_STORAGE_READ_BIT |
        VK_ACCESS_2_HOST_READ_BIT
    );
}
