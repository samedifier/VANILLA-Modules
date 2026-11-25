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

#ifndef MAIN_H
#define MAIN_H

#include <stdbool.h>
#include <stdint.h>

#include "configuration.h"
#include "primitives.h"

typedef struct { uint32_t slot; uint16_t generation; } AnimationID;

typedef enum {
    VANILLA_ANIMATION_MODE_ONCE = 0,
    VANILLA_ANIMATION_MODE_LOOP,
    VANILLA_ANIMATION_MODE_REVERSE,
    VANILLA_ANIMATION_MODE_REVERSE_LOOP
} VANILLA_ANIMATION_MODE;

typedef enum {
    VANILLA_ANIMATION_INTERPOLATION_EASE = 0,  /* smoothstep between keys */
    VANILLA_ANIMATION_INTERPOLATION_LINEAR,    /* use for baked (densely sampled) clips */
    VANILLA_ANIMATION_INTERPOLATION_STEP       /* hold each key until the next one (glTF STEP) */
} VANILLA_ANIMATION_INTERPOLATION;

typedef enum {
    VANILLA_ANIMATION_SUGGESTION_CLIP = 0,
    VANILLA_ANIMATION_SUGGESTION_TRANSLATION
} VANILLA_ANIMATION_SUGGESTION_KIND;

/*
 * A baked clip: transform tracks and / or morph weight tracks sharing keyCount key times
 * (times[0] = 0, strictly increasing). Everything is copied.
 */
typedef struct {
    uint32_t keyCount;
    const double* times;

    /* Transform tracks: keyCount x trackCount ANYCORE transforms, key-major (key 0: track 0..n-1, key 1: ...).
       Absolute (world) transforms, one track per entity. trackCount may be 0 when there are weight tracks. */
    uint32_t trackCount;
    const void* transforms;

    /* Morph weight tracks (glTF "weights" channels), one per morphed mesh entity. weightCounts[i] = weights of
       track i (its morph target count). weights = keyCount x (sum of weightCounts) floats, key-major
       (key 0: track 0 weights, track 1 weights, ...; key 1: ...). weightTrackCount 0 = none. */
    uint32_t weightTrackCount;
    const uint32_t* weightCounts;
    const float* weights;
} VANILLA_ANIMATION_ClipCreateInfo;

typedef struct {
    VANILLA_ANIMATION_SUGGESTION_KIND kind;
    AnimationID playback;
    const EntityID* entities;
    uint32_t trackCount;
    const void* transforms;
    bool finished;

    /* Morph weights (CLIP suggestions of clips with weight tracks; 0 / NULL otherwise). weights holds every
       weight track's current weights back to back, weightCounts[i] of them for weightEntities[i]. */
    uint32_t weightTrackCount;
    const EntityID* weightEntities;
    const uint32_t* weightCounts;
    const float* weights;
} VANILLA_ANIMATION_TransformSuggestion;

typedef enum {
    VANILLA_ANIMATION_SUCCESS = 0,

    VANILLA_ANIMATION_ERR_NULL_POINTER,
    VANILLA_ANIMATION_ERR_ALLOC_FAILED,
    VANILLA_ANIMATION_ERR_ALREADY_STARTED,
    VANILLA_ANIMATION_ERR_CAPACITY_REACHED,
    VANILLA_ANIMATION_ERR_DIDNT_START,
    VANILLA_ANIMATION_ERR_INVALID_ARGUMENT,
    VANILLA_ANIMATION_ERR_INVALID_HANDLE
} VANILLA_ANIMATION_RESULT;

extern uint32_t ANYCOREBuildID;
extern ANYCORE_Stream* stream;
extern bool didstart;

extern uint32_t clipLimit;
extern uint32_t playbackLimit;
extern uint32_t trackLimit;
extern uint32_t weightLimit;
extern uint32_t translationLimit;
extern uint32_t clipArenaSize;

#endif // MAIN_H