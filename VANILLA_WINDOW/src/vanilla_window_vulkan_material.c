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
#include <math.h>

/* CPU copy of each material's alpha mode: routes entities to draw lists without reading GPU memory. */
static uint8_t* materialAlphaModes = NULL;

uint32_t materialLimit = 512;
StandardComponent materialComponent;

static bool didsetmateriallimit = false;

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
        return 1;
    }

    for (uint32_t i = 0; i < materialLimit; i++) { vulkan.materialFreeSlots[i] = i; }
    vulkan.materialFreeSlotCount = materialLimit;

    materialAlphaModes = VANILLA_mmap(materialLimit * sizeof(uint8_t));
    if (materialAlphaModes == VANILLA_MAP_FAILED) { materialAlphaModes = NULL; return 1; }
    memset(materialAlphaModes, 0, materialLimit * sizeof(uint8_t));

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
        if (VANILLA_VK_createMaterialUploadBuffer() != VANILLA_WINDOW_SUCCESS) {
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
    if (materialAlphaModes != NULL) {
        VANILLA_munmap(materialAlphaModes, materialLimit * sizeof(uint8_t));
        materialAlphaModes = NULL;
    }
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

static bool VANILLA_VK_isTextureValid(TextureID texture) {
    return texture.generation != 0 &&
           texture.slot < textureLimit &&
           vulkan.textureGenerations[texture.slot] == texture.generation;
}

static bool VANILLA_VK_isSamplerValid(SamplerID sampler) {
    return sampler.generation != 0 &&
           sampler.slot < samplerLimit &&
           vulkan.samplerGenerations[sampler.slot] == sampler.generation;
}

/* KHR_texture_transform as two rows of a 2x3 matrix: T * R * S (zero scale = 1). */
static void VANILLA_VK_uvTransformRows(const VANILLA_TextureTransform* t, Vec4f* row0, Vec4f* row1) {
    const float sx = (t->scale[0] != 0.0f) ? t->scale[0] : 1.0f;
    const float sy = (t->scale[1] != 0.0f) ? t->scale[1] : 1.0f;
    const float c  = cosf(t->rotation);
    const float sn = sinf(t->rotation);

    *row0 = (Vec4f){  c * sx, sn * sy, t->offset[0], 0.0f };
    *row1 = (Vec4f){ -sn * sx, c * sy, t->offset[1], 0.0f };
}

VANILLA_EXPORT VANILLA_WINDOW_RESULT VANILLA_createMaterial(const VANILLA_MaterialCreateInfo materialCreateInfo, MaterialID* outMaterialID) {
    if (didstart == false) { return VANILLA_WINDOW_ERR_DIDNT_START; }
    if (!outMaterialID) { return VANILLA_WINDOW_ERR_NULL_POINTER; }
    if (vulkan.materialFreeSlotCount == 0) { return VANILLA_WINDOW_ERR_CAPACITY_REACHED; }

    uint32_t slot = vulkan.materialFreeSlots[--vulkan.materialFreeSlotCount];

    VANILLA_GPUMaterial material = {0};

    material.albedoSlot = materialCreateInfo.albedo.slot;
    material.normalSlot = materialCreateInfo.normal.slot;
    material.metallicRoughnessSlot = materialCreateInfo.metallicRoughness.slot;
    material.occlusionSlot = materialCreateInfo.occlusion.slot;
    material.emissiveSlot = materialCreateInfo.emissive.slot;
    material.samplerSlot = materialCreateInfo.sampler.slot;

    material.flags1 = materialCreateInfo.flags1 | VANILLA_MATERIALFLAGS1_VALID;

    /*
     * Every valid texture turns its USE_* flag on by itself (like Unity's Lit shader).
     * Flags passed in flags1 are kept, so a feature can still be forced on.
     */
    if (VANILLA_VK_isTextureValid(materialCreateInfo.albedo))            { material.flags1 |= VANILLA_MATERIALFLAGS1_USE_ALBEDO; }
    if (VANILLA_VK_isTextureValid(materialCreateInfo.normal))            { material.flags1 |= VANILLA_MATERIALFLAGS1_USE_NORMAL_MAP; }
    if (VANILLA_VK_isTextureValid(materialCreateInfo.metallicRoughness)) { material.flags1 |= VANILLA_MATERIALFLAGS1_USE_METALLIC_ROUGHNESS; }
    if (VANILLA_VK_isTextureValid(materialCreateInfo.occlusion))         { material.flags1 |= VANILLA_MATERIALFLAGS1_USE_OCCLUSION; }
    if (VANILLA_VK_isTextureValid(materialCreateInfo.emissive))          { material.flags1 |= VANILLA_MATERIALFLAGS1_USE_EMISSIVE; }
    material.flags2 = materialCreateInfo.flags2;
    material.flags3 = materialCreateInfo.flags3;

    material.alphaCutoff = materialCreateInfo.alphaCutoff;

    material.baseColor = materialCreateInfo.baseColor;
    material.emissiveFactor = materialCreateInfo.emissiveFactor;

    material.metallic = materialCreateInfo.metallic;
    material.roughness = materialCreateInfo.roughness;
    material.normalScale = materialCreateInfo.normalScale;
    material.occlusionStrength = materialCreateInfo.occlusionStrength;

    /* glTF 2.0 */
    if (materialCreateInfo.doubleSided) { material.flags1 |= VANILLA_MATERIALFLAGS1_DOUBLE_SIDED; }
    if (materialCreateInfo.unlit)       { material.flags1 |= VANILLA_MATERIALFLAGS1_UNLIT; }
    if (materialCreateInfo.vertexColor) { material.flags1 |= VANILLA_MATERIALFLAGS1_USE_VERTEX_COLOR; }

    material.alphaMode = (materialCreateInfo.alphaMode <= VANILLA_ALPHAMODE_BLEND)
        ? (uint32_t)materialCreateInfo.alphaMode : (uint32_t)VANILLA_ALPHAMODE_OPAQUE;

    for (uint32_t i = 0; i < VANILLA_MATERIAL_TEXTURE_COUNT; i++) {
        const SamplerID textureSampler = materialCreateInfo.textureSamplers[i];
        material.samplerSlots[i] = VANILLA_VK_isSamplerValid(textureSampler) ? textureSampler.slot : material.samplerSlot;

        const VANILLA_TextureTransform* transform = &materialCreateInfo.textureTransforms[i];
        if (transform->texCoord == 1) { material.texCoordBits |= 1u << i; }
        VANILLA_VK_uvTransformRows(transform, &material.uvTransforms[i * 2], &material.uvTransforms[i * 2 + 1]);
    }

    ((VANILLA_GPUMaterial*)vulkan.mappedMaterialBufferPtr)[slot] = material;
    materialAlphaModes[slot] = (uint8_t)material.alphaMode;
    if (slot >= vulkan.currentMaterialOffset) { vulkan.currentMaterialOffset = slot + 1; }

    vulkan.materialCopy = (VkBufferCopy){
        .srcOffset = 0,
        .dstOffset = 0,
        .size = (VkDeviceSize)(vulkan.currentMaterialOffset * sizeof(VANILLA_GPUMaterial))
    };

    *outMaterialID = (MaterialID){ slot, ++vulkan.materialGenerations[slot] };
    return VANILLA_WINDOW_SUCCESS;
}

VANILLA_EXPORT VANILLA_WINDOW_RESULT VANILLA_destroyMaterial(MaterialID materialID) {
    if (didstart == false) { return VANILLA_WINDOW_ERR_DIDNT_START; }
    if (materialID.slot >= materialLimit) { return VANILLA_WINDOW_ERR_INVALID_MATERIAL; }
    
    if (vulkan.materialGenerations[materialID.slot] != materialID.generation) { return VANILLA_WINDOW_ERR_INVALID_MATERIAL; }
    VANILLA_GPUMaterial* material = &((VANILLA_GPUMaterial*)vulkan.mappedMaterialBufferPtr)[materialID.slot];

    material->flags1 = 0;
    material->flags2 = 0;
    material->flags3 = 0;
    materialAlphaModes[materialID.slot] = 0;

    vulkan.materialFreeSlots[vulkan.materialFreeSlotCount++] = materialID.slot;
    return VANILLA_WINDOW_SUCCESS;
}

VANILLA_EXPORT VANILLA_WINDOW_RESULT VANILLA_setMaterialComponent(EntityID entityID, MaterialID materialID, uint32_t entityFlags1, uint32_t entityFlags2, uint32_t entityFlags3) {
    if (didstart == false) { return VANILLA_WINDOW_ERR_DIDNT_START; }
    if (materialID.slot >= materialLimit) { return VANILLA_WINDOW_ERR_INVALID_MATERIAL; }
    if (vulkan.materialGenerations[materialID.slot] != materialID.generation) { return VANILLA_WINDOW_ERR_INVALID_MATERIAL; }

    if (!StandardComponent_has(&materialComponent, entityID)) {
        StandardComponent_RESULT result = StandardComponent_add(&materialComponent, entityID);
        if (result != STANDARDCOMPONENT_SUCCESS) { return VANILLA_WINDOW_ERR_ADD_MATERIAL_COMPONENT; }
    }

    VANILLA_GPUMaterialSlot* gpuMaterial = &((VANILLA_GPUMaterialSlot*)vulkan.mappedMaterialSlotBufferPtr)[entityID.slot];
    if (StandardComponent_setData(&materialComponent, entityID, &materialID) != STANDARDCOMPONENT_SUCCESS) { return VANILLA_WINDOW_ERR_ADD_MATERIAL_COMPONENT; }

    if (vulkan.maxmcslot < entityID.slot) { vulkan.maxmcslot = entityID.slot; }

    gpuMaterial->materialSlot = materialID.slot;
    gpuMaterial->flags1 = entityFlags1;
    gpuMaterial->flags2 = entityFlags2;
    gpuMaterial->flags3 = entityFlags3;

    /* NO_RENDER or the alpha mode (opaque / mask / blend list) may have changed: re-evaluate the entity's draw. */
    VANILLA_VK_indirectSetAlphaMode(entityID.slot, materialAlphaModes[materialID.slot]);
    VANILLA_VK_indirectRefresh(entityID.slot);

    return VANILLA_WINDOW_SUCCESS;
}

VANILLA_EXPORT VANILLA_WINDOW_RESULT VANILLA_removeMaterialComponent(EntityID entityID) {
    if (didstart == false) { return VANILLA_WINDOW_ERR_DIDNT_START; }
    if (!StandardComponent_has(&materialComponent, entityID)) { return VANILLA_WINDOW_ERR_HAS_NO_MATERIAL; }
    if (StandardComponent_remove(&materialComponent, entityID) != STANDARDCOMPONENT_SUCCESS) {
        return VANILLA_WINDOW_ERR_REMOVE_MATERIAL_COMPONENT;
    }

    VANILLA_GPUMaterialSlot* gpuMaterial = &((VANILLA_GPUMaterialSlot*)vulkan.mappedMaterialSlotBufferPtr)[entityID.slot];
    gpuMaterial->flags1 = 0;
    gpuMaterial->flags2 = 0;
    gpuMaterial->flags3 = 0;

    VANILLA_VK_indirectSetAlphaMode(entityID.slot, 0);
    VANILLA_VK_indirectRefresh(entityID.slot);

    return VANILLA_WINDOW_SUCCESS;
}

VANILLA_EXPORT VANILLA_WINDOW_RESULT VANILLA_getMaterialComponent(EntityID entityID, MaterialID* outMaterialID) {
    if (didstart == false) { return VANILLA_WINDOW_ERR_DIDNT_START; }
    if (outMaterialID == NULL) { return VANILLA_WINDOW_ERR_NULL_POINTER; }
    if (!StandardComponent_has(&materialComponent, entityID)) { return VANILLA_WINDOW_ERR_HAS_NO_MATERIAL; }

    MaterialID* material;
    if (StandardComponent_getData(&materialComponent, entityID, (void**)&material) != STANDARDCOMPONENT_SUCCESS) {
        return VANILLA_WINDOW_ERR_GET_MATERIAL_COMPONENT;
    }

    *outMaterialID = *material;

    return VANILLA_WINDOW_SUCCESS;
}

VANILLA_EXPORT bool VANILLA_hasMaterialComponent(EntityID entityID) {
    if (didstart == false) { return false; }
    return StandardComponent_has(&materialComponent, entityID);
}

VANILLA_EXPORT void VANILLA_setMaterialLimit(uint32_t limit) {
    if (didstart || didsetmateriallimit) { return; }
    if (limit <= 0) { limit = 1; }

    materialLimit = limit;
    didsetmateriallimit = true;
}