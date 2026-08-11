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
#include "standardcomponent_utils.h"

uint32_t materialLimit = 512;

StandardComponent materialComponent;

static int VANILLA_VK_createMaterialUploadBuffer(void) {
    VkDeviceSize size = sizeof(VANILLA_GPUMaterial) * materialLimit;

    VkBufferCreateInfo createInfo = {
        .sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO,
        .size = size,
        .usage = VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
        .sharingMode = VK_SHARING_MODE_EXCLUSIVE
    };

    if (vkCreateBuffer(vulkan.device, &createInfo, NULL, &vulkan.materialUploadBuffer) != VK_SUCCESS) {
        return 1;
    }

    VkMemoryRequirements requirements;
    vkGetBufferMemoryRequirements(vulkan.device, vulkan.materialUploadBuffer, &requirements);

    vulkan.materialUploadMemory = VANILLA_gmmap(&requirements,
        VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT | VK_MEMORY_PROPERTY_HOST_CACHED_BIT);

    if (vulkan.materialUploadMemory == VK_NULL_HANDLE) {
        vkDestroyBuffer(vulkan.device, vulkan.materialUploadBuffer, NULL);

        vulkan.materialUploadBuffer = VK_NULL_HANDLE;

        return 1;
    }

    if (vkBindBufferMemory(vulkan.device, vulkan.materialUploadBuffer, vulkan.materialUploadMemory, 0) != VK_SUCCESS) {
        VANILLA_gmunmap(vulkan.materialUploadMemory);

        vkDestroyBuffer(vulkan.device, vulkan.materialUploadBuffer, NULL);

        vulkan.materialUploadMemory = VK_NULL_HANDLE;
        vulkan.materialUploadBuffer = VK_NULL_HANDLE;

        return 1;
    }

    if (vkMapMemory(vulkan.device, vulkan.materialUploadMemory, 0, size, 0, &vulkan.mappedMaterialBufferPtr) != VK_SUCCESS) {
        VANILLA_gmunmap(vulkan.materialUploadMemory);

        vkDestroyBuffer(vulkan.device, vulkan.materialUploadBuffer, NULL);

        vulkan.materialUploadMemory = VK_NULL_HANDLE;
        vulkan.materialUploadBuffer = VK_NULL_HANDLE;

        return 1;
    }

    vulkan.materialCopy.srcOffset = 0;
    vulkan.materialCopy.dstOffset = 0;
    vulkan.materialCopy.size = size;

    return 0;
}

int VANILLA_VK_createMaterialBuffer(void) {
    vulkan.materialGenerations = VANILLA_mmap(materialLimit * sizeof(uint16_t));
    vulkan.materialFreeSlots   = VANILLA_mmap(materialLimit * sizeof(uint32_t));

    if (vulkan.materialGenerations == VANILLA_MAP_FAILED || vulkan.materialFreeSlots == VANILLA_MAP_FAILED) {
        if (vulkan.materialGenerations != VANILLA_MAP_FAILED) { VANILLA_munmap(vulkan.materialGenerations, materialLimit * sizeof(uint16_t)); }
        if (vulkan.materialFreeSlots   != VANILLA_MAP_FAILED) { VANILLA_munmap(vulkan.materialFreeSlots,   materialLimit * sizeof(uint32_t)); }
    }

    for (uint32_t i = 0; i < materialLimit; i++) { vulkan.materialFreeSlots[i] = i; }
    vulkan.materialFreeSlotCount = materialLimit;

    VkDeviceSize size = sizeof(VANILLA_GPUMaterial) * materialLimit;

    VkBufferCreateInfo createInfo = {
        .sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO,
        .size = size,
        .usage = VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT,
        .sharingMode = VK_SHARING_MODE_EXCLUSIVE
    };

    if (vkCreateBuffer(vulkan.device, &createInfo, NULL, &vulkan.materialBuffer) != VK_SUCCESS) {
        return 1;
    }

    VkMemoryRequirements requirements;
    vkGetBufferMemoryRequirements(vulkan.device, vulkan.materialBuffer, &requirements);

    VkMemoryPropertyFlags properties = VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT;
    if (memoryMode == VANILLA_MEMORY_HOST_VISIBLE) {
        properties = VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT | VK_MEMORY_PROPERTY_HOST_CACHED_BIT;
    }

    vulkan.materialMemory = VANILLA_gmmap(&requirements, properties);

    if (vulkan.materialMemory == VK_NULL_HANDLE) {
        vkDestroyBuffer(vulkan.device, vulkan.materialBuffer, NULL);

        vulkan.materialBuffer = VK_NULL_HANDLE;

        VANILLA_munmap(vulkan.materialGenerations, materialLimit * sizeof(uint16_t));
        VANILLA_munmap(vulkan.materialFreeSlots,   materialLimit * sizeof(uint32_t));


        return 1;
    }

    if (vkBindBufferMemory(vulkan.device, vulkan.materialBuffer, vulkan.materialMemory, 0) != VK_SUCCESS) {
        VANILLA_gmunmap(vulkan.materialMemory);

        vkDestroyBuffer(vulkan.device, vulkan.materialBuffer, NULL);

        vulkan.materialMemory = VK_NULL_HANDLE;
        vulkan.materialBuffer = VK_NULL_HANDLE;

        VANILLA_munmap(vulkan.materialGenerations, materialLimit * sizeof(uint16_t));
        VANILLA_munmap(vulkan.materialFreeSlots,   materialLimit * sizeof(uint32_t));

        return 1;
    }

    if (memoryMode == VANILLA_MEMORY_HOST_VISIBLE) {
        if (vkMapMemory(vulkan.device, vulkan.materialMemory, 0, size, 0, &vulkan.mappedMaterialBufferPtr) != VK_SUCCESS) {
            VANILLA_gmunmap(vulkan.materialMemory);

            vkDestroyBuffer(vulkan.device, vulkan.materialBuffer, NULL);

            vulkan.materialMemory = VK_NULL_HANDLE;
            vulkan.materialBuffer = VK_NULL_HANDLE;

            VANILLA_munmap(vulkan.materialGenerations, materialLimit * sizeof(uint16_t));
            VANILLA_munmap(vulkan.materialFreeSlots,   materialLimit * sizeof(uint32_t));

            return 1;
        }
    }

    if (memoryMode != VANILLA_MEMORY_HOST_VISIBLE) {
        if (VANILLA_VK_createMaterialUploadBuffer() != VANILLA_SUCCESS) {
            VANILLA_gmunmap(vulkan.materialMemory);
            vkDestroyBuffer(vulkan.device, vulkan.materialBuffer, NULL);

            vulkan.materialMemory = VK_NULL_HANDLE;
            vulkan.materialBuffer = VK_NULL_HANDLE;

            VANILLA_munmap(vulkan.materialGenerations, materialLimit * sizeof(uint16_t));
            VANILLA_munmap(vulkan.materialFreeSlots,   materialLimit * sizeof(uint32_t));

            return 1;
        }
    }

    ///

    VkDeviceSize slotSize = stream->chunkLimit * CHUNKSIZE * sizeof(VANILLA_GPUMaterialSlot);

    VkBufferCreateInfo slotCreateInfo = {
        .sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO,
        .size = slotSize,
        .usage = VK_BUFFER_USAGE_STORAGE_BUFFER_BIT,
        .sharingMode = VK_SHARING_MODE_EXCLUSIVE
    };

    if (vkCreateBuffer(vulkan.device, &slotCreateInfo, NULL, &vulkan.materialSlotBuffer) != VK_SUCCESS) { return 1; }

    VkMemoryRequirements slotRequirements;
    vkGetBufferMemoryRequirements(vulkan.device, vulkan.materialSlotBuffer, &slotRequirements);

    vulkan.materialSlotMemory = VANILLA_gmmap(
        &slotRequirements,
        VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT |
        VK_MEMORY_PROPERTY_HOST_COHERENT_BIT |
        VK_MEMORY_PROPERTY_HOST_CACHED_BIT
    );

    if (vulkan.materialSlotMemory == VK_NULL_HANDLE) {
        vkDestroyBuffer(vulkan.device, vulkan.materialSlotBuffer, NULL);
        vulkan.materialSlotBuffer = VK_NULL_HANDLE;
        return 1;
    }

    if (vkBindBufferMemory(
        vulkan.device,
        vulkan.materialSlotBuffer,
        vulkan.materialSlotMemory,
        0
    ) != VK_SUCCESS) {

        VANILLA_gmunmap(vulkan.materialSlotMemory);

        vkDestroyBuffer(vulkan.device, vulkan.materialSlotBuffer, NULL);

        vulkan.materialSlotMemory = VK_NULL_HANDLE;
        vulkan.materialSlotBuffer = VK_NULL_HANDLE;

        return 1;
    }

    if (vkMapMemory(
        vulkan.device,
        vulkan.materialSlotMemory,
        0,
        slotSize,
        0,
        &vulkan.mappedMaterialSlotBufferPtr
    ) != VK_SUCCESS) {

        VANILLA_gmunmap(vulkan.materialSlotMemory);

        vkDestroyBuffer(vulkan.device, vulkan.materialSlotBuffer, NULL);

        vulkan.materialSlotMemory = VK_NULL_HANDLE;
        vulkan.materialSlotBuffer = VK_NULL_HANDLE;

        return 1;
    }

    memset(vulkan.mappedMaterialSlotBufferPtr, 0, slotSize);

    return 0;
}

static void VANILLA_VK_destroyMaterialUploadBuffer(void) {
    if (vulkan.materialUploadBuffer != VK_NULL_HANDLE) {
        vkDestroyBuffer(vulkan.device, vulkan.materialUploadBuffer, NULL);
        vulkan.materialUploadBuffer = VK_NULL_HANDLE;
    }

    if (vulkan.materialUploadMemory != VK_NULL_HANDLE) {
        VANILLA_gmunmap(vulkan.materialUploadMemory);
        vulkan.materialUploadMemory = VK_NULL_HANDLE;
    }

    vulkan.materialCopy.srcOffset = 0;
    vulkan.materialCopy.dstOffset = 0;
    vulkan.materialCopy.size = 0;
}

void VANILLA_VK_destroyMaterialBuffer(void) {
    if (vulkan.materialFreeSlots != NULL) {
        VANILLA_munmap(vulkan.materialFreeSlots, materialLimit * sizeof(uint32_t));
        vulkan.materialFreeSlots = NULL;
    }
    if (vulkan.materialGenerations != NULL) {
        VANILLA_munmap(vulkan.materialGenerations, materialLimit * sizeof(uint16_t));
        vulkan.materialGenerations = NULL;
    }
    vulkan.materialFreeSlotCount = 0;

    if (vulkan.mappedMaterialBufferPtr) {
        if (memoryMode != VANILLA_MEMORY_HOST_VISIBLE) {
            vkUnmapMemory(vulkan.device, vulkan.materialUploadMemory);
        }
        else {
            vkUnmapMemory(vulkan.device, vulkan.materialMemory);
        }
        vulkan.mappedMaterialBufferPtr = NULL;
    }

    if (vulkan.materialBuffer != VK_NULL_HANDLE) {
        vkDestroyBuffer(vulkan.device, vulkan.materialBuffer, NULL);
        vulkan.materialBuffer = VK_NULL_HANDLE;
    }

    if (vulkan.materialMemory != VK_NULL_HANDLE) {
        VANILLA_gmunmap(vulkan.materialMemory);
        vulkan.materialMemory = VK_NULL_HANDLE;
    }

    VANILLA_VK_destroyMaterialUploadBuffer();
}

VANILLA_EXPORT VANILLA_RESULT VANILLA_createMaterial(
    const VANILLA_MaterialCreateInfo materialCreateInfo,
    MaterialID* outMaterialID
) {
    if (!outMaterialID) {
        return VANILLA_ERR_NULL_POINTER;
    }

    if (vulkan.materialFreeSlotCount == 0) {
        return VANILLA_ERR_CAPACITY_REACHED;
    }

    uint32_t slot = vulkan.materialFreeSlots[--vulkan.materialFreeSlotCount];

    VANILLA_GPUMaterial material = {0};

    material.albedoSlot = materialCreateInfo.albedo.slot;
    material.normalSlot = materialCreateInfo.normal.slot;
    material.samplerSlot = materialCreateInfo.sampler.slot;

    material.baseColor = materialCreateInfo.baseColor;

    material.metallic = materialCreateInfo.metallic;
    material.roughness = materialCreateInfo.roughness;

    material.flags |= 1UL;

    ((VANILLA_GPUMaterial*)vulkan.mappedMaterialBufferPtr)[slot] = material;

    if (slot > vulkan.currentMaterialOffset) {
        vulkan.currentMaterialOffset = slot + 1;
    }

    vulkan.materialCopy = (VkBufferCopy){
        .srcOffset = 0,
        .dstOffset = 0,
        .size = (VkDeviceSize)(
            vulkan.currentMaterialOffset * sizeof(VANILLA_GPUMaterial)
        )
    };

    *outMaterialID = (MaterialID){
        slot,
        ++vulkan.materialGenerations[slot]
    };

    return VANILLA_SUCCESS;
}

VANILLA_EXPORT VANILLA_RESULT VANILLA_destroyMaterial(MaterialID materialID) {
    if (materialID.slot >= materialLimit) {
        return VANILLA_ERR_INVALID_MATERIAL;
    }

    if (vulkan.materialGenerations[materialID.slot] != materialID.generation) {
        return VANILLA_ERR_INVALID_MATERIAL;
    }

    VANILLA_GPUMaterial* material =
        &((VANILLA_GPUMaterial*)vulkan.mappedMaterialBufferPtr)[materialID.slot];

    material->flags &= ~1UL;

    vulkan.materialFreeSlots[vulkan.materialFreeSlotCount++] =
        materialID.slot;

    return VANILLA_SUCCESS;
}

VANILLA_EXPORT VANILLA_RESULT VANILLA_setMaterialComponent(
    EntityID entityID,
    MaterialID materialID,
    uint32_t materialFlags1,
    uint32_t materialFlags2,
    uint32_t materialFlags3
) {
    if (materialID.slot >= materialLimit) {
        return VANILLA_ERR_INVALID_MATERIAL;
    }

    if (vulkan.materialGenerations[materialID.slot] != materialID.generation) {
        return VANILLA_ERR_INVALID_MATERIAL;
    }

    if (!StandardComponent_has(&materialComponent, entityID)) {
        StandardComponent_RESULT result =
            StandardComponent_add(&materialComponent, entityID);

        if (result != STANDARDCOMPONENT_SUCCESS) {
            return VANILLA_ERR_ADD_MATERIAL_COMPONENT;
        }
    }

    VANILLA_GPUMaterialSlot* gpuMaterial =
        &((VANILLA_GPUMaterialSlot*)vulkan.mappedMaterialSlotBufferPtr)[entityID.slot];

    if (StandardComponent_setData(&materialComponent, entityID, &materialID) != STANDARDCOMPONENT_SUCCESS) {
        return VANILLA_ERR_ADD_MATERIAL_COMPONENT;
    }

    if (vulkan.maxmcslot < entityID.slot) { vulkan.maxmcslot = entityID.slot; }

    gpuMaterial->materialSlot = materialID.slot;
    gpuMaterial->flags1 = materialFlags1;
    gpuMaterial->flags2 = materialFlags2;
    gpuMaterial->flags3 = materialFlags3;

    return VANILLA_SUCCESS;
}

VANILLA_EXPORT VANILLA_RESULT VANILLA_removeMaterialComponent(EntityID entityID) {
    if (!StandardComponent_has(&materialComponent, entityID)) { return VANILLA_ERR_HAS_NO_MATERIAL; }
    if (StandardComponent_remove(&materialComponent, entityID) != STANDARDCOMPONENT_SUCCESS) {
        return VANILLA_ERR_REMOVE_MATERIAL_COMPONENT;
    }

    return VANILLA_SUCCESS;
}

VANILLA_EXPORT VANILLA_RESULT VANILLA_getMaterialComponent(EntityID entityID, MaterialID* outMaterialID) {
    if (outMaterialID == NULL) { return VANILLA_ERR_NULL_POINTER; }
    if (!StandardComponent_has(&materialComponent, entityID)) { return VANILLA_ERR_HAS_NO_MATERIAL; }

    MaterialID* material;
    if (StandardComponent_getData(&materialComponent, entityID, (void**)&material) != STANDARDCOMPONENT_SUCCESS) {
        return VANILLA_ERR_GET_MATERIAL_COMPONENT;
    }

    *outMaterialID = *material;

    return VANILLA_SUCCESS;
}

VANILLA_EXPORT bool VANILLA_hasMaterialComponent(EntityID entityID) {
    return StandardComponent_has(&materialComponent, entityID);
}