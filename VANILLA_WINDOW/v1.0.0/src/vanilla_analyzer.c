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

#include "vanilla_analyzer.h"
#include "vanilla_window_vulkan_utils.h"
#include "primitives.h"
#include "main.h"

#include "standardcomponent_utils.h"

void VANILLA_analyzeStream(void) {
    if (!stream) {
        printf("[ANALYZER] HATA: Stream pointer NULL!\n");
        return;
    }
    if (stream->chunkModelDirtyCount == 0 && stream->chunkDirtyCount == 0) {
        return; 
    }

    uint32_t maxModelIndex = 0;
    if (stream->chunkModelDirtyCount > 0) {
        for (uint32_t i = 0; i < stream->chunkModelDirtyCount; i++) {
            uint16_t dirtyModelChunkIdx = stream->chunkModelDirtyList[i];
            
            ANYCORE_ModelChunk* mc = &stream->modelChunks[dirtyModelChunkIdx];
            ANYCORE_ModelDirtyChunk* mdc = &stream->modelDirtyChunks[dirtyModelChunkIdx];

            for (uint32_t m = 0; m < mdc->modelDirtyCount; m++) {
                uint16_t slotIdx = mdc->modelDirtyList[m & CHUNKMASK];
                
                uint32_t modelID = (dirtyModelChunkIdx * CHUNKSIZE) + slotIdx;
                ANYCORE_Model* model = &mc->models[slotIdx];

                if (maxModelIndex < modelID) { maxModelIndex = modelID; }

                ModelLookup* lookupTable = vulkan.mappedModelLookupPtr;
                if(vulkan.mappedModelLookupPtr == NULL) {
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
                    }
                    else {
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
                }
                else {
                    if (lookupTable[modelID].inuse == true) {
                        lookupTable[modelID].vertexCount = 0;
                        lookupTable[modelID].indexCount  = 0;
                    }
                }
            }
        }
    }

    if (stream->chunkDirtyCount > 0) {
        mat4* gpuTransformTable = (mat4*)vulkan.mappedTransformBufferPtr;
        if (vulkan.mappedTransformBufferPtr == NULL) {
            gpuTransformTable = (mat4*)vulkan.mappedUploadTransformBufferPtr;
        }
        VkDrawIndexedIndirectCommand* gpuIndirectCommands = (VkDrawIndexedIndirectCommand*)vulkan.mappedIndirectCommands;
        
        uint32_t dim  = GET_DIMENSION(ANYCOREBuildID);
        uint32_t prec = GET_PRECISION(ANYCOREBuildID);
        void* tcArray = stream->transformChunks;

        uint32_t maxTransformIndex = 0;

        for (uint32_t i = 0; i < stream->chunkDirtyCount; i++) {
            uint16_t dirtyChunkIdx = stream->chunkDirtyList[i];
            
            const ANYCORE_DirtyChunk* dc = &stream->dirtyChunks[dirtyChunkIdx];
            const ANYCORE_SceneChunk* sc = &stream->sceneChunks[dirtyChunkIdx];
            const ANYCORE_InstanceChunk* ic = &stream->instanceChunks[dirtyChunkIdx]; 

            uint32_t localDirtyCount = dc->dirtyCount;

            StandardComponent_Chunk* scc = &materialComponent.chunks[dirtyChunkIdx];

            for (uint32_t d = 0; d < localDirtyCount; d++) {
                uint32_t slotIdx = (uint32_t)dc->dirtyList[d];
                
                uint32_t absoluteIdx = (dirtyChunkIdx * CHUNKSIZE) + slotIdx;
                if (maxTransformIndex < absoluteIdx) { maxTransformIndex = absoluteIdx; }

                uint32_t wordIdx = slotIdx >> 5;
                uint32_t bitIdx  = slotIdx & 31;
                
                int isLive = (sc->validFlags[wordIdx] & (1u << bitIdx)) ? 1 : 0;

                if (((dc->createFlags[wordIdx] & (1u << bitIdx)) ? 1 : 0) && (scc->generations[slotIdx] != sc->generations[slotIdx])) {
                    StandardComponent_remove(&materialComponent, (EntityID){ slotIdx, sc->generations[slotIdx] });
                }

                ModelLookup* lookupTable = vulkan.mappedModelLookupPtr;
                if(vulkan.mappedModelLookupPtr == NULL) {
                    lookupTable = vulkan.mappedUploadModelLookupPtr;
                }
                uint32_t modelIndex = ic->instances[slotIdx];

                vulkan.vramModelList[absoluteIdx] = modelIndex;

                if (isLive) {
                    mat4 renderMatrix = {0};
                    renderMatrix.m[15] = 1.0f;

                    float px = 0.0f, py = 0.0f, pz = 0.0f;
                    float sx = 1.0f, sy = 1.0f, sz = 1.0f;
                    float rx = 0.0f, ry = 0.0f, rz = 0.0f;

                    if (dim == 3 && prec == 4) {
                        ANYCORE_TransformChunk3Df* tc = &((ANYCORE_TransformChunk3Df*)tcArray)[dirtyChunkIdx];
                        px = tc->posx[slotIdx]; py = tc->posy[slotIdx]; pz = tc->posz[slotIdx];
                        sx = tc->scax[slotIdx]; sy = tc->scay[slotIdx]; sz = tc->scaz[slotIdx];
                        rx = tc->rotx[slotIdx]; ry = tc->roty[slotIdx]; rz = tc->rotz[slotIdx];
                    }
                    else if (dim == 3 && prec == 8) {
                        ANYCORE_TransformChunk3Dd* tc = &((ANYCORE_TransformChunk3Dd*)tcArray)[dirtyChunkIdx];
                        px = (float)tc->posx[slotIdx]; py = (float)tc->posy[slotIdx]; pz = (float)tc->posz[slotIdx];
                        sx = (float)tc->scax[slotIdx]; sy = (float)tc->scay[slotIdx]; sz = (float)tc->scaz[slotIdx];
                        rx = (float)tc->rotx[slotIdx]; ry = (float)tc->roty[slotIdx]; rz = (float)tc->rotz[slotIdx];
                    }
                    else if (dim == 2 && prec == 4) {
                        ANYCORE_TransformChunk2Df* tc = &((ANYCORE_TransformChunk2Df*)tcArray)[dirtyChunkIdx];
                        px = tc->posx[slotIdx]; py = tc->posy[slotIdx];
                        sx = tc->scax[slotIdx]; sy = tc->scay[slotIdx];
                        rz = tc->rotz[slotIdx];
                    }
                    else if (dim == 2 && prec == 8) {
                        ANYCORE_TransformChunk2Dd* tc = &((ANYCORE_TransformChunk2Dd*)tcArray)[dirtyChunkIdx];
                        px = (float)tc->posx[slotIdx]; py = (float)tc->posy[slotIdx];
                        sx = (float)tc->scax[slotIdx]; sy = (float)tc->scay[slotIdx];
                        rz = (float)tc->rotz[slotIdx];
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

                    gpuIndirectCommands[absoluteIdx].indexCount    = lookupTable[modelIndex].indexCount;
                    gpuIndirectCommands[absoluteIdx].instanceCount = 1;
                    gpuIndirectCommands[absoluteIdx].firstIndex    = lookupTable[modelIndex].firstIndex;
                    gpuIndirectCommands[absoluteIdx].vertexOffset  = (int32_t)lookupTable[modelIndex].firstVertex;
                    gpuIndirectCommands[absoluteIdx].firstInstance = absoluteIdx;

                    memcpy(&gpuTransformTable[absoluteIdx], &renderMatrix, sizeof(mat4));
                } else {
                    gpuIndirectCommands[absoluteIdx].instanceCount = 0;
                }

                if (vulkan.mappedUploadTransformBufferPtr != NULL) {
                    VkBufferCopy transformBufferCopy = {
                        .srcOffset = 0,
                        .dstOffset = 0,
                        .size = 0
                    };

                    if (stream->chunkDirtyCount > 0) {
                        transformBufferCopy.size = ((VkDeviceSize)(maxTransformIndex + 1)) * sizeof(mat4);
                    }
                    vulkan.transformBufferCopy = transformBufferCopy;
                }
            }
        }

        vulkan.vramActiveCount = stream->chunkCount * CHUNKSIZE;
    }
}