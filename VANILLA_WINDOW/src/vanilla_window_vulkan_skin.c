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

/*
 * Compute skinning (glTF skins) + morph targets (glTF mesh.targets / weights).
 *
 * Skins and morphs deform an entity into the same per-entity copy ("deformed entity"). One compute job per
 * changed entity applies the weighted morph deltas (only targets with non-zero weight) in mesh space first,
 * then skinning, as the glTF spec orders them. Morph-only entities are re-deformed when their weights change.
 *
 * A skin = per-vertex joints/weights of one model + one inverse bind matrix per joint (registerSkin).
 * A skinned entity = an entity of that model + the entities acting as its joints (setEntitySkin). Joints are
 * ordinary (usually NO_RENDER) entities moved by baked animation clips; the scene stays flat.
 *
 * Each frame one dispatch deforms the skinned entities that CHANGED (the entity or one of its joints is dirty in
 * ANYCORE this frame, or it was just set up) into the entity's own region of the
 * global vertex buffer (above vertexLimit, so model uploads never touch it). The entity's draw command points
 * at that region (VANILLA_VK_skinnedVertexOffset), so the main pass and every shadow pass draw the deformed
 * mesh without knowing about skinning.
 *
 * A deformed region keeps its content, so idle characters cost nothing. A frame the module did not draw
 * (minimized window) loses ANYCORE's dirty bits: the next drawn frame re-skins everything.
 *
 * GPU pools (device local): skin vertices (16 B each), inverse binds, joint slots, jobs.
 * Uploads at registration use a temporary staging buffer (load time); jobs are rebuilt and uploaded per frame.
 */

#include "vanilla_window_vulkan_utils.h"
#include "vanilla_window_skinning_spv.h"
#include <stdint.h>
#include <stdbool.h>
#include <math.h>

/* --------------------------------------------------------- */
/* Limits (before start)                                      */
/* --------------------------------------------------------- */

uint32_t skinLimit             = 256;      /* registered skins */
uint32_t skinnedEntityLimit    = 1024;     /* entities with a skin (<= 2048: jobs go through vkCmdUpdateBuffer) */
uint32_t skinnedVertexLimit    = 262144;   /* deformed vertices of all skinned entities (x 64 B) */
static uint32_t skinVertexPoolLimit   = 1u << 20;  /* skin data vertices of all skins (x 16 B) */
static uint32_t inverseBindPoolLimit  = 65536;     /* joints of all skins (x 64 B) */
static uint32_t jointSlotPoolLimit    = 262144;    /* joints of all skinned entities (x 4 B) */
static bool     didsetskinninglimits  = false;

uint32_t        morphLimit            = 256;       /* registered morphs (glTF mesh.targets of one model) */
static uint32_t deltaPoolLimit        = 1u << 20;  /* vec3 deltas of all morphs (x 12 B) */
static uint32_t weightPoolLimit       = 65536;     /* morph weights of all deformed entities */
static bool     didsetmorphlimits     = false;

#define VANILLA_SKIN_MAX_JOBS 2048   /* deformed entities (x 64 B jobs, uploaded in 64 KB vkCmdUpdateBuffer pieces) */

static uint32_t* customSkinningShader     = NULL;
static size_t    customSkinningShaderSize = 0;

/* --------------------------------------------------------- */
/* Range allocator (first fit, merging)                       */
/* --------------------------------------------------------- */

typedef struct {
    uint32_t* starts;
    uint32_t* counts;
    uint32_t  blockCount;
    uint32_t  blockCapacity;
} VANILLA_RangeAllocator;

static int rangeInit(VANILLA_RangeAllocator* a, uint32_t base, uint32_t total, uint32_t maxBlocks) {
    a->starts = VANILLA_mmap(sizeof(uint32_t) * maxBlocks);
    a->counts = VANILLA_mmap(sizeof(uint32_t) * maxBlocks);
    if (a->starts == VANILLA_MAP_FAILED || a->counts == VANILLA_MAP_FAILED) {
        a->starts = a->counts = NULL;
        return 1;
    }
    a->blockCapacity = maxBlocks;
    a->blockCount = 1;
    a->starts[0] = base;
    a->counts[0] = total;
    return 0;
}

static void rangeDestroy(VANILLA_RangeAllocator* a) {
    if (a->starts && a->starts != VANILLA_MAP_FAILED) { VANILLA_munmap(a->starts, sizeof(uint32_t) * a->blockCapacity); }
    if (a->counts && a->counts != VANILLA_MAP_FAILED) { VANILLA_munmap(a->counts, sizeof(uint32_t) * a->blockCapacity); }
    *a = (VANILLA_RangeAllocator){0};
}

static uint32_t rangeAlloc(VANILLA_RangeAllocator* a, uint32_t count) {
    if (count == 0) { return UINT32_MAX; }
    for (uint32_t i = 0; i < a->blockCount; i++) {
        if (a->counts[i] < count) { continue; }

        const uint32_t start = a->starts[i];
        a->starts[i] += count;
        a->counts[i] -= count;

        if (a->counts[i] == 0) {
            memmove(&a->starts[i], &a->starts[i + 1], sizeof(uint32_t) * (a->blockCount - i - 1));
            memmove(&a->counts[i], &a->counts[i + 1], sizeof(uint32_t) * (a->blockCount - i - 1));
            a->blockCount--;
        }
        return start;
    }
    return UINT32_MAX;
}

static void rangeFree(VANILLA_RangeAllocator* a, uint32_t start, uint32_t count) {
    if (count == 0 || start == UINT32_MAX) { return; }

    uint32_t i = 0;
    while (i < a->blockCount && a->starts[i] < start) { i++; }

    const bool mergePrev = i > 0 && a->starts[i - 1] + a->counts[i - 1] == start;
    const bool mergeNext = i < a->blockCount && start + count == a->starts[i];

    if (mergePrev && mergeNext) {
        a->counts[i - 1] += count + a->counts[i];
        memmove(&a->starts[i], &a->starts[i + 1], sizeof(uint32_t) * (a->blockCount - i - 1));
        memmove(&a->counts[i], &a->counts[i + 1], sizeof(uint32_t) * (a->blockCount - i - 1));
        a->blockCount--;
    }
    else if (mergePrev) {
        a->counts[i - 1] += count;
    }
    else if (mergeNext) {
        a->starts[i] = start;
        a->counts[i] += count;
    }
    else if (a->blockCount < a->blockCapacity) {
        memmove(&a->starts[i + 1], &a->starts[i], sizeof(uint32_t) * (a->blockCount - i));
        memmove(&a->counts[i + 1], &a->counts[i], sizeof(uint32_t) * (a->blockCount - i));
        a->starts[i] = start;
        a->counts[i] = count;
        a->blockCount++;
    }
    /* else: the free list is full and the range leaks (cannot happen with maxBlocks >= allocations + 1) */
}


/* --------------------------------------------------------- */
/* State                                                      */
/* --------------------------------------------------------- */

typedef struct {
    uint32_t model;              /* ANYCORE model index */
    uint32_t vertexCount;
    uint32_t skinVertexOffset;
    uint32_t jointCount;
    uint32_t inverseBindOffset;
    uint32_t users;              /* deformed entities using it */
    uint16_t generation;
    bool     used;
} VANILLA_Skin;

/* glTF mesh.targets of one model. Deltas are target-major per attribute: delta(t, v) = block[t * vertexCount + v]. */
typedef struct {
    uint32_t model;
    uint32_t vertexCount;
    uint32_t targetCount;
    uint32_t positionOffset;     /* into the delta pool (vec3 units), UINT32_MAX = not morphed */
    uint32_t normalOffset;
    uint32_t tangentOffset;
    uint32_t deltaCount;         /* allocated deltas (all present attributes) */
    float*   defaultWeights;     /* mesh.weights (targetCount), host */
    uint32_t users;
    uint16_t generation;
    bool     used;
} VANILLA_Morph;

/* An entity with a skin and / or morph targets, drawn from its own deformed copy. */
typedef struct {
    uint32_t entitySlot;
    uint32_t model;              /* the skin's / morph's model (both must agree) */
    uint32_t vertexCount;
    uint32_t region;             /* first vertex of the deformed copy */

    uint32_t skin;               /* index into skins, UINT32_MAX = none */
    uint32_t jointOffset;        /* into the joint slot pool */

    uint32_t morph;              /* index into morphs, UINT32_MAX = none */
    uint32_t weightOffset;       /* into weightsHost (morph targetCount floats) */

    bool     dirty;              /* re-deform even without ANYCORE dirt (just set up, weights changed, model not ready) */
} VANILLA_DeformedEntity;

/* std430 mirror of DeformJob in skinning.comp (64 B) */
typedef struct {
    uint32_t srcFirstVertex;
    uint32_t dstFirstVertex;
    uint32_t vertexCount;
    uint32_t flags;              /* VANILLA_DEFORM_SKIN | VANILLA_DEFORM_MORPH */

    uint32_t skinVertexOffset;
    uint32_t inverseBindOffset;
    uint32_t jointOffset;
    uint32_t entitySlot;

    uint32_t morphPositionOffset;   /* vec3 units, UINT32_MAX = none */
    uint32_t morphNormalOffset;
    uint32_t morphTangentOffset;
    uint32_t activeOffset;          /* into the active target list (target index, weight) */

    uint32_t activeCount;
    uint32_t _padding[3];
} VANILLA_DeformJob;

#define VANILLA_DEFORM_SKIN  1u
#define VANILLA_DEFORM_MORPH 2u

static bool deformActive = false;   /* skins or morphs enabled */

static VANILLA_Skin*  skins          = NULL;
static uint32_t*      skinFreeSlots  = NULL;
static uint32_t       skinFreeCount  = 0;

static VANILLA_Morph* morphs         = NULL;
static uint32_t*      morphFreeSlots = NULL;
static uint32_t       morphFreeCount = 0;

static VANILLA_DeformedEntity* deformed      = NULL;   /* dense */
static uint32_t                deformedCount = 0;
static uint32_t*               slotToDeformed = NULL;  /* per entity slot, UINT32_MAX = not deformed */
static uint32_t                slotCapacity   = 0;

static VANILLA_DeformJob*     jobs           = NULL;   /* host copy, rebuilt per frame */
static uint32_t*              activeHost     = NULL;   /* host copy of the active targets: (index, weight bits) pairs */
static uint32_t*              jointSlotsHost = NULL;   /* CPU mirror of the joint slot pool (dirty checks) */
static float*                 weightsHost    = NULL;   /* current morph weights of every deformed entity */
static uint64_t               lastSkinnedFrame = UINT64_MAX;
static bool                   warnedNotReady = false;

static VANILLA_RangeAllocator regionAlloc;
static VANILLA_RangeAllocator skinVertexAlloc;
static VANILLA_RangeAllocator inverseBindAlloc;
static VANILLA_RangeAllocator jointSlotAlloc;
static VANILLA_RangeAllocator deltaAlloc;
static VANILLA_RangeAllocator weightAlloc;

static VkBuffer       skinVertexBuffer  = VK_NULL_HANDLE;   static VkDeviceMemory skinVertexMemory  = VK_NULL_HANDLE;
static VkBuffer       inverseBindBuffer = VK_NULL_HANDLE;   static VkDeviceMemory inverseBindMemory = VK_NULL_HANDLE;
static VkBuffer       jointSlotBuffer   = VK_NULL_HANDLE;   static VkDeviceMemory jointSlotMemory   = VK_NULL_HANDLE;
static VkBuffer       jobBuffer         = VK_NULL_HANDLE;   static VkDeviceMemory jobMemory         = VK_NULL_HANDLE;
static VkBuffer       deltaBuffer       = VK_NULL_HANDLE;   static VkDeviceMemory deltaMemory       = VK_NULL_HANDLE;
static VkBuffer       activeBuffer      = VK_NULL_HANDLE;   static VkDeviceMemory activeMemory      = VK_NULL_HANDLE;

static VkDescriptorSetLayout skinSetLayout      = VK_NULL_HANDLE;
static VkDescriptorPool      skinDescriptorPool = VK_NULL_HANDLE;
static VkDescriptorSet       skinDescriptorSet  = VK_NULL_HANDLE;
static VkPipelineLayout      skinPipelineLayout = VK_NULL_HANDLE;
static VkPipeline            skinPipeline       = VK_NULL_HANDLE;

#define VANILLA_SKIN_BINDINGS 8

/* --------------------------------------------------------- */
/* Helpers                                                    */
/* --------------------------------------------------------- */

static int VANILLA_VK_createSkinBuffer(VkDeviceSize size, VkBuffer* outBuffer, VkDeviceMemory* outMemory) {
    VkBufferCreateInfo info = {
        .sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO,
        .size = size,
        .usage = VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT,
        .sharingMode = VK_SHARING_MODE_EXCLUSIVE
    };
    if (vkCreateBuffer(vulkan.device, &info, NULL, outBuffer) != VK_SUCCESS) {
        *outBuffer = VK_NULL_HANDLE;
        return 1;
    }

    VkMemoryRequirements requirements;
    vkGetBufferMemoryRequirements(vulkan.device, *outBuffer, &requirements);
    *outMemory = VANILLA_gmmap(&requirements, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
    if (*outMemory == VK_NULL_HANDLE) { return 1; }
    if (vkBindBufferMemory(vulkan.device, *outBuffer, *outMemory, 0) != VK_SUCCESS) { return 1; }
    return 0;
}

static void VANILLA_VK_destroySkinBuffer(VkBuffer* buffer, VkDeviceMemory* memory) {
    if (*buffer != VK_NULL_HANDLE) { vkDestroyBuffer(vulkan.device, *buffer, NULL); *buffer = VK_NULL_HANDLE; }
    if (*memory != VK_NULL_HANDLE) { VANILLA_gmunmap(*memory); *memory = VK_NULL_HANDLE; }
}

/* Load-time upload through a temporary staging buffer (blocks until done). */
static int VANILLA_VK_skinUpload(VkBuffer dst, VkDeviceSize offset, const void* data, VkDeviceSize size) {
    if (size == 0) { return 0; }

    VkBuffer staging = VK_NULL_HANDLE;
    VkDeviceMemory stagingMemory = VK_NULL_HANDLE;

    VkBufferCreateInfo info = {
        .sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO,
        .size = size,
        .usage = VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
        .sharingMode = VK_SHARING_MODE_EXCLUSIVE
    };
    if (vkCreateBuffer(vulkan.device, &info, NULL, &staging) != VK_SUCCESS) { return 1; }

    VkMemoryRequirements requirements;
    vkGetBufferMemoryRequirements(vulkan.device, staging, &requirements);
    stagingMemory = VANILLA_gmmap(&requirements, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);

    int failed = 1;
    void* mapped = NULL;

    if (stagingMemory != VK_NULL_HANDLE &&
        vkBindBufferMemory(vulkan.device, staging, stagingMemory, 0) == VK_SUCCESS &&
        vkMapMemory(vulkan.device, stagingMemory, 0, size, 0, &mapped) == VK_SUCCESS) {

        memcpy(mapped, data, (size_t)size);
        vkUnmapMemory(vulkan.device, stagingMemory);

        VkCommandBuffer commandBuffer = VANILLA_VK_beginSingleTimeCommands();
        if (commandBuffer != VK_NULL_HANDLE) {
            VkBufferCopy region = { .srcOffset = 0, .dstOffset = offset, .size = size };
            vkCmdCopyBuffer(commandBuffer, staging, dst, 1, &region);
            failed = VANILLA_VK_endSingleTimeCommands(commandBuffer);
        }
    }

    vkDestroyBuffer(vulkan.device, staging, NULL);
    if (stagingMemory != VK_NULL_HANDLE) { VANILLA_gmunmap(stagingMemory); }
    return failed;
}

/* vkCmdUpdateBuffer takes at most 65536 bytes per call. */
static void VANILLA_VK_updateBufferChunked(VkCommandBuffer commandBuffer, VkBuffer buffer, const void* data, VkDeviceSize size) {
    const uint8_t* bytes = (const uint8_t*)data;
    VkDeviceSize offset = 0;
    while (offset < size) {
        VkDeviceSize chunk = size - offset;
        if (chunk > 65536) { chunk = 65536; }
        vkCmdUpdateBuffer(commandBuffer, buffer, offset, chunk, bytes + offset);
        offset += chunk;
    }
}

static const ModelLookup* VANILLA_VK_skinLookup(void) {
    return vulkan.mappedModelLookupPtr != NULL
        ? (const ModelLookup*)vulkan.mappedModelLookupPtr
        : (const ModelLookup*)vulkan.mappedUploadModelLookupPtr;
}

static VANILLA_Skin* VANILLA_VK_getSkin(SkinID id) {
    if (skins == NULL || id.slot >= skinLimit) { return NULL; }
    VANILLA_Skin* skin = &skins[id.slot];
    if (!skin->used || skin->generation != id.generation) { return NULL; }
    return skin;
}

static VANILLA_Morph* VANILLA_VK_getMorph(MorphID id) {
    if (morphs == NULL || id.slot >= morphLimit) { return NULL; }
    VANILLA_Morph* morph = &morphs[id.slot];
    if (!morph->used || morph->generation != id.generation) { return NULL; }
    return morph;
}

/* --------------------------------------------------------- */
/* Pipeline                                                   */
/* --------------------------------------------------------- */

static int VANILLA_VK_createSkinPipeline(void) {
    VkDescriptorSetLayoutBinding bindings[VANILLA_SKIN_BINDINGS];
    for (uint32_t i = 0; i < VANILLA_SKIN_BINDINGS; i++) {
        bindings[i] = (VkDescriptorSetLayoutBinding){
            .binding = i,
            .descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
            .descriptorCount = 1,
            .stageFlags = VK_SHADER_STAGE_COMPUTE_BIT
        };
    }

    VkDescriptorSetLayoutCreateInfo layoutInfo = {
        .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO,
        .bindingCount = VANILLA_SKIN_BINDINGS,
        .pBindings = bindings
    };
    if (vkCreateDescriptorSetLayout(vulkan.device, &layoutInfo, NULL, &skinSetLayout) != VK_SUCCESS) {
        skinSetLayout = VK_NULL_HANDLE;
        return 1;
    }

    VkDescriptorPoolSize poolSize = { .type = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, .descriptorCount = VANILLA_SKIN_BINDINGS };
    VkDescriptorPoolCreateInfo poolInfo = {
        .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO,
        .maxSets = 1,
        .poolSizeCount = 1,
        .pPoolSizes = &poolSize
    };
    if (vkCreateDescriptorPool(vulkan.device, &poolInfo, NULL, &skinDescriptorPool) != VK_SUCCESS) {
        skinDescriptorPool = VK_NULL_HANDLE;
        return 1;
    }

    VkDescriptorSetAllocateInfo allocInfo = {
        .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO,
        .descriptorPool = skinDescriptorPool,
        .descriptorSetCount = 1,
        .pSetLayouts = &skinSetLayout
    };
    if (vkAllocateDescriptorSets(vulkan.device, &allocInfo, &skinDescriptorSet) != VK_SUCCESS) { return 1; }

    VkDescriptorBufferInfo infos[VANILLA_SKIN_BINDINGS] = {
        { vulkan.globalVertexBuffer,    0, VK_WHOLE_SIZE },
        { vulkan.globalTransformBuffer, 0, VK_WHOLE_SIZE },
        { skinVertexBuffer,             0, VK_WHOLE_SIZE },
        { inverseBindBuffer,            0, VK_WHOLE_SIZE },
        { jointSlotBuffer,              0, VK_WHOLE_SIZE },
        { jobBuffer,                    0, VK_WHOLE_SIZE },
        { deltaBuffer,                  0, VK_WHOLE_SIZE },
        { activeBuffer,                 0, VK_WHOLE_SIZE }
    };
    VkWriteDescriptorSet writes[VANILLA_SKIN_BINDINGS];
    for (uint32_t i = 0; i < VANILLA_SKIN_BINDINGS; i++) {
        writes[i] = (VkWriteDescriptorSet){
            .sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
            .dstSet = skinDescriptorSet,
            .dstBinding = i,
            .descriptorCount = 1,
            .descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
            .pBufferInfo = &infos[i]
        };
    }
    vkUpdateDescriptorSets(vulkan.device, VANILLA_SKIN_BINDINGS, writes, 0, NULL);

    VkPipelineLayoutCreateInfo pipelineLayoutInfo = {
        .sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO,
        .setLayoutCount = 1,
        .pSetLayouts = &skinSetLayout
    };
    if (vkCreatePipelineLayout(vulkan.device, &pipelineLayoutInfo, NULL, &skinPipelineLayout) != VK_SUCCESS) {
        skinPipelineLayout = VK_NULL_HANDLE;
        return 1;
    }

    VkShaderModuleCreateInfo shaderInfo = {
        .sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO,
        .codeSize = customSkinningShader ? customSkinningShaderSize : VANILLA_skinningSpv_len * sizeof(uint32_t),
        .pCode = customSkinningShader ? customSkinningShader : VANILLA_skinningSpv
    };

    VkShaderModule shaderModule = VK_NULL_HANDLE;
    VkResult moduleResult = vkCreateShaderModule(vulkan.device, &shaderInfo, NULL, &shaderModule);

    if (customSkinningShader != NULL) {
        VANILLA_munmap(customSkinningShader, customSkinningShaderSize);
        customSkinningShader = NULL;
        customSkinningShaderSize = 0;
    }
    if (moduleResult != VK_SUCCESS) { return 1; }

    VkComputePipelineCreateInfo pipelineInfo = {
        .sType = VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO,
        .stage = {
            .sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
            .stage = VK_SHADER_STAGE_COMPUTE_BIT,
            .module = shaderModule,
            .pName = "main"
        },
        .layout = skinPipelineLayout,
        .basePipelineIndex = -1
    };

    VkResult result = vkCreateComputePipelines(vulkan.device, VK_NULL_HANDLE, 1, &pipelineInfo, NULL, &skinPipeline);
    vkDestroyShaderModule(vulkan.device, shaderModule, NULL);
    if (result != VK_SUCCESS) {
        skinPipeline = VK_NULL_HANDLE;
        return 1;
    }
    return 0;
}

/* --------------------------------------------------------- */
/* Init / destroy                                             */
/* --------------------------------------------------------- */

#define VANILLA_SKIN_UNMAP(ptr, size)                               \
    do {                                                            \
        if ((ptr) != NULL && (void*)(ptr) != VANILLA_MAP_FAILED) {  \
            VANILLA_munmap((ptr), (size));                          \
        }                                                           \
        (ptr) = NULL;                                               \
    } while (0)

int VANILLA_VK_initSkinning(void) {
    deformActive = (skinLimit > 0 || morphLimit > 0) && skinnedEntityLimit > 0 && skinnedVertexLimit > 0;
    if (!deformActive) { return 0; }

    if (skinnedEntityLimit > VANILLA_SKIN_MAX_JOBS) { skinnedEntityLimit = VANILLA_SKIN_MAX_JOBS; }

    /* A zero-sized pool still gets one element so every buffer / allocator exists. */
    const uint32_t skinCap  = skinLimit  > 0 ? skinLimit  : 1;
    const uint32_t morphCap = morphLimit > 0 ? morphLimit : 1;
    if (deltaPoolLimit  == 0) { deltaPoolLimit  = 1; }
    if (weightPoolLimit == 0) { weightPoolLimit = 1; }

    slotCapacity = stream->chunkLimit * CHUNKSIZE;

    skins          = VANILLA_mmap(sizeof(VANILLA_Skin) * skinCap);
    skinFreeSlots  = VANILLA_mmap(sizeof(uint32_t) * skinCap);
    morphs         = VANILLA_mmap(sizeof(VANILLA_Morph) * morphCap);
    morphFreeSlots = VANILLA_mmap(sizeof(uint32_t) * morphCap);
    deformed       = VANILLA_mmap(sizeof(VANILLA_DeformedEntity) * skinnedEntityLimit);
    slotToDeformed = VANILLA_mmap(sizeof(uint32_t) * slotCapacity);
    jobs           = VANILLA_mmap(sizeof(VANILLA_DeformJob) * skinnedEntityLimit);
    jointSlotsHost = VANILLA_mmap(sizeof(uint32_t) * jointSlotPoolLimit);
    weightsHost    = VANILLA_mmap(sizeof(float) * weightPoolLimit);
    activeHost     = VANILLA_mmap(sizeof(uint32_t) * 2 * weightPoolLimit);

    if (skins == VANILLA_MAP_FAILED || skinFreeSlots == VANILLA_MAP_FAILED || morphs == VANILLA_MAP_FAILED ||
        morphFreeSlots == VANILLA_MAP_FAILED || deformed == VANILLA_MAP_FAILED || slotToDeformed == VANILLA_MAP_FAILED ||
        jobs == VANILLA_MAP_FAILED || jointSlotsHost == VANILLA_MAP_FAILED || weightsHost == VANILLA_MAP_FAILED ||
        activeHost == VANILLA_MAP_FAILED) { goto fail; }

    lastSkinnedFrame = UINT64_MAX;

    memset(skins, 0, sizeof(VANILLA_Skin) * skinCap);
    memset(morphs, 0, sizeof(VANILLA_Morph) * morphCap);
    memset(slotToDeformed, 0xFF, sizeof(uint32_t) * slotCapacity);
    for (uint32_t i = 0; i < skinLimit; i++)  { skinFreeSlots[i]  = skinLimit - 1 - i; }
    for (uint32_t i = 0; i < morphLimit; i++) { morphFreeSlots[i] = morphLimit - 1 - i; }
    skinFreeCount  = skinLimit;
    morphFreeCount = morphLimit;
    deformedCount  = 0;

    /* Deformed copies live above the model vertices (the global vertex buffer was sized for both). */
    if (rangeInit(&regionAlloc,      (uint32_t)vertexLimit, skinnedVertexLimit, skinnedEntityLimit + 1)) { goto fail; }
    if (rangeInit(&skinVertexAlloc,  0, skinVertexPoolLimit,  skinCap + 1))            { goto fail; }
    if (rangeInit(&inverseBindAlloc, 0, inverseBindPoolLimit, skinCap + 1))            { goto fail; }
    if (rangeInit(&jointSlotAlloc,   0, jointSlotPoolLimit,   skinnedEntityLimit + 1)) { goto fail; }
    if (rangeInit(&deltaAlloc,       0, deltaPoolLimit,       morphCap + 1))           { goto fail; }
    if (rangeInit(&weightAlloc,      0, weightPoolLimit,      skinnedEntityLimit + 1)) { goto fail; }

    if (VANILLA_VK_createSkinBuffer((VkDeviceSize)skinVertexPoolLimit * 16,  &skinVertexBuffer,  &skinVertexMemory))  { goto fail; }
    if (VANILLA_VK_createSkinBuffer((VkDeviceSize)inverseBindPoolLimit * 64, &inverseBindBuffer, &inverseBindMemory)) { goto fail; }
    if (VANILLA_VK_createSkinBuffer((VkDeviceSize)jointSlotPoolLimit * 4,    &jointSlotBuffer,   &jointSlotMemory))   { goto fail; }
    if (VANILLA_VK_createSkinBuffer((VkDeviceSize)skinnedEntityLimit * sizeof(VANILLA_DeformJob), &jobBuffer, &jobMemory)) { goto fail; }
    if (VANILLA_VK_createSkinBuffer((VkDeviceSize)deltaPoolLimit * 12,       &deltaBuffer,       &deltaMemory))       { goto fail; }
    if (VANILLA_VK_createSkinBuffer((VkDeviceSize)weightPoolLimit * 8,       &activeBuffer,      &activeMemory))      { goto fail; }

    if (VANILLA_VK_createSkinPipeline()) { goto fail; }
    return 0;

fail:
    printf("[VANILLA WINDOW] skinning / morph init failed\n");
    VANILLA_VK_destroySkinning();
    return 1;
}

void VANILLA_VK_destroySkinning(void) {
    if (customSkinningShader != NULL) {
        VANILLA_munmap(customSkinningShader, customSkinningShaderSize);
        customSkinningShader = NULL;
        customSkinningShaderSize = 0;
    }

    if (skinPipeline != VK_NULL_HANDLE)       { vkDestroyPipeline(vulkan.device, skinPipeline, NULL);             skinPipeline = VK_NULL_HANDLE; }
    if (skinPipelineLayout != VK_NULL_HANDLE) { vkDestroyPipelineLayout(vulkan.device, skinPipelineLayout, NULL); skinPipelineLayout = VK_NULL_HANDLE; }
    if (skinDescriptorPool != VK_NULL_HANDLE) {
        vkDestroyDescriptorPool(vulkan.device, skinDescriptorPool, NULL);
        skinDescriptorPool = VK_NULL_HANDLE;
        skinDescriptorSet = VK_NULL_HANDLE;
    }
    if (skinSetLayout != VK_NULL_HANDLE) { vkDestroyDescriptorSetLayout(vulkan.device, skinSetLayout, NULL); skinSetLayout = VK_NULL_HANDLE; }

    VANILLA_VK_destroySkinBuffer(&skinVertexBuffer,  &skinVertexMemory);
    VANILLA_VK_destroySkinBuffer(&inverseBindBuffer, &inverseBindMemory);
    VANILLA_VK_destroySkinBuffer(&jointSlotBuffer,   &jointSlotMemory);
    VANILLA_VK_destroySkinBuffer(&jobBuffer,         &jobMemory);
    VANILLA_VK_destroySkinBuffer(&deltaBuffer,       &deltaMemory);
    VANILLA_VK_destroySkinBuffer(&activeBuffer,      &activeMemory);

    rangeDestroy(&regionAlloc);
    rangeDestroy(&skinVertexAlloc);
    rangeDestroy(&inverseBindAlloc);
    rangeDestroy(&jointSlotAlloc);
    rangeDestroy(&deltaAlloc);
    rangeDestroy(&weightAlloc);

    const uint32_t skinCap  = skinLimit  > 0 ? skinLimit  : 1;
    const uint32_t morphCap = morphLimit > 0 ? morphLimit : 1;

    if (morphs != NULL && (void*)morphs != VANILLA_MAP_FAILED) {
        for (uint32_t i = 0; i < morphLimit; i++) {
            if (morphs[i].defaultWeights != NULL) {
                VANILLA_munmap(morphs[i].defaultWeights, sizeof(float) * morphs[i].targetCount);
                morphs[i].defaultWeights = NULL;
            }
        }
    }

    VANILLA_SKIN_UNMAP(skins,          sizeof(VANILLA_Skin) * skinCap);
    VANILLA_SKIN_UNMAP(skinFreeSlots,  sizeof(uint32_t) * skinCap);
    VANILLA_SKIN_UNMAP(morphs,         sizeof(VANILLA_Morph) * morphCap);
    VANILLA_SKIN_UNMAP(morphFreeSlots, sizeof(uint32_t) * morphCap);
    VANILLA_SKIN_UNMAP(deformed,       sizeof(VANILLA_DeformedEntity) * skinnedEntityLimit);
    VANILLA_SKIN_UNMAP(slotToDeformed, sizeof(uint32_t) * slotCapacity);
    VANILLA_SKIN_UNMAP(jobs,           sizeof(VANILLA_DeformJob) * skinnedEntityLimit);
    VANILLA_SKIN_UNMAP(jointSlotsHost, sizeof(uint32_t) * jointSlotPoolLimit);
    VANILLA_SKIN_UNMAP(weightsHost,    sizeof(float) * weightPoolLimit);
    VANILLA_SKIN_UNMAP(activeHost,     sizeof(uint32_t) * 2 * weightPoolLimit);

    skinFreeCount  = 0;
    morphFreeCount = 0;
    deformedCount  = 0;
    deformActive   = false;
}

/* --------------------------------------------------------- */
/* Draw command hook                                          */
/* --------------------------------------------------------- */

/* First vertex of the entity's deformed copy, UINT32_MAX when it has no skin / morph. */
uint32_t VANILLA_VK_skinnedVertexOffset(uint32_t slot) {
    if (slotToDeformed == NULL || slot >= slotCapacity) { return UINT32_MAX; }
    const uint32_t index = slotToDeformed[slot];
    return index == UINT32_MAX ? UINT32_MAX : deformed[index].region;
}

/* --------------------------------------------------------- */
/* Per frame                                                  */
/* --------------------------------------------------------- */

static void VANILLA_VK_skinBarrier(VkCommandBuffer commandBuffer,
                                   VkPipelineStageFlags2 srcStage, VkAccessFlags2 srcAccess,
                                   VkPipelineStageFlags2 dstStage, VkAccessFlags2 dstAccess) {
    VkMemoryBarrier2 barrier = {
        .sType = VK_STRUCTURE_TYPE_MEMORY_BARRIER_2,
        .srcStageMask = srcStage, .srcAccessMask = srcAccess,
        .dstStageMask = dstStage, .dstAccessMask = dstAccess
    };
    VkDependencyInfo dependencyInfo = {
        .sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO,
        .memoryBarrierCount = 1,
        .pMemoryBarriers = &barrier
    };
    vkCmdPipelineBarrier2(commandBuffer, &dependencyInfo);
}

/* Was this entity slot created / moved / changed in ANYCORE this frame? */
static bool VANILLA_VK_slotDirty(uint32_t slot) {
    const uint32_t chunk = slot >> CHUNKSHIFT;
    const uint32_t local = slot & CHUNKMASK;
    if (stream == NULL || chunk >= stream->chunkCount) { return false; }

    const uint32_t* dirties = stream->dirtyChunks[chunk].dirties;
    return dirties != NULL && (dirties[local >> 5] & (1u << (local & 31u))) != 0u;
}

/* Skinned: the entity (the result is in its space) or one of its joints moved. Morph-only: weights changes set de->dirty. */
static bool VANILLA_VK_deformedEntityChanged(const VANILLA_DeformedEntity* de) {
    if (de->skin == UINT32_MAX) { return false; }
    if (VANILLA_VK_slotDirty(de->entitySlot)) { return true; }

    const uint32_t* joints = jointSlotsHost + de->jointOffset;
    const uint32_t jointCount = skins[de->skin].jointCount;
    for (uint32_t j = 0; j < jointCount; j++) {
        if (VANILLA_VK_slotDirty(joints[j])) { return true; }
    }
    return false;
}

/* After the transform upload, before user compute and the shadow / main passes. */
void VANILLA_VK_recordSkinning(VkCommandBuffer commandBuffer) {
    if (!deformActive) { return; }

    /* ANYCORE's dirty bits only cover one frame: after a frame we did not record, re-deform everything. */
    const uint64_t frame = stream != NULL ? stream->frame : 0;
    const bool resync = (lastSkinnedFrame == UINT64_MAX) || (frame != lastSkinnedFrame + 1);
    lastSkinnedFrame = frame;

    if (deformedCount == 0) { return; }

    const ModelLookup* lookup = VANILLA_VK_skinLookup();
    uint32_t jobCount = 0;
    uint32_t activeCount = 0;
    uint32_t maxVertices = 0;

    for (uint32_t i = 0; i < deformedCount; i++) {
        VANILLA_DeformedEntity* de = &deformed[i];

        /* The model reaches the GPU a frame after addModel: wait for it, and for the entity to use it. */
        const uint32_t entityModel = vulkan.vramModelList != NULL ? vulkan.vramModelList[de->entitySlot] : UINT32_MAX;
        const bool ready = lookup != NULL &&
                           entityModel == de->model &&
                           lookup[de->model].inuse &&
                           lookup[de->model].vertexCount == de->vertexCount;
        if (!ready) {
            if (!warnedNotReady && lookup != NULL && entityModel == de->model &&
                lookup[de->model].inuse && lookup[de->model].vertexCount != de->vertexCount) {
                printf("[VANILLA WINDOW] skin / morph vertex count (%u) does not match its model (%u): not deformed\n",
                       de->vertexCount, lookup[de->model].vertexCount);
                warnedNotReady = true;
            }
            continue;   /* de->dirty stays set: deformed as soon as it is ready */
        }

        /* Nothing it depends on changed: last frame's deformed copy is still right. */
        if (!de->dirty && !resync && !VANILLA_VK_deformedEntityChanged(de)) { continue; }
        de->dirty = false;

        VANILLA_DeformJob job = {
            .srcFirstVertex      = lookup[de->model].firstVertex,
            .dstFirstVertex      = de->region,
            .vertexCount         = de->vertexCount,
            .entitySlot          = de->entitySlot,
            .morphPositionOffset = UINT32_MAX,
            .morphNormalOffset   = UINT32_MAX,
            .morphTangentOffset  = UINT32_MAX
        };

        if (de->skin != UINT32_MAX) {
            const VANILLA_Skin* skin = &skins[de->skin];
            job.flags            |= VANILLA_DEFORM_SKIN;
            job.skinVertexOffset  = skin->skinVertexOffset;
            job.inverseBindOffset = skin->inverseBindOffset;
            job.jointOffset       = de->jointOffset;
        }

        if (de->morph != UINT32_MAX) {
            const VANILLA_Morph* morph = &morphs[de->morph];
            const float* weights = weightsHost + de->weightOffset;

            /* Only targets with a non-zero weight cost anything on the GPU. */
            job.activeOffset = activeCount;
            for (uint32_t t = 0; t < morph->targetCount && activeCount < weightPoolLimit; t++) {
                if (weights[t] == 0.0f) { continue; }
                uint32_t bits;
                memcpy(&bits, &weights[t], sizeof(bits));
                activeHost[activeCount * 2 + 0] = t;
                activeHost[activeCount * 2 + 1] = bits;
                activeCount++;
            }
            job.activeCount = activeCount - job.activeOffset;

            job.flags              |= VANILLA_DEFORM_MORPH;
            job.morphPositionOffset = morph->positionOffset;
            job.morphNormalOffset   = morph->normalOffset;
            job.morphTangentOffset  = morph->tangentOffset;
        }

        jobs[jobCount++] = job;
        if (de->vertexCount > maxVertices) { maxVertices = de->vertexCount; }
    }

    if (jobCount == 0) { return; }

    const VkPipelineStageFlags2 cs = VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT;

    /* Last frame's vertex reads of the deformed regions, and the job / active buffers, before the new writes. */
    VANILLA_VK_skinBarrier(commandBuffer,
        VK_PIPELINE_STAGE_2_VERTEX_ATTRIBUTE_INPUT_BIT | cs, VK_ACCESS_2_NONE,
        VK_PIPELINE_STAGE_2_TRANSFER_BIT | cs,               VK_ACCESS_2_NONE);

    VANILLA_VK_updateBufferChunked(commandBuffer, jobBuffer, jobs, sizeof(VANILLA_DeformJob) * jobCount);
    if (activeCount > 0) {
        VANILLA_VK_updateBufferChunked(commandBuffer, activeBuffer, activeHost, sizeof(uint32_t) * 2 * activeCount);
    }

    /* Jobs + this frame's transform / vertex uploads -> deformation. */
    VANILLA_VK_skinBarrier(commandBuffer,
        VK_PIPELINE_STAGE_2_TRANSFER_BIT | VK_PIPELINE_STAGE_2_HOST_BIT,
        VK_ACCESS_2_TRANSFER_WRITE_BIT | VK_ACCESS_2_HOST_WRITE_BIT,
        cs, VK_ACCESS_2_SHADER_STORAGE_READ_BIT | VK_ACCESS_2_SHADER_STORAGE_WRITE_BIT);

    vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_COMPUTE, skinPipeline);
    vkCmdBindDescriptorSets(commandBuffer, VK_PIPELINE_BIND_POINT_COMPUTE, skinPipelineLayout, 0, 1, &skinDescriptorSet, 0, NULL);
    vkCmdDispatch(commandBuffer, (maxVertices + 63) / 64, jobCount, 1);

    /* Deformed vertices -> every later vertex fetch (shadow passes, main pass) and user compute. */
    VANILLA_VK_skinBarrier(commandBuffer,
        cs, VK_ACCESS_2_SHADER_STORAGE_WRITE_BIT,
        VK_PIPELINE_STAGE_2_VERTEX_ATTRIBUTE_INPUT_BIT | cs,
        VK_ACCESS_2_VERTEX_ATTRIBUTE_READ_BIT | VK_ACCESS_2_SHADER_STORAGE_READ_BIT);
}

/* --------------------------------------------------------- */
/* Deformed entities (shared by skins and morphs)             */
/* --------------------------------------------------------- */

/* Finds the entity's deformed record, or creates one (with its region) for `model`. */
static VANILLA_WINDOW_RESULT VANILLA_VK_acquireDeformed(uint32_t entitySlot, uint32_t model, uint32_t vertexCount, VANILLA_DeformedEntity** out) {
    const uint32_t index = slotToDeformed[entitySlot];
    if (index != UINT32_MAX) {
        VANILLA_DeformedEntity* de = &deformed[index];
        /* skin and morph of one entity deform the same model */
        if (de->model != model || de->vertexCount != vertexCount) { return VANILLA_WINDOW_ERR_INVALID_ARGUMENT; }
        *out = de;
        return VANILLA_WINDOW_SUCCESS;
    }

    if (deformedCount >= skinnedEntityLimit) { return VANILLA_WINDOW_ERR_CAPACITY_REACHED; }

    const uint32_t region = rangeAlloc(&regionAlloc, vertexCount);
    if (region == UINT32_MAX) { return VANILLA_WINDOW_ERR_CAPACITY_REACHED; }

    const uint32_t newIndex = deformedCount++;
    deformed[newIndex] = (VANILLA_DeformedEntity){
        .entitySlot  = entitySlot,
        .model       = model,
        .vertexCount = vertexCount,
        .region      = region,
        .skin        = UINT32_MAX,
        .jointOffset = UINT32_MAX,
        .morph       = UINT32_MAX,
        .weightOffset = UINT32_MAX,
        .dirty       = true
    };
    slotToDeformed[entitySlot] = newIndex;

    /* The entity's draw command now points at its deformed copy. */
    VANILLA_VK_indirectRefresh(entitySlot);

    *out = &deformed[newIndex];
    return VANILLA_WINDOW_SUCCESS;
}

/* Drops the record once neither a skin nor a morph is left (the entity draws its model again). */
static void VANILLA_VK_releaseDeformedIfEmpty(uint32_t entitySlot) {
    const uint32_t index = slotToDeformed[entitySlot];
    if (index == UINT32_MAX) { return; }

    VANILLA_DeformedEntity* de = &deformed[index];
    if (de->skin != UINT32_MAX || de->morph != UINT32_MAX) {
        de->dirty = true;   /* the remaining deformation alone */
        return;
    }

    rangeFree(&regionAlloc, de->region, de->vertexCount);

    /* Swap-remove from the dense list. */
    const uint32_t last = --deformedCount;
    if (index != last) {
        deformed[index] = deformed[last];
        slotToDeformed[deformed[index].entitySlot] = index;
    }
    slotToDeformed[entitySlot] = UINT32_MAX;

    VANILLA_VK_indirectRefresh(entitySlot);
}

/* --------------------------------------------------------- */
/* Skin API                                                   */
/* --------------------------------------------------------- */

VANILLA_EXPORT VANILLA_WINDOW_RESULT VANILLA_registerSkin(
    ModelID model,
    const VANILLA_SkinVertex* vertices, uint32_t vertexCount,
    const mat4* inverseBindMatrices, uint32_t jointCount,
    SkinID* outSkin)
{
    if (didstart == false) { return VANILLA_WINDOW_ERR_DIDNT_START; }
    if (!deformActive || skinLimit == 0) { return VANILLA_WINDOW_ERR_CAPACITY_REACHED; }
    if (vertices == NULL || inverseBindMatrices == NULL || outSkin == NULL) { return VANILLA_WINDOW_ERR_NULL_POINTER; }
    if (vertexCount == 0 || jointCount == 0 || jointCount > 65535) { return VANILLA_WINDOW_ERR_INVALID_ARGUMENT; }
    if (skinFreeCount == 0) { return VANILLA_WINDOW_ERR_CAPACITY_REACHED; }

    /* Pack: joints as 4 x u16 (2 words), weights as 4 x unorm16 (2 words). */
    uint32_t* packed = VANILLA_mmap(sizeof(uint32_t) * 4 * (size_t)vertexCount);
    if (packed == VANILLA_MAP_FAILED) { return VANILLA_WINDOW_ERR_ALLOC_FAILED; }

    VANILLA_WINDOW_RESULT result = VANILLA_WINDOW_SUCCESS;

    for (uint32_t v = 0; v < vertexCount && result == VANILLA_WINDOW_SUCCESS; v++) {
        const VANILLA_SkinVertex* sv = &vertices[v];
        uint16_t w16[4];

        for (int k = 0; k < 4; k++) {
            float w = sv->weights[k];
            if (!(w > 0.0f)) { w = 0.0f; }
            if (w > 1.0f) { w = 1.0f; }
            w16[k] = (uint16_t)lroundf(w * 65535.0f);

            if (w16[k] != 0 && sv->joints[k] >= jointCount) { result = VANILLA_WINDOW_ERR_INVALID_ARGUMENT; }
        }

        packed[v * 4 + 0] = (uint32_t)sv->joints[0] | ((uint32_t)sv->joints[1] << 16);
        packed[v * 4 + 1] = (uint32_t)sv->joints[2] | ((uint32_t)sv->joints[3] << 16);
        packed[v * 4 + 2] = (uint32_t)w16[0] | ((uint32_t)w16[1] << 16);
        packed[v * 4 + 3] = (uint32_t)w16[2] | ((uint32_t)w16[3] << 16);
    }

    uint32_t vertexOffset = UINT32_MAX, bindOffset = UINT32_MAX;

    if (result == VANILLA_WINDOW_SUCCESS) {
        vertexOffset = rangeAlloc(&skinVertexAlloc, vertexCount);
        bindOffset   = rangeAlloc(&inverseBindAlloc, jointCount);
        if (vertexOffset == UINT32_MAX || bindOffset == UINT32_MAX) { result = VANILLA_WINDOW_ERR_CAPACITY_REACHED; }
    }

    if (result == VANILLA_WINDOW_SUCCESS) {
        if (VANILLA_VK_skinUpload(skinVertexBuffer, (VkDeviceSize)vertexOffset * 16, packed, (VkDeviceSize)vertexCount * 16) ||
            VANILLA_VK_skinUpload(inverseBindBuffer, (VkDeviceSize)bindOffset * 64, inverseBindMatrices, (VkDeviceSize)jointCount * 64)) {
            result = VANILLA_WINDOW_ERR_ALLOC_FAILED;
        }
    }

    VANILLA_munmap(packed, sizeof(uint32_t) * 4 * (size_t)vertexCount);

    if (result != VANILLA_WINDOW_SUCCESS) {
        rangeFree(&skinVertexAlloc, vertexOffset, vertexCount);
        rangeFree(&inverseBindAlloc, bindOffset, jointCount);
        return result;
    }

    const uint32_t slot = skinFreeSlots[--skinFreeCount];
    VANILLA_Skin* skin = &skins[slot];

    skin->model             = (uint32_t)model;
    skin->vertexCount       = vertexCount;
    skin->skinVertexOffset  = vertexOffset;
    skin->jointCount        = jointCount;
    skin->inverseBindOffset = bindOffset;
    skin->users             = 0;
    skin->used              = true;

    *outSkin = (SkinID){ slot, ++skin->generation };
    return VANILLA_WINDOW_SUCCESS;
}

/* Fails while entities still use the skin (remove their skins first). */
VANILLA_EXPORT VANILLA_WINDOW_RESULT VANILLA_destroySkin(SkinID skinID) {
    if (didstart == false) { return VANILLA_WINDOW_ERR_DIDNT_START; }
    VANILLA_Skin* skin = VANILLA_VK_getSkin(skinID);
    if (skin == NULL) { return VANILLA_WINDOW_ERR_INVALID_SKIN; }
    if (skin->users > 0) { return VANILLA_WINDOW_ERR_INVALID_ARGUMENT; }

    rangeFree(&skinVertexAlloc, skin->skinVertexOffset, skin->vertexCount);
    rangeFree(&inverseBindAlloc, skin->inverseBindOffset, skin->jointCount);

    skin->used = false;
    skinFreeSlots[skinFreeCount++] = skinID.slot;
    return VANILLA_WINDOW_SUCCESS;
}

static void VANILLA_VK_dropSkin(VANILLA_DeformedEntity* de) {
    if (de->skin == UINT32_MAX) { return; }
    VANILLA_Skin* skin = &skins[de->skin];
    rangeFree(&jointSlotAlloc, de->jointOffset, skin->jointCount);
    if (skin->users > 0) { skin->users--; }
    de->skin = UINT32_MAX;
    de->jointOffset = UINT32_MAX;
}

/*
 * Deforms `entity` with `skin` every frame. jointEntities: the skin's jointCount entities, in glTF joint order
 * (copied). The entity must use the skin's model; its own transform still places the mesh.
 * Morph targets on the same entity (setEntityMorph) are applied before skinning.
 */
VANILLA_EXPORT VANILLA_WINDOW_RESULT VANILLA_setEntitySkin(EntityID entity, SkinID skinID, const EntityID* jointEntities) {
    if (didstart == false) { return VANILLA_WINDOW_ERR_DIDNT_START; }
    if (jointEntities == NULL) { return VANILLA_WINDOW_ERR_NULL_POINTER; }
    if (!deformActive || entity.slot >= slotCapacity) { return VANILLA_WINDOW_ERR_INVALID_ARGUMENT; }

    VANILLA_Skin* skin = VANILLA_VK_getSkin(skinID);
    if (skin == NULL) { return VANILLA_WINDOW_ERR_INVALID_SKIN; }

    /* A previous skin is replaced (a morph on the entity stays). */
    const uint32_t existing = slotToDeformed[entity.slot];
    if (existing != UINT32_MAX && deformed[existing].skin != UINT32_MAX) {
        VANILLA_VK_dropSkin(&deformed[existing]);
        VANILLA_VK_releaseDeformedIfEmpty(entity.slot);
    }

    const uint32_t jointOffset = rangeAlloc(&jointSlotAlloc, skin->jointCount);
    if (jointOffset == UINT32_MAX) { return VANILLA_WINDOW_ERR_CAPACITY_REACHED; }

    uint32_t* slots = VANILLA_mmap(sizeof(uint32_t) * skin->jointCount);
    if (slots == VANILLA_MAP_FAILED) {
        rangeFree(&jointSlotAlloc, jointOffset, skin->jointCount);
        return VANILLA_WINDOW_ERR_ALLOC_FAILED;
    }
    for (uint32_t j = 0; j < skin->jointCount; j++) { slots[j] = jointEntities[j].slot; }

    const int failed = VANILLA_VK_skinUpload(jointSlotBuffer, (VkDeviceSize)jointOffset * 4, slots, (VkDeviceSize)skin->jointCount * 4);
    if (failed) {
        VANILLA_munmap(slots, sizeof(uint32_t) * skin->jointCount);
        rangeFree(&jointSlotAlloc, jointOffset, skin->jointCount);
        return VANILLA_WINDOW_ERR_ALLOC_FAILED;
    }

    VANILLA_DeformedEntity* de = NULL;
    VANILLA_WINDOW_RESULT result = VANILLA_VK_acquireDeformed(entity.slot, skin->model, skin->vertexCount, &de);
    if (result != VANILLA_WINDOW_SUCCESS) {
        VANILLA_munmap(slots, sizeof(uint32_t) * skin->jointCount);
        rangeFree(&jointSlotAlloc, jointOffset, skin->jointCount);
        return result;
    }

    memcpy(jointSlotsHost + jointOffset, slots, sizeof(uint32_t) * skin->jointCount);
    VANILLA_munmap(slots, sizeof(uint32_t) * skin->jointCount);

    de->skin        = skinID.slot;
    de->jointOffset = jointOffset;
    de->dirty       = true;
    skin->users++;
    return VANILLA_WINDOW_SUCCESS;
}

VANILLA_EXPORT VANILLA_WINDOW_RESULT VANILLA_removeEntitySkin(EntityID entity) {
    if (didstart == false) { return VANILLA_WINDOW_ERR_DIDNT_START; }
    if (!deformActive || entity.slot >= slotCapacity) { return VANILLA_WINDOW_ERR_INVALID_ARGUMENT; }

    const uint32_t index = slotToDeformed[entity.slot];
    if (index == UINT32_MAX || deformed[index].skin == UINT32_MAX) { return VANILLA_WINDOW_ERR_INVALID_ARGUMENT; }

    VANILLA_VK_dropSkin(&deformed[index]);
    VANILLA_VK_releaseDeformedIfEmpty(entity.slot);
    return VANILLA_WINDOW_SUCCESS;
}

/* Before start, once. 0 skins = no skinning (morphs still work if setMorphLimits allows them). */
VANILLA_EXPORT void VANILLA_setSkinningLimits(uint32_t skins_, uint32_t skinnedEntities_, uint32_t skinnedVertices) {
    if (didstart || didsetskinninglimits) { return; }
    skinLimit          = skins_;
    skinnedEntityLimit = skinnedEntities_ > VANILLA_SKIN_MAX_JOBS ? VANILLA_SKIN_MAX_JOBS : skinnedEntities_;
    skinnedVertexLimit = skinnedVertices;
    jointSlotPoolLimit = skinnedEntityLimit * 256u;
    didsetskinninglimits = true;
}

/* Replaces the embedded skinning / morph compute shader (custom vertex layout). Copied. Before start, once. */
VANILLA_EXPORT void VANILLA_setSkinningShader(const uint32_t* code, size_t size) {
    if (didstart || customSkinningShader != NULL) { return; }
    if (code == NULL || size == 0 || (size % sizeof(uint32_t)) != 0) { return; }

    uint32_t* copy = VANILLA_mmap(size);
    if (copy == VANILLA_MAP_FAILED) { return; }
    memcpy(copy, code, size);

    customSkinningShader     = copy;
    customSkinningShaderSize = size;
}

/* --------------------------------------------------------- */
/* Morph target API (glTF mesh.targets / weights)             */
/* --------------------------------------------------------- */

static uint32_t VANILLA_VK_morphUploadBlock(const Vec3f* block, uint64_t count, uint32_t* allocated, VANILLA_WINDOW_RESULT* result) {
    if (block == NULL || *result != VANILLA_WINDOW_SUCCESS) { return UINT32_MAX; }

    const uint32_t offset = rangeAlloc(&deltaAlloc, (uint32_t)count);
    if (offset == UINT32_MAX) { *result = VANILLA_WINDOW_ERR_CAPACITY_REACHED; return UINT32_MAX; }
    *allocated += (uint32_t)count;

    if (VANILLA_VK_skinUpload(deltaBuffer, (VkDeviceSize)offset * 12, block, (VkDeviceSize)count * 12)) {
        *result = VANILLA_WINDOW_ERR_ALLOC_FAILED;
    }
    return offset;
}

static void VANILLA_VK_morphFreeBlocks(const VANILLA_Morph* morph) {
    const uint32_t count = morph->vertexCount * morph->targetCount;
    if (morph->positionOffset != UINT32_MAX) { rangeFree(&deltaAlloc, morph->positionOffset, count); }
    if (morph->normalOffset   != UINT32_MAX) { rangeFree(&deltaAlloc, morph->normalOffset,   count); }
    if (morph->tangentOffset  != UINT32_MAX) { rangeFree(&deltaAlloc, morph->tangentOffset,  count); }
}

VANILLA_EXPORT VANILLA_WINDOW_RESULT VANILLA_registerMorph(ModelID model, const VANILLA_MorphCreateInfo* info, MorphID* outMorph) {
    if (didstart == false) { return VANILLA_WINDOW_ERR_DIDNT_START; }
    if (!deformActive || morphLimit == 0) { return VANILLA_WINDOW_ERR_CAPACITY_REACHED; }
    if (info == NULL || outMorph == NULL) { return VANILLA_WINDOW_ERR_NULL_POINTER; }
    if (info->vertexCount == 0 || info->targetCount == 0) { return VANILLA_WINDOW_ERR_INVALID_ARGUMENT; }
    if (info->positions == NULL && info->normals == NULL && info->tangents == NULL) { return VANILLA_WINDOW_ERR_INVALID_ARGUMENT; }
    if (morphFreeCount == 0) { return VANILLA_WINDOW_ERR_CAPACITY_REACHED; }

    const uint64_t count = (uint64_t)info->vertexCount * info->targetCount;
    if (count > deltaPoolLimit) { return VANILLA_WINDOW_ERR_CAPACITY_REACHED; }

    float* defaults = VANILLA_mmap(sizeof(float) * info->targetCount);
    if (defaults == VANILLA_MAP_FAILED) { return VANILLA_WINDOW_ERR_ALLOC_FAILED; }
    for (uint32_t t = 0; t < info->targetCount; t++) {
        defaults[t] = info->defaultWeights != NULL ? info->defaultWeights[t] : 0.0f;
    }

    VANILLA_Morph morph = {
        .model          = (uint32_t)model,
        .vertexCount    = info->vertexCount,
        .targetCount    = info->targetCount,
        .positionOffset = UINT32_MAX,
        .normalOffset   = UINT32_MAX,
        .tangentOffset  = UINT32_MAX,
        .defaultWeights = defaults
    };

    VANILLA_WINDOW_RESULT result = VANILLA_WINDOW_SUCCESS;
    morph.positionOffset = VANILLA_VK_morphUploadBlock(info->positions, count, &morph.deltaCount, &result);
    morph.normalOffset   = VANILLA_VK_morphUploadBlock(info->normals,   count, &morph.deltaCount, &result);
    morph.tangentOffset  = VANILLA_VK_morphUploadBlock(info->tangents,  count, &morph.deltaCount, &result);

    if (result != VANILLA_WINDOW_SUCCESS) {
        VANILLA_VK_morphFreeBlocks(&morph);
        VANILLA_munmap(defaults, sizeof(float) * info->targetCount);
        return result;
    }

    const uint32_t slot = morphFreeSlots[--morphFreeCount];
    const uint16_t generation = morphs[slot].generation;

    morphs[slot] = morph;
    morphs[slot].generation = generation + 1;
    morphs[slot].used = true;

    *outMorph = (MorphID){ slot, morphs[slot].generation };
    return VANILLA_WINDOW_SUCCESS;
}

/* Fails while entities still use the morph (remove their morphs first). */
VANILLA_EXPORT VANILLA_WINDOW_RESULT VANILLA_destroyMorph(MorphID morphID) {
    if (didstart == false) { return VANILLA_WINDOW_ERR_DIDNT_START; }
    VANILLA_Morph* morph = VANILLA_VK_getMorph(morphID);
    if (morph == NULL) { return VANILLA_WINDOW_ERR_INVALID_MORPH; }
    if (morph->users > 0) { return VANILLA_WINDOW_ERR_INVALID_ARGUMENT; }

    VANILLA_VK_morphFreeBlocks(morph);
    VANILLA_munmap(morph->defaultWeights, sizeof(float) * morph->targetCount);
    morph->defaultWeights = NULL;

    morph->used = false;
    morphFreeSlots[morphFreeCount++] = morphID.slot;
    return VANILLA_WINDOW_SUCCESS;
}

static void VANILLA_VK_dropMorph(VANILLA_DeformedEntity* de) {
    if (de->morph == UINT32_MAX) { return; }
    VANILLA_Morph* morph = &morphs[de->morph];
    rangeFree(&weightAlloc, de->weightOffset, morph->targetCount);
    if (morph->users > 0) { morph->users--; }
    de->morph = UINT32_MAX;
    de->weightOffset = UINT32_MAX;
}

/*
 * Deforms `entity` with the morph's targets, weighted by its own weights (start: the morph's default weights).
 * The entity must use the morph's model. With a skin on the same entity, morphing runs first (glTF order).
 */
VANILLA_EXPORT VANILLA_WINDOW_RESULT VANILLA_setEntityMorph(EntityID entity, MorphID morphID) {
    if (didstart == false) { return VANILLA_WINDOW_ERR_DIDNT_START; }
    if (!deformActive || entity.slot >= slotCapacity) { return VANILLA_WINDOW_ERR_INVALID_ARGUMENT; }

    VANILLA_Morph* morph = VANILLA_VK_getMorph(morphID);
    if (morph == NULL) { return VANILLA_WINDOW_ERR_INVALID_MORPH; }

    const uint32_t existing = slotToDeformed[entity.slot];
    if (existing != UINT32_MAX && deformed[existing].morph != UINT32_MAX) {
        VANILLA_VK_dropMorph(&deformed[existing]);
        VANILLA_VK_releaseDeformedIfEmpty(entity.slot);
    }

    const uint32_t weightOffset = rangeAlloc(&weightAlloc, morph->targetCount);
    if (weightOffset == UINT32_MAX) { return VANILLA_WINDOW_ERR_CAPACITY_REACHED; }

    VANILLA_DeformedEntity* de = NULL;
    VANILLA_WINDOW_RESULT result = VANILLA_VK_acquireDeformed(entity.slot, morph->model, morph->vertexCount, &de);
    if (result != VANILLA_WINDOW_SUCCESS) {
        rangeFree(&weightAlloc, weightOffset, morph->targetCount);
        return result;
    }

    memcpy(weightsHost + weightOffset, morph->defaultWeights, sizeof(float) * morph->targetCount);

    de->morph        = morphID.slot;
    de->weightOffset = weightOffset;
    de->dirty        = true;
    morph->users++;
    return VANILLA_WINDOW_SUCCESS;
}

/* Sets the first `count` weights (copied; the rest keep their values). Unchanged weights cost nothing. */
VANILLA_EXPORT VANILLA_WINDOW_RESULT VANILLA_setEntityMorphWeights(EntityID entity, const float* weights, uint32_t count) {
    if (didstart == false) { return VANILLA_WINDOW_ERR_DIDNT_START; }
    if (weights == NULL) { return VANILLA_WINDOW_ERR_NULL_POINTER; }
    if (!deformActive || entity.slot >= slotCapacity) { return VANILLA_WINDOW_ERR_INVALID_ARGUMENT; }

    const uint32_t index = slotToDeformed[entity.slot];
    if (index == UINT32_MAX || deformed[index].morph == UINT32_MAX) { return VANILLA_WINDOW_ERR_INVALID_ARGUMENT; }

    VANILLA_DeformedEntity* de = &deformed[index];
    const VANILLA_Morph* morph = &morphs[de->morph];
    if (count > morph->targetCount) { return VANILLA_WINDOW_ERR_INVALID_ARGUMENT; }

    float* current = weightsHost + de->weightOffset;
    if (memcmp(current, weights, sizeof(float) * count) != 0) {
        memcpy(current, weights, sizeof(float) * count);
        de->dirty = true;
    }
    return VANILLA_WINDOW_SUCCESS;
}

VANILLA_EXPORT VANILLA_WINDOW_RESULT VANILLA_getEntityMorphWeights(EntityID entity, float* outWeights, uint32_t count) {
    if (didstart == false) { return VANILLA_WINDOW_ERR_DIDNT_START; }
    if (outWeights == NULL) { return VANILLA_WINDOW_ERR_NULL_POINTER; }
    if (!deformActive || entity.slot >= slotCapacity) { return VANILLA_WINDOW_ERR_INVALID_ARGUMENT; }

    const uint32_t index = slotToDeformed[entity.slot];
    if (index == UINT32_MAX || deformed[index].morph == UINT32_MAX) { return VANILLA_WINDOW_ERR_INVALID_ARGUMENT; }

    const VANILLA_DeformedEntity* de = &deformed[index];
    if (count > morphs[de->morph].targetCount) { return VANILLA_WINDOW_ERR_INVALID_ARGUMENT; }

    memcpy(outWeights, weightsHost + de->weightOffset, sizeof(float) * count);
    return VANILLA_WINDOW_SUCCESS;
}

VANILLA_EXPORT VANILLA_WINDOW_RESULT VANILLA_removeEntityMorph(EntityID entity) {
    if (didstart == false) { return VANILLA_WINDOW_ERR_DIDNT_START; }
    if (!deformActive || entity.slot >= slotCapacity) { return VANILLA_WINDOW_ERR_INVALID_ARGUMENT; }

    const uint32_t index = slotToDeformed[entity.slot];
    if (index == UINT32_MAX || deformed[index].morph == UINT32_MAX) { return VANILLA_WINDOW_ERR_INVALID_ARGUMENT; }

    VANILLA_VK_dropMorph(&deformed[index]);
    VANILLA_VK_releaseDeformedIfEmpty(entity.slot);
    return VANILLA_WINDOW_SUCCESS;
}

/*
 * Before start, once. morphs: registered morphs (default 256; 0 = no morph targets).
 * deltas: vec3 deltas of all morphs (vertexCount x targetCount per morphed attribute, x 12 B; default 1M = 12 MB).
 * weights: morph weights of all deformed entities together (x 4 B host + 8 B GPU; default 65536).
 * Deformed entities / vertices are shared with skinning (setSkinningLimits).
 */
VANILLA_EXPORT void VANILLA_setMorphLimits(uint32_t morphs_, uint32_t deltas, uint32_t weights) {
    if (didstart || didsetmorphlimits) { return; }
    morphLimit      = morphs_;
    deltaPoolLimit  = deltas;
    weightPoolLimit = weights;
    didsetmorphlimits = true;
}
