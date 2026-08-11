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

#ifndef STANDARDCOMPONENT_H
#define STANDARDCOMPONENT_H

#include "primitives.h"

#include <stddef.h>
#include <sys/mman.h>

#define STANDARDCOMPONENT_mmap(size) mmap(NULL, size, PROT_READ | PROT_WRITE, MAP_ANONYMOUS | MAP_PRIVATE, -1, 0)

#define STANDARDCOMPONENT_munmap(ptr, size) munmap(ptr, size)

#define STANDARDCOMPONENT_MAP_FAILED MAP_FAILED

typedef enum {
    STANDARDCOMPONENT_SUCCESS = 0,
    STANDARDCOMPONENT_ERR_ALREADY_INIT,
    STANDARDCOMPONENT_ERR_NOT_INIT,
    STANDARDCOMPONENT_ERR_INVALID_STANDARDCOMPONENT,
    STANDARDCOMPONENT_ERR_INVALID_ENTITY,
    STANDARDCOMPONENT_ERR_INVALID_DATA_SIZE,
    STANDARDCOMPONENT_ERR_NULL_POINTER,
    STANDARDCOMPONENT_ERR_ALLOC_FAILED,
    STANDARDCOMPONENT_ERR_CHUNK_LIMIT,
    STANDARDCOMPONENT_ERR_NO_CHUNK,
    STANDARDCOMPONENT_ERR_CAPACITY_REACHED,
    STANDARDCOMPONENT_ERR_ALREADY_HAS,
    STANDARDCOMPONENT_ERR_DOESNT_HAVE
} StandardComponent_RESULT;

typedef struct {
    uint32_t* validFlags;
    uint16_t* generations;
    void*     data;
    uint32_t  count;
} StandardComponent_Chunk;

typedef struct {
    StandardComponent_Chunk* chunks;

    uint32_t chunkCount;
    uint32_t chunkLimit;

    uint32_t totalCount;
    size_t   dataSize;

    bool didinit;
} StandardComponent;

#endif // STANDARDCOMPONENT_H
