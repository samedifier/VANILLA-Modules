/*
 * ANYCORE
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

/*
 * DO NOT MODIFY THIS FILE.
 *
 * primitives.h is the shared source of common data types used by
 * ANYCORE, modules and behaviours.
 *
 * Modules and behaviours must use the same primitives.h provided
 * with this ANYCORE release.
 */

#ifndef PRIMITIVES_H
#define PRIMITIVES_H

#include "stdint.h"
#include "stdbool.h"
#include "pthread.h"

typedef uint32_t ID;
typedef uint16_t ID16;
typedef ID       ID32;

typedef struct { ID32 slot; ID16 generation; } EntityID;
typedef ID32 ModelID;

typedef struct { float x; float y; } Vec2f;
typedef struct { float x; float y; float z; } Vec3f;
typedef struct { float x; float y; float z; float w; } Vec4f;

typedef struct { double x; double y; } Vec2d;
typedef struct { double x; double y; double z; } Vec3d;
typedef struct { double x; double y; double z; double w; } Vec4d;

typedef struct {
    uint32_t* validFlags;
    uint32_t* lockFlags;
    uint16_t* generations;
    uint16_t* freeSlots;
} ANYCORE_SceneChunk;

typedef struct {
    float posx;
    float posy;
    float rotz;
    float scax;
    float scay;
} ANYCORE_Transform2Df;

typedef struct {
    float posx;
    float posy;
    float posz;

    float rotx;
    float roty;
    float rotz;

    float scax;
    float scay;
    float scaz;
} ANYCORE_Transform3Df;

typedef struct {
    double posx;
    double posy;
    double rotz;
    double scax;
    double scay;
} ANYCORE_Transform2Dd;

typedef struct {
    double posx;
    double posy;
    double posz;

    double rotx;
    double roty;
    double rotz;

    double scax;
    double scay;
    double scaz;
} ANYCORE_Transform3Dd;

typedef struct {
    uint32_t* dirties;
    uint32_t* createFlags;
    uint16_t* dirtyList;
    uint32_t  dirtyCount;
} ANYCORE_DirtyChunk;

typedef struct {
    uint32_t* instances;
} ANYCORE_InstanceChunk;

typedef struct {
    void*  vertices;
    uint32_t  vertexCount;
    uint32_t* indices;
    uint32_t  indexCount;
} ANYCORE_Model;

typedef struct {
    ANYCORE_Model* models;
} ANYCORE_ModelChunk;

typedef struct {
    uint32_t* modelDirties;
    uint16_t* modelDirtyList;
    uint32_t  modelDirtyCount;
} ANYCORE_ModelDirtyChunk;

typedef struct {
    uint64_t frame;
    double deltaTime;
    double elapsed;

    ANYCORE_SceneChunk* sceneChunks;    
    
    void* transformChunks;

    ANYCORE_InstanceChunk* instanceChunks;
    ANYCORE_ModelChunk*    modelChunks;

    uint32_t modelCount;
    uint32_t stride;

    ANYCORE_DirtyChunk*      dirtyChunks;
    ANYCORE_ModelDirtyChunk* modelDirtyChunks;

    uint16_t* chunkDirtyList;
    uint32_t  chunkDirtyCount;

    uint16_t* chunkModelDirtyList;
    uint32_t  chunkModelDirtyCount;

    uint32_t activeEntityCount;
    uint32_t chunkCount;
    uint32_t chunkLimit;
    uint32_t modelChunkLimit;
} ANYCORE_Stream;

typedef enum {
    ANYCORE_SUCCESS = 0,
    ANYCORE_ERR_ALREADY_INIT,
    ANYCORE_ERR_INVALID_ANYCORE,
    ANYCORE_ERR_ALLOC_FAILED,
    ANYCORE_ERR_FREE_SLOT_NOT_FOUND,
    ANYCORE_ERR_CHUNK_LIMIT,
    ANYCORE_ERR_NO_CHUNK,
    ANYCORE_ERR_ENTITY_DEAD,
    ANYCORE_ERR_ENTITY_LOCKED,
    ANYCORE_ERR_INVALID_MODEL_ID,
    ANYCORE_ERR_MODEL_NULL, 
    ANYCORE_ERR_INVALID_INDEX,
    ANYCORE_ERR_NULL_POINTER,
    ANYCORE_ERR_SYMBOL_IMPORT_FAILED,
    ANYCORE_ERR_CAPACITY_REACHED,
    ANYCORE_ERR_MODULE_LOAD_FAILED,
    ANYCORE_ERR_BEHAVIOUR_LOAD_FAILED
} ANYCORE_RESULT;

typedef enum {
    MODULE_TIMED,
    MODULE_MANDATORY,
    MODULE_INDEPENDENT
} ModuleType;

typedef struct {
    void* libHandle;
    ModuleType type;
    bool running;
    pthread_t thread;

    void (*start)(uint32_t);
    void (*loop)();
    void (*injectStream)(ANYCORE_Stream*);
    void (*clean)();

    pthread_mutex_t runMutex;
    pthread_cond_t  runCond;
    bool            readyToWork;

    pthread_mutex_t doneMutex;
    pthread_cond_t  doneCond;
    bool            isDone;

    double          interval;
    double          lastTick;
} Module;

typedef void* Behaviour;

#define ID_MAX   ((ID)UINT32_MAX)
#define ID16_MAX ((ID16)UINT16_MAX)
#define ID32_MAX ID_MAX

#define CHUNKSIZE  0X10000
#define CHUNKLIMIT 0X10000
#define CHUNKSHIFT 16
#define CHUNKMASK  0XFFFF

#endif // PRIMITIVES_H