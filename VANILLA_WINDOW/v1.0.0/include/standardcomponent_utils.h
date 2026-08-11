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

#ifndef STANDARDCOMPONENT_UTILS_H
#define STANDARDCOMPONENT_UTILS_H

#include "standardcomponent.h"
#include <string.h>

static inline StandardComponent_RESULT StandardComponent_init(StandardComponent* sc, uint32_t chunkLimit, size_t dataSize) {
    if (!sc)           { return STANDARDCOMPONENT_ERR_INVALID_STANDARDCOMPONENT; }
    if (sc->didinit)   { return STANDARDCOMPONENT_ERR_ALREADY_INIT; }
    if (dataSize == 0) { return STANDARDCOMPONENT_ERR_INVALID_DATA_SIZE; }

    if (chunkLimit == 0)          { chunkLimit = 1; }
    if (chunkLimit >= CHUNKLIMIT) { chunkLimit = CHUNKLIMIT; }

    StandardComponent_Chunk* chunks = (StandardComponent_Chunk*)STANDARDCOMPONENT_mmap(chunkLimit * sizeof(StandardComponent_Chunk));
    if (chunks == STANDARDCOMPONENT_MAP_FAILED) { return STANDARDCOMPONENT_ERR_ALLOC_FAILED; }

    sc->chunks = chunks;

    sc->chunkLimit = chunkLimit;
    sc->dataSize   = dataSize;
    sc->chunkCount = 0;
    sc->totalCount = 0;
    sc->didinit    = true;

    return STANDARDCOMPONENT_SUCCESS;
}

static inline StandardComponent_RESULT StandardComponent_free(StandardComponent* sc) {
    if (!sc)         { return STANDARDCOMPONENT_ERR_INVALID_STANDARDCOMPONENT; }
    if (!sc->didinit) { return STANDARDCOMPONENT_ERR_NOT_INIT; }

    uint32_t chunkLimit = sc->chunkLimit;
    uint32_t chunkCount = sc->chunkCount;
    size_t   dataSize   = sc->dataSize;

    StandardComponent_Chunk* chunks = sc->chunks;

    uint32_t prec1 = 0X800     * sizeof(uint32_t);
    uint32_t prec2 = CHUNKSIZE * dataSize;
    uint32_t prec3 = CHUNKSIZE * sizeof(uint16_t);

    if (chunkCount == 0) { goto skip; }

    for (uint32_t i = 0; i < chunkCount; i++) {
        StandardComponent_Chunk* cunk = &sc->chunks[i];

        STANDARDCOMPONENT_munmap(cunk->validFlags,  prec1);
        STANDARDCOMPONENT_munmap(cunk->data,        prec2);
        STANDARDCOMPONENT_munmap(cunk->generations, prec3);
    }

    skip:

    STANDARDCOMPONENT_munmap(chunks, chunkLimit * sizeof(StandardComponent_Chunk));

    sc->chunkLimit = 0;
    sc->chunkCount = 0;
    sc->dataSize   = 0;
    sc->totalCount = 0;
    sc->didinit    = false;

    return STANDARDCOMPONENT_SUCCESS;
}

static inline StandardComponent_RESULT StandardComponent_allocNewChunk(StandardComponent* sc) {
    if (!sc)                              { return STANDARDCOMPONENT_ERR_INVALID_STANDARDCOMPONENT; }
    if (!sc->didinit)                     { return STANDARDCOMPONENT_ERR_NOT_INIT; }
    if (sc->chunkCount >= sc->chunkLimit) { return STANDARDCOMPONENT_ERR_CHUNK_LIMIT; }

    uint32_t prec1 = 0X800     * sizeof(uint32_t);
    uint32_t prec2 = CHUNKSIZE * sc->dataSize;
    uint32_t prec3 = CHUNKSIZE * sizeof(uint16_t);

    uint32_t* validflags  = STANDARDCOMPONENT_mmap(prec1);
    void*     data        = STANDARDCOMPONENT_mmap(prec2);
    uint16_t* generations = STANDARDCOMPONENT_mmap(prec3);

    if (validflags == STANDARDCOMPONENT_MAP_FAILED || data == STANDARDCOMPONENT_MAP_FAILED || generations == STANDARDCOMPONENT_MAP_FAILED) {
        if (validflags  != STANDARDCOMPONENT_MAP_FAILED) { STANDARDCOMPONENT_munmap(validflags,  prec1); }
        if (data        != STANDARDCOMPONENT_MAP_FAILED) { STANDARDCOMPONENT_munmap(data,        prec2); }
        if (generations != STANDARDCOMPONENT_MAP_FAILED) { STANDARDCOMPONENT_munmap(generations, prec3); }
        return STANDARDCOMPONENT_ERR_ALLOC_FAILED;
    }

    StandardComponent_Chunk* chunk = &sc->chunks[sc->chunkCount];

    chunk->validFlags  = validflags;
    chunk->data        = data;
    chunk->generations = generations;
    chunk->count       = 0;

    sc->chunkCount++;
    return STANDARDCOMPONENT_SUCCESS;
}

static inline StandardComponent_RESULT StandardComponent_freeLastChunk(StandardComponent* sc) {
    if (!sc)          { return STANDARDCOMPONENT_ERR_INVALID_STANDARDCOMPONENT; }
    if (!sc->didinit) { return STANDARDCOMPONENT_ERR_NOT_INIT; }

    uint32_t prec1 = 0X800     * sizeof(uint32_t);
    uint32_t prec2 = CHUNKSIZE * sc->dataSize;
    uint32_t prec3 = CHUNKSIZE * sizeof(uint16_t);

    uint32_t chunkCount = sc->chunkCount;
    if (chunkCount == 0) { return STANDARDCOMPONENT_ERR_NO_CHUNK; }

    StandardComponent_Chunk* chunk = &sc->chunks[chunkCount - 1];

    STANDARDCOMPONENT_munmap(chunk->validFlags,  prec1);
    STANDARDCOMPONENT_munmap(chunk->data,        prec2);
    STANDARDCOMPONENT_munmap(chunk->generations, prec3);

    sc->totalCount -= chunk->count;
    sc->chunkCount--;

    return STANDARDCOMPONENT_SUCCESS;
}

static inline StandardComponent_RESULT StandardComponent_add(StandardComponent* sc, EntityID entityID) {
    if (!sc) { return STANDARDCOMPONENT_ERR_INVALID_STANDARDCOMPONENT; }

    uint32_t chunk = entityID.slot >> CHUNKSHIFT;
    if (chunk >= sc->chunkCount) { return STANDARDCOMPONENT_ERR_INVALID_ENTITY; }

    uint32_t slot = entityID.slot & CHUNKMASK;
    uint32_t mask     = 1u << (slot & 31);
    uint32_t wordindx = slot >> 5;

    StandardComponent_Chunk* c = &sc->chunks[chunk];
    if ((c->validFlags[wordindx] & mask)) { return STANDARDCOMPONENT_ERR_ALREADY_HAS; }

    c->generations[slot] = entityID.generation;
    c->validFlags[wordindx] |= mask;
    c->count++;

    sc->totalCount++;

    return STANDARDCOMPONENT_SUCCESS;
}

static inline StandardComponent_RESULT StandardComponent_remove(StandardComponent* sc, EntityID entityID) {
    if (!sc) { return STANDARDCOMPONENT_ERR_INVALID_STANDARDCOMPONENT; }

    uint32_t chunk = entityID.slot >> CHUNKSHIFT;
    if (chunk >= sc->chunkCount) { return STANDARDCOMPONENT_ERR_INVALID_ENTITY; }

    uint32_t slot = entityID.slot & CHUNKMASK;
    uint32_t mask     = 1u << (slot & 31);
    uint32_t wordindx = slot >> 5;

    StandardComponent_Chunk* c = &sc->chunks[chunk];
    if (!((c->validFlags[wordindx] & mask) && (c->generations[slot] == entityID.generation))) { return STANDARDCOMPONENT_ERR_DOESNT_HAVE; }

    c->validFlags[wordindx] &= ~mask;
    c->count--;

    sc->totalCount--;

    return STANDARDCOMPONENT_SUCCESS;
}

static inline bool StandardComponent_has(StandardComponent* sc, EntityID entityID) {
    if (!sc) { return false; }

    uint32_t chunk = entityID.slot >> CHUNKSHIFT;
    if (chunk >= sc->chunkCount) { return false; }

    uint32_t slot  = entityID.slot &  CHUNKMASK;
    uint32_t mask     = 1u << (slot & 31);
    uint32_t wordindx = slot >> 5;

    StandardComponent_Chunk* c = &sc->chunks[chunk];
    if (!((c->validFlags[wordindx] & mask) && (c->generations[slot] == entityID.generation))) { return false; }

    return true;
}

static inline StandardComponent_RESULT StandardComponent_setData(StandardComponent* sc, EntityID entityID, const void* data) {
    if (!sc)   { return STANDARDCOMPONENT_ERR_INVALID_STANDARDCOMPONENT; }
    if (!data) { return STANDARDCOMPONENT_ERR_NULL_POINTER; }

    uint32_t chunk = entityID.slot >> CHUNKSHIFT;
    if (chunk >= sc->chunkCount) { return STANDARDCOMPONENT_ERR_INVALID_ENTITY; }

    uint32_t slot  = entityID.slot & CHUNKMASK;
    uint32_t mask     = 1u << (slot & 31);
    uint32_t wordindx = slot >> 5;

    StandardComponent_Chunk* c = &sc->chunks[chunk];
    if (!((c->validFlags[wordindx] & mask) && (c->generations[slot] == entityID.generation))) { return STANDARDCOMPONENT_ERR_DOESNT_HAVE; }

    uint8_t* d = (uint8_t*)c->data + (slot * sc->dataSize);
    memcpy(d, data, sc->dataSize);

    return STANDARDCOMPONENT_SUCCESS;
}

static inline StandardComponent_RESULT StandardComponent_getData(StandardComponent* sc, EntityID entityID, void** outData) {
    if (!sc)      { return STANDARDCOMPONENT_ERR_INVALID_STANDARDCOMPONENT; }
    if (!outData) { return STANDARDCOMPONENT_ERR_NULL_POINTER; }

    uint32_t chunk = entityID.slot >> CHUNKSHIFT;
    if (chunk >= sc->chunkCount) { return STANDARDCOMPONENT_ERR_INVALID_ENTITY; }

    uint32_t slot  = entityID.slot & CHUNKMASK;
    uint32_t wordindx = slot >> 5;
    uint32_t mask     = 1u << (slot & 31);

    StandardComponent_Chunk* c = &sc->chunks[chunk];
    if (!((c->validFlags[wordindx] & mask) && (c->generations[slot] == entityID.generation))) { return STANDARDCOMPONENT_ERR_DOESNT_HAVE; }

    *outData = (uint8_t*)c->data + (slot * sc->dataSize);

    return STANDARDCOMPONENT_SUCCESS;
}

#endif // STANDARDCOMPONENT_UTILS_H
