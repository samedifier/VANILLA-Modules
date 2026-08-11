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

#include <vulkan/vulkan.h>
#include <stdio.h>
#include <stdint.h>

#include "vanilla_window_vulkan_utils.h"
#include "main.h"

static int inituploadbuffer(VkBuffer* buffer, VkDeviceMemory* memory, void** ptr, VkDeviceSize size, VkDeviceSize* sizeptr, VkMemoryPropertyFlags propertyflags) {
    VkBufferCreateInfo bufferInfo = {
        .sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO,
        .sharingMode = VK_SHARING_MODE_EXCLUSIVE,
        .usage = VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
        .size = size
    };

    VkMemoryRequirements memRequirements;

    if (vkCreateBuffer(vulkan.device, &bufferInfo, NULL, buffer) != VK_SUCCESS) {
        printf("[VANILLA] HATA: Upload Buffer oluşturulamadı\n");
        return 1;
    }

    vkGetBufferMemoryRequirements(vulkan.device, *buffer, &memRequirements);

    *memory = VANILLA_gmmap(&memRequirements, propertyflags);
    if (*memory == VK_NULL_HANDLE) {
        vkDestroyBuffer(vulkan.device, *buffer, NULL);
        *buffer = VK_NULL_HANDLE;

        return 1;
    }

    if (vkBindBufferMemory(vulkan.device, *buffer, *memory, 0) != VK_SUCCESS) {
        vkDestroyBuffer(vulkan.device, *buffer, NULL);
        *buffer = VK_NULL_HANDLE;

        vkFreeMemory(vulkan.device, *memory, NULL);
        *memory = VK_NULL_HANDLE;

        return 1;
    }

    if (vkMapMemory(vulkan.device, *memory, 0, size, 0, ptr) != VK_SUCCESS) {
        vkDestroyBuffer(vulkan.device, *buffer, NULL);
        *buffer = VK_NULL_HANDLE;

        vkFreeMemory(vulkan.device, *memory, NULL);
        *memory = VK_NULL_HANDLE;

        *ptr = NULL;

        return 1;
    }

    *sizeptr = size;

    return 0;
}

static int VANILLA_InitializeUploadBuffers(VkDeviceSize vertexbuffersize, VkDeviceSize indexbuffersize, VkDeviceSize transformbuffersize, VkDeviceSize modellookupbuffersize) {
    if (memoryMode == VANILLA_MEMORY_HOST_VISIBLE) { return 0; }

    if (memoryMode == VANILLA_MEMORY_DEVICE_LOCAL) {
        if(inituploadbuffer(&vulkan.uploadGlobalTransformBuffer, 
                            &vulkan.uploadGlobalTransformMemory,
                            &vulkan.mappedUploadTransformBufferPtr,
                            transformbuffersize,
                            &vulkan.uploadTransformBufferSize,
                            VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT)) { return 1; }

        if(inituploadbuffer(&vulkan.uploadModelLookupBuffer, 
                            &vulkan.uploadModelLookupMemory,
                            &vulkan.mappedUploadModelLookupPtr,
                            modellookupbuffersize,
                            &vulkan.uploadModelLookupSize,
                            VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_CACHED_BIT)) { return 1; }
    }

    if(inituploadbuffer(&vulkan.uploadGlobalVertexBuffer, 
                        &vulkan.uploadGlobalVertexMemory,
                        &vulkan.mappedUploadVertexBufferPtr,
                        vertexbuffersize,
                        &vulkan.uploadVertexSize,
                        VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT)) { return 1; }

    if(inituploadbuffer(&vulkan.uploadGlobalIndexBuffer, 
                        &vulkan.uploadGlobalIndexMemory,
                        &vulkan.mappedUploadIndexBufferPtr,
                        indexbuffersize,
                        &vulkan.uploadIndexSize,
                        VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT)) { return 1; }

    return 0;
}

int VANILLA_InitializePersistentBuffers(VkDevice device, VkPhysicalDevice physicalDevice) {
    VkBufferCreateInfo bufferInfo = {
        .sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO,
        .sharingMode = VK_SHARING_MODE_EXCLUSIVE
    };

    VkMemoryRequirements memRequirements;

    uint32_t maxTotalEntities = stream->chunkLimit * CHUNKSIZE;
    VkDeviceSize transformBufferSize = (VkDeviceSize)maxTotalEntities * sizeof(mat4);

    bufferInfo.size = transformBufferSize;
    bufferInfo.usage = VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT;

    if (vkCreateBuffer(device, &bufferInfo, NULL,
        &vulkan.globalTransformBuffer) != VK_SUCCESS) {
        return 1;
    }

    vkGetBufferMemoryRequirements(device,
        vulkan.globalTransformBuffer,
        &memRequirements);

    VkMemoryPropertyFlags properties;

    switch (memoryMode) {
        case VANILLA_MEMORY_HOST_VISIBLE:
            properties = VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT;
            break;

        case VANILLA_MEMORY_HYBRID:
            properties = VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT;
            break;

        case VANILLA_MEMORY_DEVICE_LOCAL:
            properties = VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT;
            break;

        default:
            return 1;
    }

    vulkan.globalTransformMemory = VANILLA_gmmap(&memRequirements, properties);

    if (vulkan.globalTransformMemory == VK_NULL_HANDLE) { return 1; }

    vkBindBufferMemory(device, vulkan.globalTransformBuffer, vulkan.globalTransformMemory, 0);

    vulkan.mappedTransformBufferPtr = NULL;

    if (memoryMode == VANILLA_MEMORY_HYBRID || memoryMode == VANILLA_MEMORY_HOST_VISIBLE) {
        if (vkMapMemory(
            device,
            vulkan.globalTransformMemory,
            0,
            transformBufferSize,
            0,
            &vulkan.mappedTransformBufferPtr) != VK_SUCCESS){
            
            return 1;
        }
    }

    uint32_t maxTotalModels = stream->modelChunkLimit * CHUNKSIZE;

    VkDeviceSize lookupBufferSize = (VkDeviceSize)maxTotalModels * sizeof(ModelLookup);

    bufferInfo.size = lookupBufferSize;
    bufferInfo.usage = VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT;

    if (vkCreateBuffer(device, &bufferInfo, NULL, &vulkan.modelLookupBuffer) != VK_SUCCESS) {
        return 1;
    }

    vkGetBufferMemoryRequirements(device, vulkan.modelLookupBuffer, &memRequirements);

    switch (memoryMode) {
        case VANILLA_MEMORY_HOST_VISIBLE:
            properties = VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT | VK_MEMORY_PROPERTY_HOST_CACHED_BIT;
            break;

        case VANILLA_MEMORY_HYBRID:
            properties = VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT | VK_MEMORY_PROPERTY_HOST_CACHED_BIT;
            break;

        case VANILLA_MEMORY_DEVICE_LOCAL:
            properties = VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT;
            break;

        default:
            return 1;
    }

    vulkan.modelLookupMemory = VANILLA_gmmap(&memRequirements, properties);

    if (vulkan.modelLookupMemory == VK_NULL_HANDLE) { return 1; }

    vkBindBufferMemory(device, vulkan.modelLookupBuffer, vulkan.modelLookupMemory, 0);

    if (memoryMode == VANILLA_MEMORY_HYBRID || memoryMode == VANILLA_MEMORY_HOST_VISIBLE) {
        if (vkMapMemory(
                device,
                vulkan.modelLookupMemory,
                0,
                lookupBufferSize,
                0,
                &vulkan.mappedModelLookupPtr) != VK_SUCCESS) {
            return 1;
        }
    }

    VkDeviceSize globalVertexBufferSize = (VkDeviceSize)vertexLimit * stride;

    bufferInfo.size = globalVertexBufferSize;
    bufferInfo.usage =
        VK_BUFFER_USAGE_VERTEX_BUFFER_BIT |
        VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT;

    if (vkCreateBuffer(device, &bufferInfo, NULL, &vulkan.globalVertexBuffer) != VK_SUCCESS) {
        return 1;
    }

    vkGetBufferMemoryRequirements(device, vulkan.globalVertexBuffer, &memRequirements);

    switch (memoryMode) {
        case VANILLA_MEMORY_HOST_VISIBLE:
            properties = VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT;
            break;

        case VANILLA_MEMORY_HYBRID:
            properties = VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT;
            break;

        case VANILLA_MEMORY_DEVICE_LOCAL:
            properties = VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT;
            break;

        default:
            return 1;
    }

    vulkan.globalVertexMemory = VANILLA_gmmap(&memRequirements, properties);

    if (vulkan.globalVertexMemory == VK_NULL_HANDLE) { return 1; }

    vkBindBufferMemory(device,
                       vulkan.globalVertexBuffer,
                       vulkan.globalVertexMemory,
                       0);

    vulkan.mappedVertexBufferPtr = NULL;

    if (memoryMode == VANILLA_MEMORY_HOST_VISIBLE) {
        if (vkMapMemory(
                device,
                vulkan.globalVertexMemory,
                0,
                globalVertexBufferSize,
                0,
                &vulkan.mappedVertexBufferPtr) != VK_SUCCESS) {
            return 1;
        }
    }

    VkDeviceSize globalIndexBufferSize = (VkDeviceSize)indexLimit * sizeof(uint32_t);

    bufferInfo.size = globalIndexBufferSize;
    bufferInfo.usage = VK_BUFFER_USAGE_INDEX_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT;

    if (vkCreateBuffer(device,
                       &bufferInfo,
                       NULL,
                       &vulkan.globalIndexBuffer) != VK_SUCCESS) {
        return 1;
    }

    vkGetBufferMemoryRequirements(device, vulkan.globalIndexBuffer, &memRequirements);

    switch (memoryMode) {
        case VANILLA_MEMORY_HOST_VISIBLE:
            properties = VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT;
            break;

        case VANILLA_MEMORY_HYBRID:
            properties = VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT;
            break;

        case VANILLA_MEMORY_DEVICE_LOCAL:
            properties = VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT;
            break;

        default:
            return 1;
    }

    vulkan.globalIndexMemory = VANILLA_gmmap(&memRequirements, properties);

    if (vulkan.globalIndexMemory == VK_NULL_HANDLE) { return 1; }

    vkBindBufferMemory(device,
                       vulkan.globalIndexBuffer,
                       vulkan.globalIndexMemory,
                       0);

    vulkan.mappedIndexBufferPtr = NULL;

    if (memoryMode == VANILLA_MEMORY_HOST_VISIBLE) {
        if (vkMapMemory(
            device,
            vulkan.globalIndexMemory,
            0,
            globalIndexBufferSize,
            0,
            &vulkan.mappedIndexBufferPtr) != VK_SUCCESS) {

            return 1;
        }
    }

    vulkan.currentVertexOffset = 0;
    vulkan.currentIndexOffset = 0;

    VANILLA_InitializeUploadBuffers(globalVertexBufferSize, globalIndexBufferSize, transformBufferSize, lookupBufferSize);

    return 0;
}

void VANILLA_ExtractAndPushModel(uint32_t modelIndex, void* anycoreModelBuffer, uint32_t vertexCount, void* anycoreIndexBuffer, uint32_t indexCount, uint32_t maxModelIndex, VANILLA_VertexLayout* layout) {
    if (vulkan.currentVertexOffset + vertexCount > vertexLimit || vulkan.currentIndexOffset + indexCount > indexLimit) {
        return;
    }

    uint8_t* gpuBytePool = (uint8_t*)vulkan.mappedVertexBufferPtr;
    if (vulkan.mappedVertexBufferPtr == NULL) {
        gpuBytePool = (uint8_t*)vulkan.mappedUploadVertexBufferPtr;
    }
    
    uint8_t* srcBytePool = (uint8_t*)anycoreModelBuffer;
    
    uint32_t gpuBaseOffset = vulkan.currentVertexOffset * stride;
    size_t anycoreStride = stream->stride;

    for (uint32_t v = 0; v < vertexCount; v++) {
        uint8_t* currentSrcVertex = srcBytePool + (v * anycoreStride);
        uint8_t* currentDstVertex = gpuBytePool + gpuBaseOffset + (v * stride);

        memset(currentDstVertex, 0, stride);

        for (uint32_t a = 0; a < vertexLayout.attributeCount; a++) {
            VANILLA_VertexAttribute attr = vertexLayout.attributes[a];

            if (attr.vanillaOffset + attr.size <= stride && attr.size > 0) {
                memcpy(currentDstVertex + attr.vanillaOffset, 
                       currentSrcVertex + attr.anycoreOffset, 
                       attr.size);
            }
        }
    }

    uint32_t* gpuIndexPool = (uint32_t*)vulkan.mappedIndexBufferPtr;
    if (vulkan.mappedIndexBufferPtr == NULL) {
        gpuIndexPool = (uint32_t*)vulkan.mappedUploadIndexBufferPtr;
    }
    uint32_t* srcIndexPool = (uint32_t*)anycoreIndexBuffer;

    memcpy(&gpuIndexPool[vulkan.currentIndexOffset], srcIndexPool, indexCount * sizeof(uint32_t));

    ModelLookup* lookupTable = vulkan.mappedModelLookupPtr;
    if(vulkan.mappedModelLookupPtr == NULL) {
        lookupTable = vulkan.mappedUploadModelLookupPtr;
    }
    lookupTable[modelIndex].firstVertex = vulkan.currentVertexOffset;
    lookupTable[modelIndex].vertexCount = vertexCount;
    lookupTable[modelIndex].firstIndex  = vulkan.currentIndexOffset;
    lookupTable[modelIndex].indexCount  = indexCount;
    lookupTable[modelIndex].inuse  = true;

    vulkan.currentVertexOffset += vertexCount;
    vulkan.currentIndexOffset  += indexCount;

    if (vulkan.mappedUploadVertexBufferPtr != NULL) {
        vulkan.vertexBufferCopy = (VkBufferCopy){
            .srcOffset = 0,
            .dstOffset = 0,
            .size      = (VkDeviceSize)(vulkan.currentVertexOffset * stride)
        };
    }

    if (vulkan.mappedUploadIndexBufferPtr != NULL) {
        vulkan.indexBufferCopy = (VkBufferCopy){
            .srcOffset = 0,
            .dstOffset = 0,
            .size      = (VkDeviceSize)(vulkan.currentIndexOffset * sizeof(uint32_t))
        };
    }

    if (vulkan.mappedUploadModelLookupPtr != NULL) {
        vulkan.modelLookupBufferCopy = (VkBufferCopy){
            .srcOffset = 0,
            .dstOffset = 0,
            .size      = (VkDeviceSize)(maxModelIndex * sizeof(ModelLookup))
        };
    }
}

void VANILLA_redoModel(uint32_t modelIndex, void* anycoreModelBuffer, uint32_t vertexCount, void* anycoreIndexBuffer, uint32_t indexCount, VANILLA_VertexLayout* layout) {
    ModelLookup* lookupTable = vulkan.mappedModelLookupPtr;
    if (vulkan.mappedModelLookupPtr == NULL) {
        lookupTable = vulkan.mappedUploadModelLookupPtr;
    }

    if (vertexCount > lookupTable[modelIndex].vertexCount || indexCount > lookupTable[modelIndex].indexCount) {
        return;
    }

    uint32_t targetVertexOffset = lookupTable[modelIndex].firstVertex;
    uint32_t targetIndexOffset  = lookupTable[modelIndex].firstIndex;

    uint8_t* gpuBytePool = (uint8_t*)vulkan.mappedVertexBufferPtr;
    if (vulkan.mappedVertexBufferPtr == NULL) {
        gpuBytePool = (uint8_t*)vulkan.mappedUploadVertexBufferPtr;
    }
    
    uint8_t* srcBytePool = (uint8_t*)anycoreModelBuffer;
    uint32_t gpuBaseOffset = targetVertexOffset * stride;
    size_t anycoreStride = stream->stride;

    for (uint32_t v = 0; v < vertexCount; v++) {
        uint8_t* currentSrcVertex = srcBytePool + (v * anycoreStride);
        uint8_t* currentDstVertex = gpuBytePool + gpuBaseOffset + (v * stride);

        memset(currentDstVertex, 0, stride);

        for (uint32_t a = 0; a < vertexLayout.attributeCount; a++) {
            VANILLA_VertexAttribute attr = vertexLayout.attributes[a];

            if (attr.vanillaOffset + attr.size <= stride && attr.size > 0) {
                memcpy(currentDstVertex + attr.vanillaOffset, currentSrcVertex + attr.anycoreOffset, attr.size);
            }
        }
    }

    uint32_t* gpuIndexPool = (uint32_t*)vulkan.mappedIndexBufferPtr;
    if (vulkan.mappedIndexBufferPtr == NULL) {
        gpuIndexPool = (uint32_t*)vulkan.mappedUploadIndexBufferPtr;
    }
    uint32_t* srcIndexPool = (uint32_t*)anycoreIndexBuffer;

    memcpy(&gpuIndexPool[targetIndexOffset], srcIndexPool, indexCount * sizeof(uint32_t));

    lookupTable[modelIndex].vertexCount = vertexCount;
    lookupTable[modelIndex].indexCount  = indexCount;
    lookupTable[modelIndex].inuse       = true;
}

void VANILLA_CleanupPersistentBuffers(VkDevice device) {
    if (vulkan.mappedUploadTransformBufferPtr) {
        vkUnmapMemory(device, vulkan.uploadGlobalTransformMemory);
        vulkan.mappedUploadTransformBufferPtr = NULL;
    }
    if (vulkan.mappedUploadModelLookupPtr) {
        vkUnmapMemory(device, vulkan.uploadModelLookupMemory);
        vulkan.mappedUploadModelLookupPtr = NULL;
    }
    if (vulkan.mappedUploadVertexBufferPtr) {
        vkUnmapMemory(device, vulkan.uploadGlobalVertexMemory);
        vulkan.mappedUploadVertexBufferPtr = NULL;
    }
    if (vulkan.mappedUploadIndexBufferPtr) {
        vkUnmapMemory(device, vulkan.uploadGlobalIndexMemory);
        vulkan.mappedUploadIndexBufferPtr = NULL;
    }

    if (vulkan.mappedTransformBufferPtr) {
        vkUnmapMemory(device, vulkan.globalTransformMemory);
        vulkan.mappedTransformBufferPtr = NULL;
    }
    if (vulkan.mappedModelLookupPtr) {
        vkUnmapMemory(device, vulkan.modelLookupMemory);
        vulkan.mappedModelLookupPtr = NULL;
    }
    if (vulkan.mappedVertexBufferPtr) {
        vkUnmapMemory(device, vulkan.globalVertexMemory);
        vulkan.mappedVertexBufferPtr = NULL;
    }
    if (vulkan.mappedIndexBufferPtr) {
        vkUnmapMemory(device, vulkan.globalIndexMemory);
        vulkan.mappedIndexBufferPtr = NULL;
    }

    if (vulkan.uploadGlobalTransformBuffer) { vkDestroyBuffer(device, vulkan.uploadGlobalTransformBuffer, NULL); }
    if (vulkan.uploadGlobalTransformMemory) { vkFreeMemory(device, vulkan.uploadGlobalTransformMemory, NULL); }

    if (vulkan.uploadModelLookupBuffer) { vkDestroyBuffer(device, vulkan.uploadModelLookupBuffer, NULL); }
    if (vulkan.uploadModelLookupMemory) { vkFreeMemory(device, vulkan.uploadModelLookupMemory, NULL); }

    if (vulkan.uploadGlobalVertexBuffer) { vkDestroyBuffer(device, vulkan.uploadGlobalVertexBuffer, NULL); }
    if (vulkan.uploadGlobalVertexMemory) { vkFreeMemory(device, vulkan.uploadGlobalVertexMemory, NULL); }

    if (vulkan.uploadGlobalIndexBuffer) { vkDestroyBuffer(device, vulkan.uploadGlobalIndexBuffer, NULL); }
    if (vulkan.uploadGlobalIndexMemory) { vkFreeMemory(device, vulkan.uploadGlobalIndexMemory, NULL); }

    if (vulkan.globalTransformBuffer) { vkDestroyBuffer(device, vulkan.globalTransformBuffer, NULL); }
    if (vulkan.globalTransformMemory) { vkFreeMemory(device, vulkan.globalTransformMemory, NULL); }

    if (vulkan.modelLookupBuffer) { vkDestroyBuffer(device, vulkan.modelLookupBuffer, NULL); }
    if (vulkan.modelLookupMemory) { vkFreeMemory(device, vulkan.modelLookupMemory, NULL); }

    if (vulkan.globalVertexBuffer) { vkDestroyBuffer(device, vulkan.globalVertexBuffer, NULL); }
    if (vulkan.globalVertexMemory) { vkFreeMemory(device, vulkan.globalVertexMemory, NULL); }

    if (vulkan.globalIndexBuffer) { vkDestroyBuffer(device, vulkan.globalIndexBuffer, NULL); }
    if (vulkan.globalIndexMemory) { vkFreeMemory(device, vulkan.globalIndexMemory, NULL); }
}

int VANILLA_VK_initIndirectBuffer(void) {
    VkDeviceSize indirectBufferSize = (VkDeviceSize)stream->chunkLimit * CHUNKSIZE * sizeof(VkDrawIndexedIndirectCommand);

    VkBufferCreateInfo bufferInfo = {
        .sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO,
        .size = indirectBufferSize,
        .usage = VK_BUFFER_USAGE_INDIRECT_BUFFER_BIT,
        .sharingMode = VK_SHARING_MODE_EXCLUSIVE
    };

    if (vkCreateBuffer(vulkan.device, &bufferInfo, NULL, &vulkan.indirectCommandBuffer) != VK_SUCCESS) {
        return 1;
    }

    VkMemoryRequirements memReqs;
    vkGetBufferMemoryRequirements(vulkan.device, vulkan.indirectCommandBuffer, &memReqs);

    uint32_t memTypeIdx = VANILLA_FindMemoryType(
        vulkan.physicalDevice,
        memReqs.memoryTypeBits, 
        VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT
    );

    VkMemoryAllocateInfo allocInfo = {
        .sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO,
        .allocationSize = memReqs.size,
        .memoryTypeIndex = memTypeIdx
    };

    if (vkAllocateMemory(vulkan.device, &allocInfo, NULL, &vulkan.indirectCommandBufferMemory) != VK_SUCCESS) {
        vkDestroyBuffer(vulkan.device, vulkan.indirectCommandBuffer, NULL);
        return 1;
    }

    vkBindBufferMemory(vulkan.device, vulkan.indirectCommandBuffer, vulkan.indirectCommandBufferMemory, 0);

    if (vkMapMemory(
            vulkan.device, 
            vulkan.indirectCommandBufferMemory, 
            0, 
            indirectBufferSize, 
            0, 
            (void**)&vulkan.mappedIndirectCommands) != VK_SUCCESS) 
    {
        vkFreeMemory(vulkan.device, vulkan.indirectCommandBufferMemory, NULL);
        vkDestroyBuffer(vulkan.device, vulkan.indirectCommandBuffer, NULL);
        return 1;
    }

    memset(vulkan.mappedIndirectCommands, 0, indirectBufferSize);

    return 0;
}

void VANILLA_VK_cleanupIndirectBuffer(void) {
    if (vulkan.indirectCommandBufferMemory != VK_NULL_HANDLE) {
        vkUnmapMemory(vulkan.device, vulkan.indirectCommandBufferMemory);
    }

    if (vulkan.indirectCommandBuffer != VK_NULL_HANDLE) {
        vkDestroyBuffer(vulkan.device, vulkan.indirectCommandBuffer, NULL);
        vulkan.indirectCommandBuffer = VK_NULL_HANDLE;
    }

    if (vulkan.indirectCommandBufferMemory != VK_NULL_HANDLE) {
        vkFreeMemory(vulkan.device, vulkan.indirectCommandBufferMemory, NULL);
        vulkan.indirectCommandBufferMemory = VK_NULL_HANDLE;
    }

    vulkan.mappedIndirectCommands = NULL;
}