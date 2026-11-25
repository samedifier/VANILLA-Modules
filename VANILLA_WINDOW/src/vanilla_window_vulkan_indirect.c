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
 * Dense indirect draw lists.
 *
 * glTF alpha modes: every pipeline owns THREE lists (L = pipeline * 3 + VANILLA_AlphaMode):
 *   opaque (capacity = all entities), mask and blend (smaller, VANILLA_setAlphaListCapacity).
 * An entity's list follows the alpha mode of its material (slotAlphaMode, set by setMaterialComponent).
 * Blend lists are sorted back to front each frame (entity origin distance to the camera).
 * Below, "pipeline p" in the older comments means "list L".
 *
 * Every pipeline owns a dense list of draw commands [0, count). A command's
 * firstInstance is still the entity slot, so every other buffer (transforms,
 * material slots, alive flags...) keeps its slot layout and the shaders are
 * unchanged. Draws read only `count` commands instead of the whole capacity.
 *
 * The lists live in a host-visible staging buffer (written by the CPU) and are
 * copied to a device-local buffer (read by the GPU) every frame, changed range only.
 *
 * Order:
 *   - pipelines with preserveDrawOrder keep slot order (meant for small pipelines
 *     such as overlapping transparent UI). Their list is a region inside the
 *     pipeline's segment that can grow at both ends: inserting at either end is O(1)
 *     (ANYCORE hands out descending slots, so new entities land at the front), and a
 *     middle insert/remove only shifts the shorter side;
 *   - all other pipelines append and swap-remove (O(1)).
 *
 * Indices stored in slotToDense / denseToSlot are absolute within the segment;
 * a pipeline draws [regionStart, regionStart + count).
 *
 * All calls happen on the app thread or the module's main loop thread, which never
 * run at the same time (ANYCORE frame order), so no locking is needed.
 */

#include "vanilla_window_vulkan_utils.h"
#include <stdint.h>
#include <stdbool.h>

#include "vanilla_window_camera.h"
#include <stdlib.h>

#define VANILLA_INDIRECT_NONE_PIPELINE UINT16_MAX
#define VANILLA_INDIRECT_MODES 3   /* lists per pipeline: opaque, mask, blend (VANILLA_AlphaMode) */

static uint32_t entityCapacity = 0;
static uint16_t pipelineCount  = 0;

static uint32_t  listCount    = 0;      /* pipelineCount * VANILLA_INDIRECT_MODES */
static uint32_t* listCapacity = NULL;   /* per list */
static uint32_t* listBase     = NULL;   /* per list: first entry of its segment */
static uint64_t  totalEntries = 0;      /* sum of the capacities */

static uint8_t*  slotAlphaMode = NULL;  /* per slot: VANILLA_AlphaMode of the entity's material */

/* Mask / blend list capacity per pipeline (opaque lists hold every entity). */
static uint32_t alphaListCapacity[2] = { 65536, 16384 };
static bool     didsetalphalistcapacity = false;
static bool     warnedAlphaListFull = false;

/* Blend sorting scratch */
typedef struct { float key; uint32_t index; } VANILLA_SortEntry;
static VANILLA_SortEntry*            sortEntries  = NULL;
static VkDrawIndexedIndirectCommand* sortCommands = NULL;
static uint32_t*                     sortSlots    = NULL;
static uint32_t                      sortCapacity = 0;

static VkBuffer       stagingBuffer = VK_NULL_HANDLE;
static VkDeviceMemory stagingMemory = VK_NULL_HANDLE;
static VkDrawIndexedIndirectCommand* commands = NULL;  /* staging, per pipeline: [p * capacity, + count) */

static uint32_t* denseToSlot  = NULL;   /* per list, same layout as commands          */
static uint32_t* slotToDense  = NULL;   /* per slot                                   */
static uint16_t* slotPipeline = NULL;   /* per slot: LIST index, VANILLA_INDIRECT_NONE_PIPELINE */

static uint32_t* counts      = NULL;    /* per list */
static uint32_t* regionStart = NULL;    /* per list, first used index in the segment */
static uint32_t* dirtyMin   = NULL;     /* per list, UINT32_MAX = clean */
static uint32_t* dirtyMax   = NULL;     /* per list, inclusive          */
static bool*     keepOrder  = NULL;     /* per list (preserveDrawOrder, opaque lists only) */

static VkDeviceSize commandBufferSize = 0;

/* --------------------------------------------------------- */
/* Helpers                                                    */
/* --------------------------------------------------------- */

static inline VkDrawIndexedIndirectCommand* pipelineCommands(uint16_t p) {
    return commands + listBase[p];
}

static inline uint32_t* pipelineSlots(uint16_t p) {
    return denseToSlot + listBase[p];
}

static inline void markDirty(uint16_t p, uint32_t first, uint32_t last) {
    if (first < dirtyMin[p]) { dirtyMin[p] = first; }
    if (dirtyMax[p] == UINT32_MAX || last > dirtyMax[p]) { dirtyMax[p] = last; }
}

/* Moves entries [from, from + n) by delta (+1 / -1) and fixes their slot mapping. */
static void shiftRange(uint16_t p, uint32_t from, uint32_t n, int delta) {
    if (n == 0) { return; }

    VkDrawIndexedIndirectCommand* cmds = pipelineCommands(p);
    uint32_t* slots = pipelineSlots(p);

    const uint32_t to = (uint32_t)((int64_t)from + delta);

    memmove(&cmds[to],  &cmds[from],  sizeof(VkDrawIndexedIndirectCommand) * n);
    memmove(&slots[to], &slots[from], sizeof(uint32_t) * n);

    for (uint32_t k = to; k < to + n; k++) { slotToDense[slots[k]] = k; }
    markDirty(p, to, to + n - 1);
}

static void removeFromPipeline(uint16_t p, uint32_t slot) {
    VkDrawIndexedIndirectCommand* cmds = pipelineCommands(p);
    uint32_t* slots = pipelineSlots(p);

    const uint32_t start = regionStart[p];
    const uint32_t i     = slotToDense[slot];
    const uint32_t last  = start + counts[p] - 1;

    if (keepOrder[p]) {
        const uint32_t before = i - start;   /* entries in front of i */
        const uint32_t after  = last - i;    /* entries behind i      */

        if (before < after) {
            shiftRange(p, start, before, +1);   /* close the hole from the front */
            regionStart[p]++;
        }
        else {
            shiftRange(p, i + 1, after, -1);    /* close the hole from the back  */
        }
    }
    else if (i != last) {
        /* Swap-remove: the last command fills the hole. */
        cmds[i]  = cmds[last];
        slots[i] = slots[last];
        slotToDense[slots[i]] = i;
        markDirty(p, i, i);
    }

    counts[p]--;
    if (counts[p] == 0 && keepOrder[p]) { regionStart[p] = listCapacity[p] / 2; }

    slotPipeline[slot] = VANILLA_INDIRECT_NONE_PIPELINE;
    slotToDense[slot]  = UINT32_MAX;
}

static bool addToPipeline(uint16_t p, uint32_t slot, const VkDrawIndexedIndirectCommand* cmd) {
    if (counts[p] >= listCapacity[p]) { return false; }

    VkDrawIndexedIndirectCommand* cmds = pipelineCommands(p);
    uint32_t* slots = pipelineSlots(p);

    uint32_t pos;

    if (keepOrder[p]) {
        uint32_t start = regionStart[p];
        const uint32_t count = counts[p];

        /* Binary search: position k keeps the region sorted by slot. */
        uint32_t lo = 0, hi = count;
        while (lo < hi) {
            uint32_t mid = lo + (hi - lo) / 2;
            if (slots[start + mid] < slot) { lo = mid + 1; } else { hi = mid; }
        }
        const uint32_t k = lo;

        bool roomFront = start > 0;
        bool roomBack  = start + count < listCapacity[p];
        const bool preferFront = (k < count - k);

        /*
         * The preferred end is full: move the whole region once so that all free
         * space is on that side (ANYCORE's descending slots keep hitting the front).
         * This only happens when an end is reached, so it stays amortized O(1).
         */
        if (preferFront && !roomFront && roomBack) {
            const uint32_t newStart = listCapacity[p] - count;
            shiftRange(p, start, count, (int)(newStart - start));
            regionStart[p] = start = newStart;
            roomFront = true;
            roomBack  = false;
        }
        else if (!preferFront && !roomBack && roomFront) {
            shiftRange(p, start, count, -(int)start);
            regionStart[p] = start = 0;
            roomFront = false;
            roomBack  = true;
        }

        if (roomFront && (preferFront || !roomBack)) {
            shiftRange(p, start, k, -1);        /* make room by moving the front part left */
            regionStart[p]--;
            pos = regionStart[p] + k;
        }
        else {
            shiftRange(p, start + k, count - k, +1);   /* move the back part right */
            pos = start + k;
        }
    }
    else {
        pos = counts[p];   /* regionStart is 0 for unordered pipelines */
    }

    cmds[pos]  = *cmd;
    slots[pos] = slot;

    slotToDense[slot]  = pos;
    slotPipeline[slot] = p;

    counts[p]++;
    markDirty(p, pos, pos);
    return true;
}

/* --------------------------------------------------------- */
/* Public (module-internal) API                               */
/* --------------------------------------------------------- */

/*
 * Puts the entity's command into `pipeline`, or removes it when pipeline < 0.
 * cmd may be NULL only when removing.
 */
void VANILLA_VK_indirectSet(uint32_t slot, int32_t pipeline, const VkDrawIndexedIndirectCommand* cmd) {
    if (commands == NULL || slot >= entityCapacity) { return; }

    const uint16_t current = slotPipeline[slot];

    if (pipeline < 0 || pipeline >= (int32_t)pipelineCount || cmd == NULL) {
        if (current != VANILLA_INDIRECT_NONE_PIPELINE) { removeFromPipeline(current, slot); }
        return;
    }

    uint16_t target = (uint16_t)((uint32_t)pipeline * VANILLA_INDIRECT_MODES + slotAlphaMode[slot]);

    if (current == target) {
        const uint32_t i = slotToDense[slot];
        pipelineCommands(target)[i] = *cmd;
        markDirty(target, i, i);
        return;
    }

    if (current != VANILLA_INDIRECT_NONE_PIPELINE) { removeFromPipeline(current, slot); }

    if (!addToPipeline(target, slot, cmd)) {
        /* Mask / blend list full: draw it as opaque rather than not at all. */
        if (!warnedAlphaListFull) {
            printf("[VANILLA WINDOW] mask/blend draw list full (setAlphaListCapacity): extra entities drawn opaque\n");
            warnedAlphaListFull = true;
        }
        target = (uint16_t)((uint32_t)pipeline * VANILLA_INDIRECT_MODES);
        addToPipeline(target, slot, cmd);
    }
}

/* Alpha mode of an entity's material; the caller refreshes the entity afterwards. */
void VANILLA_VK_indirectSetAlphaMode(uint32_t slot, uint32_t alphaMode) {
    if (slotAlphaMode == NULL || slot >= entityCapacity) { return; }
    slotAlphaMode[slot] = (uint8_t)(alphaMode < VANILLA_INDIRECT_MODES ? alphaMode : 0u);
}

/* Builds the entity's draw command from its model (lookup table). */
void VANILLA_VK_indirectBuildCommand(uint32_t slot, uint32_t modelIndex, VkDrawIndexedIndirectCommand* out) {
    const ModelLookup* lookupTable = vulkan.mappedModelLookupPtr != NULL
        ? (const ModelLookup*)vulkan.mappedModelLookupPtr
        : (const ModelLookup*)vulkan.mappedUploadModelLookupPtr;

    out->indexCount    = lookupTable[modelIndex].indexCount;
    out->instanceCount = 1;
    out->firstIndex    = lookupTable[modelIndex].firstIndex;
    out->vertexOffset  = (int32_t)lookupTable[modelIndex].firstVertex;
    out->firstInstance = slot;

    /* The culling needs each slot's model (its bounding sphere). */
    VANILLA_VK_cullSetSlotModel(slot, modelIndex);

    /* Skinned entities draw their own deformed copy (same index range). */
    const uint32_t skinned = VANILLA_VK_skinnedVertexOffset(slot);
    if (skinned != UINT32_MAX) { out->vertexOffset = (int32_t)skinned; }
}

/*
 * Re-evaluates one entity from its current state (alive, NO_RENDER, pipeline,
 * model). Used by setMaterialComponent / setPipelineIndex.
 */
void VANILLA_VK_indirectRefresh(uint32_t slot) {
    if (commands == NULL || slot >= entityCapacity) { return; }

    const uint32_t chunk = slot >> CHUNKSHIFT;
    const uint32_t local = slot & CHUNKMASK;

    bool live = false;
    if (stream != NULL && chunk < stream->chunkCount) {
        live = (stream->sceneChunks[chunk].validFlags[local >> 5] & (1u << (local & 31))) != 0;
    }

    const VANILLA_GPUMaterialSlot* gpuMaterial =
        &((const VANILLA_GPUMaterialSlot*)vulkan.mappedMaterialSlotBufferPtr)[slot];
    const bool renderEnabled = (gpuMaterial->flags1 & VANILLA_ENTITYFLAGS1_NO_RENDER) == 0u;

    if (!live || !renderEnabled) {
        VANILLA_VK_indirectSet(slot, -1, NULL);
        return;
    }

    /* Model may be unknown before the analyzer saw the entity; it fixes it the same frame. */
    const uint32_t modelIndex = (vulkan.vramModelList != NULL) ? vulkan.vramModelList[slot] : 0;

    VkDrawIndexedIndirectCommand cmd;
    VANILLA_VK_indirectBuildCommand(slot, modelIndex, &cmd);
    VANILLA_VK_indirectSet(slot, (int32_t)vulkan.entityPipelineIndices[slot], &cmd);
}

/* Byte offset of a list's first segment entry inside the device buffer. */
static VkDeviceSize segmentOffset(uint16_t list) {
    return (VkDeviceSize)listBase[list] * sizeof(VkDrawIndexedIndirectCommand);
}

/* Size of the device draw buffer (the culling's output buffer mirrors its layout). */
VkDeviceSize VANILLA_VK_indirectBufferSize(void) { return commandBufferSize; }

/* Draw count of one alpha-mode list of a pipeline (mode: VANILLA_AlphaMode). */
uint32_t VANILLA_VK_indirectCount(uint16_t pipeline, uint32_t mode) {
    if (counts == NULL || pipeline >= pipelineCount || mode >= VANILLA_INDIRECT_MODES) { return 0; }
    return counts[pipeline * VANILLA_INDIRECT_MODES + mode];
}

/* Byte offset of that list's first drawn command inside the device buffer. */
VkDeviceSize VANILLA_VK_indirectOffset(uint16_t pipeline, uint32_t mode) {
    if (regionStart == NULL || pipeline >= pipelineCount || mode >= VANILLA_INDIRECT_MODES) { return 0; }
    const uint16_t list = (uint16_t)(pipeline * VANILLA_INDIRECT_MODES + mode);
    return segmentOffset(list) + (VkDeviceSize)regionStart[list] * sizeof(VkDrawIndexedIndirectCommand);
}

/* --------------------------------------------------------- */
/* Blend lists: back to front                                 */
/* --------------------------------------------------------- */

static int compareFarFirst(const void* a, const void* b) {
    const float ka = ((const VANILLA_SortEntry*)a)->key;
    const float kb = ((const VANILLA_SortEntry*)b)->key;
    return (ka < kb) - (ka > kb);   /* larger distance first */
}

/* World position of an entity (its origin) from ANYCORE's transforms; 3D only. */
static bool entityPosition(uint32_t slot, float* x, float* y, float* z) {
    const uint32_t chunk = slot >> CHUNKSHIFT;
    const uint32_t local = slot & CHUNKMASK;
    if (stream == NULL || chunk >= stream->chunkCount) { return false; }

    const uint32_t prec = GET_PRECISION(ANYCOREBuildID);
    if (prec == 4) {
        const ANYCORE_Transform3Df* t = &((ANYCORE_Transform3Df**)stream->transformChunks)[chunk][local];
        *x = t->posx; *y = t->posy; *z = t->posz;
    }
    else {
        const ANYCORE_Transform3Dd* t = &((ANYCORE_Transform3Dd**)stream->transformChunks)[chunk][local];
        *x = (float)t->posx; *y = (float)t->posy; *z = (float)t->posz;
    }
    return true;
}

/* Re-orders a blend list far -> near; uploads only when the order changed. */
static void sortBlendList(uint16_t list) {
    const uint32_t count = counts[list];
    if (count < 2 || count > sortCapacity) { return; }

    VkDrawIndexedIndirectCommand* cmds = pipelineCommands(list) + regionStart[list];
    uint32_t* slots = pipelineSlots(list) + regionStart[list];

    for (uint32_t i = 0; i < count; i++) {
        float x = 0.0f, y = 0.0f, z = 0.0f;
        entityPosition(slots[i], &x, &y, &z);
        const float dx = x - camera.posx, dy = y - camera.posy, dz = z - camera.posz;
        sortEntries[i] = (VANILLA_SortEntry){ dx * dx + dy * dy + dz * dz, i };
    }

    qsort(sortEntries, count, sizeof(VANILLA_SortEntry), compareFarFirst);

    bool changed = false;
    for (uint32_t i = 0; i < count; i++) {
        if (sortEntries[i].index != i) { changed = true; break; }
    }
    if (!changed) { return; }

    for (uint32_t i = 0; i < count; i++) {
        sortCommands[i] = cmds[sortEntries[i].index];
        sortSlots[i]    = slots[sortEntries[i].index];
    }
    memcpy(cmds,  sortCommands, sizeof(VkDrawIndexedIndirectCommand) * count);
    memcpy(slots, sortSlots,    sizeof(uint32_t) * count);

    for (uint32_t i = 0; i < count; i++) { slotToDense[slots[i]] = regionStart[list] + i; }
    markDirty(list, regionStart[list], regionStart[list] + count - 1);
}

/* Copies the changed range of every list to the device buffer. Before the main barrier. */
void VANILLA_VK_recordIndirectUpload(VkCommandBuffer commandBuffer) {
    if (commands == NULL) { return; }

    /* Transparent lists follow the camera (3D). */
    if (GET_DIMENSION(ANYCOREBuildID) == 3) {
        for (uint16_t p = 0; p < pipelineCount; p++) {
            sortBlendList((uint16_t)(p * VANILLA_INDIRECT_MODES + VANILLA_ALPHAMODE_BLEND));
        }
    }

    VkBufferCopy regions[64];
    uint32_t regionCount = 0;

    for (uint16_t p = 0; p < listCount; p++) {
        if (dirtyMin[p] == UINT32_MAX) { continue; }

        /* Only the live region is ever drawn: clamp the dirty range to it. */
        if (counts[p] > 0) {
            const uint32_t liveFirst = regionStart[p];
            const uint32_t liveLast  = regionStart[p] + counts[p] - 1;

            uint32_t first = dirtyMin[p] < liveFirst ? liveFirst : dirtyMin[p];
            uint32_t last  = dirtyMax[p] > liveLast  ? liveLast  : dirtyMax[p];

            if (first <= last) {
                const VkDeviceSize offset = segmentOffset(p) + (VkDeviceSize)first * sizeof(VkDrawIndexedIndirectCommand);

                regions[regionCount++] = (VkBufferCopy){
                    .srcOffset = offset,
                    .dstOffset = offset,
                    .size = (VkDeviceSize)(last - first + 1) * sizeof(VkDrawIndexedIndirectCommand)
                };
            }
        }

        dirtyMin[p] = UINT32_MAX;
        dirtyMax[p] = UINT32_MAX;

        if (regionCount == 64) {
            vkCmdCopyBuffer(commandBuffer, stagingBuffer, vulkan.indirectCommandBuffer, regionCount, regions);
            regionCount = 0;
        }
    }

    if (regionCount > 0) {
        vkCmdCopyBuffer(commandBuffer, stagingBuffer, vulkan.indirectCommandBuffer, regionCount, regions);
    }
}

/* --------------------------------------------------------- */
/* Init / cleanup                                             */
/* --------------------------------------------------------- */

int VANILLA_VK_initIndirectBuffer(void) {
    entityCapacity    = stream->chunkLimit * CHUNKSIZE;
    pipelineCount     = vulkan.pipelineCount;
    listCount         = (uint32_t)pipelineCount * VANILLA_INDIRECT_MODES;

    listCapacity = VANILLA_mmap(sizeof(uint32_t) * listCount);
    listBase     = VANILLA_mmap(sizeof(uint32_t) * listCount);
    if (listCapacity == VANILLA_MAP_FAILED || listBase == VANILLA_MAP_FAILED) { goto fail; }

    totalEntries = 0;
    for (uint32_t l = 0; l < listCount; l++) {
        const uint32_t mode = l % VANILLA_INDIRECT_MODES;
        uint32_t capacity = entityCapacity;
        if (mode != VANILLA_ALPHAMODE_OPAQUE && alphaListCapacity[mode - 1] < capacity) { capacity = alphaListCapacity[mode - 1]; }

        listCapacity[l] = capacity;
        listBase[l]     = (uint32_t)totalEntries;
        totalEntries   += capacity;
    }

    commandBufferSize = (VkDeviceSize)totalEntries * sizeof(VkDrawIndexedIndirectCommand);

    /* Device-local draw buffer (read by the GPU every pass). */
    VkBufferCreateInfo deviceInfo = {
        .sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO,
        .size = commandBufferSize,
        .usage = VK_BUFFER_USAGE_INDIRECT_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT |
                 VK_BUFFER_USAGE_STORAGE_BUFFER_BIT,   /* read by the GPU culling */
        .sharingMode = VK_SHARING_MODE_EXCLUSIVE
    };

    if (vkCreateBuffer(vulkan.device, &deviceInfo, NULL, &vulkan.indirectCommandBuffer) != VK_SUCCESS) {
        vulkan.indirectCommandBuffer = VK_NULL_HANDLE;
        goto fail;
    }

    VkMemoryRequirements requirements;
    vkGetBufferMemoryRequirements(vulkan.device, vulkan.indirectCommandBuffer, &requirements);

    vulkan.indirectCommandBufferMemory = VANILLA_gmmap(&requirements, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
    if (vulkan.indirectCommandBufferMemory == VK_NULL_HANDLE) { goto fail; }
    if (vkBindBufferMemory(vulkan.device, vulkan.indirectCommandBuffer, vulkan.indirectCommandBufferMemory, 0) != VK_SUCCESS) { goto fail; }

    /* Host staging (written by the CPU). */
    VkBufferCreateInfo stagingInfo = {
        .sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO,
        .size = commandBufferSize,
        .usage = VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
        .sharingMode = VK_SHARING_MODE_EXCLUSIVE
    };

    if (vkCreateBuffer(vulkan.device, &stagingInfo, NULL, &stagingBuffer) != VK_SUCCESS) {
        stagingBuffer = VK_NULL_HANDLE;
        goto fail;
    }

    vkGetBufferMemoryRequirements(vulkan.device, stagingBuffer, &requirements);

    stagingMemory = VANILLA_gmmap(&requirements,
        VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT | VK_MEMORY_PROPERTY_HOST_CACHED_BIT);
    if (stagingMemory == VK_NULL_HANDLE) {
        stagingMemory = VANILLA_gmmap(&requirements, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
    }
    if (stagingMemory == VK_NULL_HANDLE) { goto fail; }
    if (vkBindBufferMemory(vulkan.device, stagingBuffer, stagingMemory, 0) != VK_SUCCESS) { goto fail; }

    if (vkMapMemory(vulkan.device, stagingMemory, 0, commandBufferSize, 0, (void**)&commands) != VK_SUCCESS) {
        commands = NULL;
        goto fail;
    }
    memset(commands, 0, commandBufferSize);

    /* Bookkeeping. */
    vulkan.entityPipelineIndices = VANILLA_mmap(sizeof(uint16_t) * entityCapacity);
    denseToSlot  = VANILLA_mmap(sizeof(uint32_t) * totalEntries);
    slotToDense  = VANILLA_mmap(sizeof(uint32_t) * entityCapacity);
    slotPipeline = VANILLA_mmap(sizeof(uint16_t) * entityCapacity);
    slotAlphaMode = VANILLA_mmap(sizeof(uint8_t) * entityCapacity);
    counts       = VANILLA_mmap(sizeof(uint32_t) * listCount);
    regionStart  = VANILLA_mmap(sizeof(uint32_t) * listCount);
    dirtyMin     = VANILLA_mmap(sizeof(uint32_t) * listCount);
    dirtyMax     = VANILLA_mmap(sizeof(uint32_t) * listCount);
    keepOrder    = VANILLA_mmap(sizeof(bool) * listCount);

    sortCapacity = alphaListCapacity[1] < entityCapacity ? alphaListCapacity[1] : entityCapacity;
    sortEntries  = VANILLA_mmap(sizeof(VANILLA_SortEntry) * sortCapacity);
    sortCommands = VANILLA_mmap(sizeof(VkDrawIndexedIndirectCommand) * sortCapacity);
    sortSlots    = VANILLA_mmap(sizeof(uint32_t) * sortCapacity);

    if (sortEntries == VANILLA_MAP_FAILED || sortCommands == VANILLA_MAP_FAILED || sortSlots == VANILLA_MAP_FAILED ||
        slotAlphaMode == VANILLA_MAP_FAILED ||
        vulkan.entityPipelineIndices == VANILLA_MAP_FAILED ||
        denseToSlot  == VANILLA_MAP_FAILED || slotToDense == VANILLA_MAP_FAILED ||
        slotPipeline == VANILLA_MAP_FAILED || counts      == VANILLA_MAP_FAILED ||
        dirtyMin     == VANILLA_MAP_FAILED || dirtyMax    == VANILLA_MAP_FAILED ||
        regionStart  == VANILLA_MAP_FAILED ||
        keepOrder    == VANILLA_MAP_FAILED) {
        goto fail;
    }

    memset(vulkan.entityPipelineIndices, 0, sizeof(uint16_t) * entityCapacity);
    memset(slotToDense, 0xFF, sizeof(uint32_t) * entityCapacity);
    memset(slotPipeline, 0xFF, sizeof(uint16_t) * entityCapacity);   /* = VANILLA_INDIRECT_NONE_PIPELINE */

    memset(slotAlphaMode, 0, sizeof(uint8_t) * entityCapacity);   /* opaque until a material says otherwise */

    for (uint32_t l = 0; l < listCount; l++) {
        const uint16_t p    = (uint16_t)(l / VANILLA_INDIRECT_MODES);
        const uint32_t mode = l % VANILLA_INDIRECT_MODES;

        counts[l]    = 0;
        dirtyMin[l]  = UINT32_MAX;
        dirtyMax[l]  = UINT32_MAX;
        keepOrder[l] = (mode == VANILLA_ALPHAMODE_OPAQUE) && (pipelineInfos != NULL) && pipelineInfos[p].preserveDrawOrder;

        /* Ordered lists start in the middle so they can grow both ways. */
        regionStart[l] = keepOrder[l] ? listCapacity[l] / 2 : 0;
    }

    return 0;

fail:
    printf("[VANILLA WINDOW] indirect buffer init failed\n");
    VANILLA_VK_cleanupIndirectBuffer();
    return 1;
}

#define VANILLA_INDIRECT_UNMAP(ptr, size)                           \
    do {                                                            \
        if ((ptr) != NULL && (void*)(ptr) != VANILLA_MAP_FAILED) {  \
            VANILLA_munmap((ptr), (size));                          \
        }                                                           \
        (ptr) = NULL;                                               \
    } while (0)

void VANILLA_VK_cleanupIndirectBuffer(void) {
    if (commands != NULL && stagingMemory != VK_NULL_HANDLE) {
        vkUnmapMemory(vulkan.device, stagingMemory);
    }
    commands = NULL;

    if (stagingBuffer != VK_NULL_HANDLE) {
        vkDestroyBuffer(vulkan.device, stagingBuffer, NULL);
        stagingBuffer = VK_NULL_HANDLE;
    }
    if (stagingMemory != VK_NULL_HANDLE) {
        VANILLA_gmunmap(stagingMemory);
        stagingMemory = VK_NULL_HANDLE;
    }
    if (vulkan.indirectCommandBuffer != VK_NULL_HANDLE) {
        vkDestroyBuffer(vulkan.device, vulkan.indirectCommandBuffer, NULL);
        vulkan.indirectCommandBuffer = VK_NULL_HANDLE;
    }
    if (vulkan.indirectCommandBufferMemory != VK_NULL_HANDLE) {
        VANILLA_gmunmap(vulkan.indirectCommandBufferMemory);
        vulkan.indirectCommandBufferMemory = VK_NULL_HANDLE;
    }

    VANILLA_INDIRECT_UNMAP(vulkan.entityPipelineIndices, sizeof(uint16_t) * entityCapacity);
    VANILLA_INDIRECT_UNMAP(denseToSlot,  sizeof(uint32_t) * totalEntries);
    VANILLA_INDIRECT_UNMAP(slotToDense,  sizeof(uint32_t) * entityCapacity);
    VANILLA_INDIRECT_UNMAP(slotPipeline, sizeof(uint16_t) * entityCapacity);
    VANILLA_INDIRECT_UNMAP(slotAlphaMode, sizeof(uint8_t) * entityCapacity);
    VANILLA_INDIRECT_UNMAP(counts,    sizeof(uint32_t) * listCount);
    VANILLA_INDIRECT_UNMAP(regionStart, sizeof(uint32_t) * listCount);
    VANILLA_INDIRECT_UNMAP(dirtyMin,  sizeof(uint32_t) * listCount);
    VANILLA_INDIRECT_UNMAP(dirtyMax,  sizeof(uint32_t) * listCount);
    VANILLA_INDIRECT_UNMAP(keepOrder, sizeof(bool) * listCount);
    VANILLA_INDIRECT_UNMAP(sortEntries,  sizeof(VANILLA_SortEntry) * sortCapacity);
    VANILLA_INDIRECT_UNMAP(sortCommands, sizeof(VkDrawIndexedIndirectCommand) * sortCapacity);
    VANILLA_INDIRECT_UNMAP(sortSlots,    sizeof(uint32_t) * sortCapacity);
    VANILLA_INDIRECT_UNMAP(listCapacity, sizeof(uint32_t) * listCount);
    VANILLA_INDIRECT_UNMAP(listBase,     sizeof(uint32_t) * listCount);

    vulkan.mappedIndirectCommands = NULL;
}

/* Mask / blend draw list capacity per pipeline (before start, once). Defaults 65536 / 16384. */
VANILLA_EXPORT void VANILLA_setAlphaListCapacity(uint32_t maskCapacity, uint32_t blendCapacity) {
    if (didstart || didsetalphalistcapacity) { return; }
    alphaListCapacity[0] = maskCapacity  > 0 ? maskCapacity  : 1;
    alphaListCapacity[1] = blendCapacity > 0 ? blendCapacity : 1;
    didsetalphalistcapacity = true;
}
