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

#include <stdio.h>
#include <stdint.h>
#include <math.h>
#include <string.h>

#include "vanilla_analyzer.h"
#include "vanilla_window_vulkan_utils.h"
#include "primitives.h"
#include "main.h"
#include "vanilla_window_threading.h"
#include "standardcomponent_utils.h"

typedef struct {
    const uint16_t* dirtyChunkList;
    const ANYCORE_DirtyChunk* dirtyChunks;
    const ANYCORE_SceneChunk* sceneChunks;

    void* transformChunks;
    mat4* gpuTransformTable;

    uint32_t dirtyChunkCount;
    uint32_t dim;
    uint32_t prec;
} VANILLA_TransformWork;

static VANILLA_TransformWork transformWork;

void VANILLA_analyzeTransformWorker(
    uint16_t workerIndex,
    uint16_t activeWorkers
) {
    for (
        uint32_t i = workerIndex;
        i < transformWork.dirtyChunkCount;
        i += activeWorkers
    ) {
        uint16_t dirtyChunkIdx = transformWork.dirtyChunkList[i];

        const ANYCORE_DirtyChunk* dc =
            &transformWork.dirtyChunks[dirtyChunkIdx];

        const ANYCORE_SceneChunk* sc =
            &transformWork.sceneChunks[dirtyChunkIdx];

        for (uint32_t d = 0; d < dc->dirtyCount; d++) {
            uint32_t slotIdx = (uint32_t)dc->dirtyList[d];

            uint32_t wordIdx = slotIdx >> 5;
            uint32_t bitIdx  = slotIdx & 31;

            int isLive = (sc->validFlags[wordIdx] & (1u << bitIdx)) ? 1 : 0;

            if (!isLive) {
                continue;
            }

            uint32_t absoluteIdx =
                ((uint32_t)dirtyChunkIdx * CHUNKSIZE) + slotIdx;

            mat4 renderMatrix = {0};
            renderMatrix.m[15] = 1.0f;

            float px = 0.0f, py = 0.0f, pz = 0.0f;
            float sx = 1.0f, sy = 1.0f, sz = 1.0f;
            float rx = 0.0f, ry = 0.0f, rz = 0.0f;

            void* tcArray = transformWork.transformChunks;

            if (transformWork.dim == 3 && transformWork.prec == 4) {
                ANYCORE_Transform3Df* tc =
                    ((ANYCORE_Transform3Df**)tcArray)[dirtyChunkIdx];

                ANYCORE_Transform3Df* transform = &tc[slotIdx];

                px = transform->posx;
                py = transform->posy;
                pz = transform->posz;

                sx = transform->scax;
                sy = transform->scay;
                sz = transform->scaz;

                rx = transform->rotx;
                ry = transform->roty;
                rz = transform->rotz;

            } else if (transformWork.dim == 3 && transformWork.prec == 8) {
                ANYCORE_Transform3Dd* tc =
                    ((ANYCORE_Transform3Dd**)tcArray)[dirtyChunkIdx];

                ANYCORE_Transform3Dd* transform = &tc[slotIdx];

                px = (float)transform->posx;
                py = (float)transform->posy;
                pz = (float)transform->posz;

                sx = (float)transform->scax;
                sy = (float)transform->scay;
                sz = (float)transform->scaz;

                rx = (float)transform->rotx;
                ry = (float)transform->roty;
                rz = (float)transform->rotz;

            } else if (transformWork.dim == 2 && transformWork.prec == 4) {
                ANYCORE_Transform2Df* tc =
                    ((ANYCORE_Transform2Df**)tcArray)[dirtyChunkIdx];

                ANYCORE_Transform2Df* transform = &tc[slotIdx];

                px = transform->posx;
                py = transform->posy;

                sx = transform->scax;
                sy = transform->scay;

                rz = transform->rotz;

            } else if (transformWork.dim == 2 && transformWork.prec == 8) {
                ANYCORE_Transform2Dd* tc =
                    ((ANYCORE_Transform2Dd**)tcArray)[dirtyChunkIdx];

                ANYCORE_Transform2Dd* transform = &tc[slotIdx];

                px = (float)transform->posx;
                py = (float)transform->posy;

                sx = (float)transform->scax;
                sy = (float)transform->scay;

                rz = (float)transform->rotz;
            }

            float cx = cosf(rx), sx_sin = sinf(rx);
            float cy = cosf(ry), sy_sin = sinf(ry);
            float cz = cosf(rz), sz_sin = sinf(rz);

            renderMatrix.m[0] = (cy * cz) * sx;
            renderMatrix.m[1] = (sx_sin * sy_sin * cz + cx * sz_sin) * sx;
            renderMatrix.m[2] = (-cx * sy_sin * cz + sx_sin * sz_sin) * sx;

            renderMatrix.m[4] = (-cy * sz_sin) * sy;
            renderMatrix.m[5] = (-sx_sin * sy_sin * sz_sin + cx * cz) * sy;
            renderMatrix.m[6] = (cx * sy_sin * sz_sin + sx_sin * cz) * sy;

            renderMatrix.m[8]  = sy_sin * sz;
            renderMatrix.m[9]  = -sx_sin * cy * sz;
            renderMatrix.m[10] = cx * cy * sz;

            renderMatrix.m[12] = px;
            renderMatrix.m[13] = py;
            renderMatrix.m[14] = pz;

            memcpy(
                &transformWork.gpuTransformTable[absoluteIdx],
                &renderMatrix,
                sizeof(mat4)
            );
        }
    }
}

void VANILLA_analyzeStream(void) {
    if (!stream) {
        printf("[VANILLA WINDOW] [ANALYZER] stream pointer NULL\n");
        return;
    }

    if (stream->chunkModelDirtyCount == 0 && stream->chunkDirtyCount == 0) {
        return;
    }

    uint32_t maxModelIndex = 0;

    /* MODEL DIRTY PROCESSING — MAIN THREAD */

    if (stream->chunkModelDirtyCount > 0) {
        for (uint32_t i = 0; i < stream->chunkModelDirtyCount; i++) {
            uint16_t dirtyModelChunkIdx = stream->chunkModelDirtyList[i];

            ANYCORE_ModelChunk* mc = &stream->modelChunks[dirtyModelChunkIdx];
            ANYCORE_ModelDirtyChunk* mdc = &stream->modelDirtyChunks[dirtyModelChunkIdx];

            for (uint32_t m = 0; m < mdc->modelDirtyCount; m++) {
                uint16_t slotIdx = mdc->modelDirtyList[m & CHUNKMASK];

                uint32_t modelID = (dirtyModelChunkIdx * CHUNKSIZE) + slotIdx;
                ANYCORE_Model* model = &mc->models[slotIdx];

                if (maxModelIndex < modelID) {
                    maxModelIndex = modelID;
                }

                ModelLookup* lookupTable = vulkan.mappedModelLookupPtr;

                if (vulkan.mappedModelLookupPtr == NULL) {
                    lookupTable = vulkan.mappedUploadModelLookupPtr;
                }

                if (model && model->vertices && model->indices) {
                    if (lookupTable[modelID].inuse == true) {
                        VANILLA_redoModel(
                            modelID,
                            model->vertices,
                            model->vertexCount,
                            model->indices,
                            model->indexCount,
                            &vertexLayout
                        );
                    } else {
                        VANILLA_ExtractAndPushModel(
                            modelID,
                            model->vertices,
                            model->vertexCount,
                            model->indices,
                            model->indexCount,
                            maxModelIndex,
                            &vertexLayout
                        );
                    }
                } else {
                    if (lookupTable[modelID].inuse == true) {
                        lookupTable[modelID].vertexCount = 0;
                        lookupTable[modelID].indexCount = 0;
                    }
                }
            }
        }
    }

    /* TRANSFORM WORK PREPARATION */

    if (stream->chunkDirtyCount > 0) {
        mat4* gpuTransformTable = (mat4*)vulkan.mappedTransformBufferPtr;

        if (vulkan.mappedTransformBufferPtr == NULL) {
            gpuTransformTable = (mat4*)vulkan.mappedUploadTransformBufferPtr;
        }

        uint32_t dim  = GET_DIMENSION(ANYCOREBuildID);
        uint32_t prec = GET_PRECISION(ANYCOREBuildID);

        transformWork.dirtyChunkList = stream->chunkDirtyList;
        transformWork.dirtyChunks = stream->dirtyChunks;
        transformWork.sceneChunks = stream->sceneChunks;
        transformWork.transformChunks = stream->transformChunks;
        transformWork.gpuTransformTable = gpuTransformTable;
        transformWork.dirtyChunkCount = stream->chunkDirtyCount;
        transformWork.dim = dim;
        transformWork.prec = prec;

        /* Start workers before processing indirect commands. */
        VANILLA_ThreadingStartTransforms(stream->chunkDirtyCount);

        uint32_t maxTransformIndex = 0;
        bool hasTransformEntries = false;

        uint32_t* aliveFlagsTable = vulkan.mappedAliveFlagsPtr;

        if (vulkan.mappedAliveFlagsPtr == NULL) {
            aliveFlagsTable = vulkan.mappedUploadAliveFlagsPtr;
        }

        uint32_t maxAliveChunk = 0;

        /* INDIRECT COMMANDS AND ENTITY DATA — MAIN THREAD */

        for (uint32_t i = 0; i < stream->chunkDirtyCount; i++) {
            uint16_t dirtyChunkIdx = stream->chunkDirtyList[i];

            const ANYCORE_DirtyChunk* dc =
                &stream->dirtyChunks[dirtyChunkIdx];

            const ANYCORE_SceneChunk* sc =
                &stream->sceneChunks[dirtyChunkIdx];

            const ANYCORE_InstanceChunk* ic =
                &stream->instanceChunks[dirtyChunkIdx];

            memcpy(
                aliveFlagsTable + ((uint32_t)dirtyChunkIdx * (CHUNKSIZE >> 5)),
                sc->validFlags,
                (CHUNKSIZE >> 5) * sizeof(uint32_t)
            );

            if (maxAliveChunk < dirtyChunkIdx) {
                maxAliveChunk = dirtyChunkIdx;
            }

            uint32_t localDirtyCount = dc->dirtyCount;

            for (uint32_t d = 0; d < localDirtyCount; d++) {
                uint32_t slotIdx = (uint32_t)dc->dirtyList[d];

                uint32_t absoluteIdx =
                    ((uint32_t)dirtyChunkIdx * CHUNKSIZE) + slotIdx;

                if (maxTransformIndex < absoluteIdx) {
                    maxTransformIndex = absoluteIdx;
                }

                hasTransformEntries = true;

                uint32_t wordIdx = slotIdx >> 5;
                uint32_t bitIdx  = slotIdx & 31;

                int isLive = (sc->validFlags[wordIdx] & (1u << bitIdx)) ? 1 : 0;

                VANILLA_GPUMaterialSlot* gpuMaterial =
                    &((VANILLA_GPUMaterialSlot*)vulkan.mappedMaterialSlotBufferPtr)[absoluteIdx];

                uint32_t modelIndex = ic->instances[slotIdx];

                vulkan.vramModelList[absoluteIdx] = modelIndex;

                /* Dense indirect lists: add / move / update / remove this entity's draw. */
                if (isLive) {
                    const uint16_t pipelineIndex =
                        vulkan.entityPipelineIndices[absoluteIdx];

                    const uint32_t renderEnabled =
                        ((gpuMaterial->flags1 & VANILLA_ENTITYFLAGS1_NO_RENDER) == 0u);

                    VkDrawIndexedIndirectCommand cmd;
                    VANILLA_VK_indirectBuildCommand(absoluteIdx, modelIndex, &cmd);

                    VANILLA_VK_indirectSet(absoluteIdx, renderEnabled ? (int32_t)pipelineIndex : -1, &cmd);
                } else {
                    VANILLA_VK_indirectSet(absoluteIdx, -1, NULL);
                }
            }
        }

        /* Wait until all transform workers finish. */
        VANILLA_ThreadingWaitTransforms();

        /* UPLOAD BUFFER COPY SIZE */

        if (vulkan.mappedUploadTransformBufferPtr != NULL) {
            VkBufferCopy transformBufferCopy = {
                .srcOffset = 0,
                .dstOffset = 0,
                .size = 0
            };

            if (hasTransformEntries) {
                transformBufferCopy.size =
                    ((VkDeviceSize)(maxTransformIndex + 1)) * sizeof(mat4);
            }

            vulkan.transformBufferCopy = transformBufferCopy;
        }

        if (vulkan.mappedUploadAliveFlagsPtr != NULL) {
            vulkan.aliveFlagsCopy = (VkBufferCopy){
                .srcOffset = 0,
                .dstOffset = 0,
                .size = ((VkDeviceSize)(maxAliveChunk + 1)) * (CHUNKSIZE >> 5) * sizeof(uint32_t)
            };
        }

        vulkan.vramActiveCount = stream->chunkCount * CHUNKSIZE;
    }
}
