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

#include <math.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>
#include <limits.h>

#include "vanilla_animation.h"

uint32_t clipLimit = 256;
uint32_t playbackLimit = 1024;
uint32_t trackLimit = 64;
uint32_t weightLimit = 256;   /* morph weights per clip (all weight tracks together) */
uint32_t translationLimit = 4096;
uint32_t clipArenaSize = (64u << 20);

#define TWO_PI 6.28318530717958647692
#define INVALID_SLOT UINT32_MAX

typedef struct {
    double* times;
    void* transforms;
    uint32_t* weightCounts;   /* per weight track */
    float* weights;           /* keyCount x weightCount, key-major */

    size_t arenaOffset;   /* where this clip's block starts in the arena */
    size_t arenaBytes;    /* block size (8-byte aligned) */

    double duration;

    uint32_t trackCount;
    uint32_t keyCount;

    uint32_t weightTrackCount;
    uint32_t weightCount;     /* sum of weightCounts = floats per key */

    uint16_t generation;

    bool used;
} VANILLA_Clip;

typedef struct {
    AnimationID clip;

    VANILLA_ANIMATION_MODE mode;
    VANILLA_ANIMATION_INTERPOLATION interpolation;

    double time;
    double speed;      /* >= 0, default 1 */
    uint32_t keyIdx;

    uint16_t generation;

    bool paused;
    bool dirty;        /* pose must be emitted next tick (seek while paused) */
    bool finished;
    bool used;
} VANILLA_Playback;

typedef struct {
    unsigned char* data;
    EntityID entity;

    VANILLA_ANIMATION_INTERPOLATION interpolation;

    double duration;
    double time;

    bool finished;
    bool used;
} VANILLA_Translation;

enum {
    MEM_CLIPS,
    MEM_ARENA,
    MEM_PLAYBACKS,
    MEM_POSES,
    MEM_PLAYBACK_ENTITIES,
    MEM_TRANSLATIONS,
    MEM_TRANSLATION_DATA,
    MEM_FREE_SLOTS,
    MEM_SUGGESTIONS,
    MEM_WEIGHT_POSES,
    MEM_WEIGHT_ENTITIES,
    MEM_COUNT
};

static void* mem[MEM_COUNT];
static size_t memSize[MEM_COUNT];

static VANILLA_Clip* clips;
static unsigned char* arena;
static VANILLA_Playback* playbacks;
static unsigned char* poses;
static EntityID* playbackEntities;
static float* weightPoses;               /* per playback: weightLimit current weights */
static EntityID* playbackWeightEntities; /* per playback: trackLimit weight track entities */
static VANILLA_Translation* translations;
static unsigned char* translationData;

static uint32_t* clipFreeSlots;
static uint32_t* playbackFreeSlots;

static VANILLA_ANIMATION_TransformSuggestion* suggestions;

static uint32_t clipFreeCount;
static uint32_t playbackFreeCount;
static uint32_t playbackHigh;
static uint32_t translationHigh;
static uint32_t suggestionCount;

static size_t arenaUsed;

static uint32_t dim;
static uint32_t prec;

static size_t transformSize;
static size_t poseStride;
static size_t translationStride;

static inline float wrapDeltaf(float delta) { return delta - (float)TWO_PI * roundf(delta / (float)TWO_PI); }
static inline double wrapDeltad(double delta) { return delta - TWO_PI * round(delta / TWO_PI); }
static inline double easeFactor(double f) { return f * f * (3.0 - 2.0 * f); }

static inline double applyInterpolation(VANILLA_ANIMATION_INTERPOLATION interpolation, double f) {
    if (interpolation == VANILLA_ANIMATION_INTERPOLATION_EASE) { return easeFactor(f); }
    if (interpolation == VANILLA_ANIMATION_INTERPOLATION_STEP) { return 0.0; }   /* hold the earlier key */
    return f;
}

static void lerp3Df(ANYCORE_Transform3Df* out, const ANYCORE_Transform3Df* a, const ANYCORE_Transform3Df* b, float f) {
    out->posx = a->posx + (b->posx - a->posx) * f;
    out->posy = a->posy + (b->posy - a->posy) * f;
    out->posz = a->posz + (b->posz - a->posz) * f;

    out->rotx = a->rotx + wrapDeltaf(b->rotx - a->rotx) * f;
    out->roty = a->roty + wrapDeltaf(b->roty - a->roty) * f;
    out->rotz = a->rotz + wrapDeltaf(b->rotz - a->rotz) * f;

    out->scax = a->scax + (b->scax - a->scax) * f;
    out->scay = a->scay + (b->scay - a->scay) * f;
    out->scaz = a->scaz + (b->scaz - a->scaz) * f;
}

static void lerp3Dd(ANYCORE_Transform3Dd* out, const ANYCORE_Transform3Dd* a, const ANYCORE_Transform3Dd* b, double f) {
    out->posx = a->posx + (b->posx - a->posx) * f;
    out->posy = a->posy + (b->posy - a->posy) * f;
    out->posz = a->posz + (b->posz - a->posz) * f;

    out->rotx = a->rotx + wrapDeltad(b->rotx - a->rotx) * f;
    out->roty = a->roty + wrapDeltad(b->roty - a->roty) * f;
    out->rotz = a->rotz + wrapDeltad(b->rotz - a->rotz) * f;

    out->scax = a->scax + (b->scax - a->scax) * f;
    out->scay = a->scay + (b->scay - a->scay) * f;
    out->scaz = a->scaz + (b->scaz - a->scaz) * f;
}

static void lerp2Df(ANYCORE_Transform2Df* out, const ANYCORE_Transform2Df* a, const ANYCORE_Transform2Df* b, float f) {
    out->posx = a->posx + (b->posx - a->posx) * f;
    out->posy = a->posy + (b->posy - a->posy) * f;

    out->rotz = a->rotz + wrapDeltaf(b->rotz - a->rotz) * f;

    out->scax = a->scax + (b->scax - a->scax) * f;
    out->scay = a->scay + (b->scay - a->scay) * f;
}

static void lerp2Dd(ANYCORE_Transform2Dd* out, const ANYCORE_Transform2Dd* a, const ANYCORE_Transform2Dd* b, double f) {
    out->posx = a->posx + (b->posx - a->posx) * f;
    out->posy = a->posy + (b->posy - a->posy) * f;

    out->rotz = a->rotz + wrapDeltad(b->rotz - a->rotz) * f;

    out->scax = a->scax + (b->scax - a->scax) * f;
    out->scay = a->scay + (b->scay - a->scay) * f;
}

static void lerpTransform(void* out, const void* a, const void* b, double f) {
    if (dim == 3 && prec == 4)      { lerp3Df(out, a, b, (float)f); }
    else if (dim == 3 && prec == 8) { lerp3Dd(out, a, b, f); }
    else if (dim == 2 && prec == 4) { lerp2Df(out, a, b, (float)f); }
    else                            { lerp2Dd(out, a, b, f); }
}

static inline unsigned char* translationStart(uint32_t slot)   { return translationData + (size_t)slot * translationStride; }
static inline unsigned char* translationTarget(uint32_t slot)  { return translationStart(slot) + transformSize; }
static inline unsigned char* translationCurrent(uint32_t slot) { return translationStart(slot) + 2u * transformSize; }

static bool validInterpolation( VANILLA_ANIMATION_INTERPOLATION interpolation) {
    return interpolation == VANILLA_ANIMATION_INTERPOLATION_EASE ||
           interpolation == VANILLA_ANIMATION_INTERPOLATION_LINEAR ||
           interpolation == VANILLA_ANIMATION_INTERPOLATION_STEP;
}

static bool validMode(VANILLA_ANIMATION_MODE mode) {
    return mode == VANILLA_ANIMATION_MODE_ONCE ||
           mode == VANILLA_ANIMATION_MODE_LOOP ||
           mode == VANILLA_ANIMATION_MODE_REVERSE ||
           mode == VANILLA_ANIMATION_MODE_REVERSE_LOOP;
}

static bool multiplySize(size_t a, size_t b, size_t* result) {
    if (a != 0 && b > SIZE_MAX / a) { return false; }
    *result = a * b;
    return true;
}

static bool addSize(size_t a, size_t b, size_t* result) {
    if (b > SIZE_MAX - a) { return false; }
    *result = a + b;
    return true;
}

VANILLA_EXPORT int VANILLA_ANIMATION_init(void) {
    if (clips) { return 1; }
    if (clipLimit == 0 || playbackLimit == 0 || trackLimit == 0 || translationLimit == 0 || clipArenaSize == 0) { return 1; }
    if (weightLimit == 0) { weightLimit = 1; }   /* keeps the pose buffer non-empty */

    dim = GET_DIMENSION(ANYCOREBuildID);
    prec = GET_PRECISION(ANYCOREBuildID);

    if      (dim == 3 && prec == 4) { transformSize = sizeof(ANYCORE_Transform3Df); }
    else if (dim == 3 && prec == 8) { transformSize = sizeof(ANYCORE_Transform3Dd); }
    else if (dim == 2 && prec == 4) { transformSize = sizeof(ANYCORE_Transform2Df); }
    else if (dim == 2 && prec == 8) { transformSize = sizeof(ANYCORE_Transform2Dd); }
    else {
        dim = prec = 0;
        return 1;
    }

    size_t clipBytes;
    size_t playbackBytes;
    size_t poseBytes;
    size_t playbackEntityCount;
    size_t playbackEntityBytes;
    size_t translationBytes;
    size_t translationDataBytes;
    size_t clipFreeBytes;
    size_t playbackFreeBytes;
    size_t freeSlotBytes;
    size_t suggestionCapacity;
    size_t suggestionBytes;
    size_t weightPoseCount;
    size_t weightPoseBytes;

    if (!multiplySize(sizeof(VANILLA_Clip), clipLimit, &clipBytes) ||
        !multiplySize(sizeof(VANILLA_Playback), playbackLimit, &playbackBytes) ||
        !multiplySize(trackLimit, transformSize, &poseStride) ||
        !multiplySize(playbackLimit, poseStride, &poseBytes) ||
        !multiplySize(playbackLimit, trackLimit, &playbackEntityCount) ||
        !multiplySize(sizeof(EntityID), playbackEntityCount, &playbackEntityBytes) ||
        !multiplySize(sizeof(VANILLA_Translation), translationLimit, &translationBytes) ||
        !multiplySize(transformSize, 3u, &translationStride) ||
        !multiplySize(translationLimit, translationStride, &translationDataBytes) ||
        !multiplySize(sizeof(uint32_t), clipLimit, &clipFreeBytes) ||
        !multiplySize(sizeof(uint32_t), playbackLimit, &playbackFreeBytes) ||
        !addSize(clipFreeBytes, playbackFreeBytes, &freeSlotBytes) ||
        !addSize(playbackLimit, translationLimit, &suggestionCapacity) ||
        !multiplySize(sizeof(VANILLA_ANIMATION_TransformSuggestion), suggestionCapacity, &suggestionBytes) ||
        !multiplySize(playbackLimit, weightLimit, &weightPoseCount) ||
        !multiplySize(sizeof(float), weightPoseCount, &weightPoseBytes)) {
        transformSize = poseStride = translationStride = 0;
        dim = prec = 0;
        return 1;
    }

    memSize[MEM_CLIPS] = clipBytes;
    memSize[MEM_ARENA] = (size_t)clipArenaSize;
    memSize[MEM_PLAYBACKS] = playbackBytes;
    memSize[MEM_POSES] = poseBytes;
    memSize[MEM_PLAYBACK_ENTITIES] = playbackEntityBytes;
    memSize[MEM_TRANSLATIONS] = translationBytes;
    memSize[MEM_TRANSLATION_DATA] = translationDataBytes;
    memSize[MEM_FREE_SLOTS] = freeSlotBytes;
    memSize[MEM_SUGGESTIONS] = suggestionBytes;
    memSize[MEM_WEIGHT_POSES] = weightPoseBytes;
    memSize[MEM_WEIGHT_ENTITIES] = playbackEntityBytes;

    for (uint32_t i = 0; i < MEM_COUNT; ++i) {
        mem[i] = VANILLA_mmap(memSize[i]);
        if (!mem[i] || mem[i] == VANILLA_MAP_FAILED) {
            mem[i] = NULL;
            for (uint32_t j = 0; j < i; ++j) {
                VANILLA_munmap(mem[j], memSize[j]);
                mem[j] = NULL;
                memSize[j] = 0;
            }

            for (uint32_t j = i; j < MEM_COUNT; ++j) {
                mem[j] = NULL;
                memSize[j] = 0;
            }

            transformSize = poseStride = translationStride = 0;
            dim = prec = 0;

            return 1;
        }
        memset(mem[i], 0, memSize[i]);
    }

    clips = mem[MEM_CLIPS];
    arena = mem[MEM_ARENA];
    playbacks = mem[MEM_PLAYBACKS];
    poses = mem[MEM_POSES];
    playbackEntities = mem[MEM_PLAYBACK_ENTITIES];
    translations = mem[MEM_TRANSLATIONS];
    translationData = mem[MEM_TRANSLATION_DATA];

    clipFreeSlots = mem[MEM_FREE_SLOTS];
    playbackFreeSlots = clipFreeSlots + clipLimit;

    suggestions = mem[MEM_SUGGESTIONS];
    weightPoses = mem[MEM_WEIGHT_POSES];
    playbackWeightEntities = mem[MEM_WEIGHT_ENTITIES];

    for (uint32_t i = 0; i < clipLimit; ++i)     { clipFreeSlots[i]     = i; }
    for (uint32_t i = 0; i < playbackLimit; ++i) { playbackFreeSlots[i] = i; }

    clipFreeCount = clipLimit;
    playbackFreeCount = playbackLimit;

    playbackHigh = 0;
    translationHigh = 0;
    suggestionCount = 0;
    arenaUsed = 0;

    return 0;
}

VANILLA_EXPORT void VANILLA_ANIMATION_clean(void) {
    for (uint32_t i = 0; i < MEM_COUNT; ++i) {
        if (mem[i]) {
            VANILLA_munmap(mem[i], memSize[i]);
            mem[i] = NULL;
        }
        memSize[i] = 0;
    }

    clips = NULL;
    arena = NULL;
    playbacks = NULL;
    poses = NULL;
    playbackEntities = NULL;
    translations = NULL;
    translationData = NULL;

    clipFreeSlots = NULL;
    playbackFreeSlots = NULL;
    suggestions = NULL;
    weightPoses = NULL;
    playbackWeightEntities = NULL;

    clipFreeCount = 0;
    playbackFreeCount = 0;

    playbackHigh = 0;
    translationHigh = 0;
    suggestionCount = 0;

    arenaUsed = 0;

    transformSize = 0;
    poseStride = 0;
    translationStride = 0;

    dim = 0;
    prec = 0;
}

VANILLA_EXPORT VANILLA_ANIMATION_RESULT VANILLA_ANIMATION_registerClip(const VANILLA_ANIMATION_ClipCreateInfo* info, AnimationID* outClip) {
    if (!didstart || !clips) { return VANILLA_ANIMATION_ERR_DIDNT_START; }
    if (!info || !outClip || !info->times) { return VANILLA_ANIMATION_ERR_NULL_POINTER; }
    if (info->trackCount > 0 && !info->transforms) { return VANILLA_ANIMATION_ERR_NULL_POINTER; }
    if (info->weightTrackCount > 0 && (!info->weightCounts || !info->weights)) { return VANILLA_ANIMATION_ERR_NULL_POINTER; }

    const uint32_t trackCount = info->trackCount;
    const uint32_t weightTrackCount = info->weightTrackCount;
    const uint32_t keyCount = info->keyCount;
    const double* times = info->times;

    if (trackCount == 0 && weightTrackCount == 0) { return VANILLA_ANIMATION_ERR_INVALID_ARGUMENT; }
    if (trackCount > trackLimit || weightTrackCount > trackLimit || keyCount < 2) { return VANILLA_ANIMATION_ERR_INVALID_ARGUMENT; }

    uint64_t weightSum = 0;
    for (uint32_t i = 0; i < weightTrackCount; ++i) {
        if (info->weightCounts[i] == 0) { return VANILLA_ANIMATION_ERR_INVALID_ARGUMENT; }
        weightSum += info->weightCounts[i];
    }
    if (weightSum > weightLimit) { return VANILLA_ANIMATION_ERR_INVALID_ARGUMENT; }
    const uint32_t weightCount = (uint32_t)weightSum;

    if (!isfinite(times[0]) || times[0] != 0.0) { return VANILLA_ANIMATION_ERR_INVALID_ARGUMENT; }
    for (uint32_t i = 1; i < keyCount; ++i) {
        if (!isfinite(times[i]) || !(times[i] > times[i - 1u])) { return VANILLA_ANIMATION_ERR_INVALID_ARGUMENT; }
    }

    if (clipFreeCount == 0) { return VANILLA_ANIMATION_ERR_CAPACITY_REACHED; }

    /* Block layout: times (8-aligned) | transforms | weightCounts | weights, padded to 8. */
    size_t transformCount;
    size_t transformsBytes;
    size_t timesBytes;
    size_t countsBytes;
    size_t weightValues;
    size_t weightsBytes;
    size_t rawBytes;
    size_t alignedBytes;

    if (!multiplySize(keyCount, trackCount, &transformCount) ||
        !multiplySize(transformCount, transformSize, &transformsBytes) ||
        !multiplySize(keyCount, sizeof(double), &timesBytes) ||
        !multiplySize(weightTrackCount, sizeof(uint32_t), &countsBytes) ||
        !multiplySize(keyCount, weightCount, &weightValues) ||
        !multiplySize(weightValues, sizeof(float), &weightsBytes) ||
        !addSize(timesBytes, transformsBytes, &rawBytes) ||
        !addSize(rawBytes, countsBytes, &rawBytes) ||
        !addSize(rawBytes, weightsBytes, &rawBytes) ||
        !addSize(rawBytes, 7u, &alignedBytes)) {
        return VANILLA_ANIMATION_ERR_CAPACITY_REACHED;
    }

    alignedBytes &= ~(size_t)7u;
    if (arenaUsed > clipArenaSize || alignedBytes > (size_t)clipArenaSize - arenaUsed) { return VANILLA_ANIMATION_ERR_CAPACITY_REACHED; }

    const uint32_t slot = clipFreeSlots[--clipFreeCount];
    VANILLA_Clip* clip = &clips[slot];

    unsigned char* base = arena + arenaUsed;
    unsigned char* transformsAt = base + timesBytes;
    unsigned char* countsAt = transformsAt + transformsBytes;
    unsigned char* weightsAt = countsAt + countsBytes;

    memcpy(base, times, timesBytes);
    if (transformsBytes) { memcpy(transformsAt, info->transforms, transformsBytes); }
    if (countsBytes)     { memcpy(countsAt, info->weightCounts, countsBytes); }
    if (weightsBytes)    { memcpy(weightsAt, info->weights, weightsBytes); }

    arenaUsed += alignedBytes;

    clip->times = (double*)base;
    clip->transforms = transformsAt;
    clip->weightCounts = (uint32_t*)countsAt;
    clip->weights = (float*)weightsAt;
    clip->arenaOffset = arenaUsed - alignedBytes;
    clip->arenaBytes = alignedBytes;

    clip->trackCount = trackCount;
    clip->keyCount = keyCount;
    clip->weightTrackCount = weightTrackCount;
    clip->weightCount = weightCount;
    clip->duration = times[keyCount - 1u];
    clip->used = true;

    *outClip = (AnimationID){ slot, ++clip->generation };
    return VANILLA_ANIMATION_SUCCESS;
}

/* Segment [keyIdx, keyIdx + 1] containing time (binary search), clamped to the last segment. */
static uint32_t findKey(const VANILLA_Clip* clip, double time) {
    uint32_t lo = 0, hi = clip->keyCount - 2u;
    while (lo < hi) {
        const uint32_t mid = (lo + hi + 1u) / 2u;
        if (clip->times[mid] <= time) { lo = mid; }
        else                          { hi = mid - 1u; }
    }
    return lo;
}

static VANILLA_Clip* getClip(AnimationID id) {
    if (id.slot >= clipLimit) { return NULL; }
    VANILLA_Clip* clip = &clips[id.slot];
    if (!clip->used || clip->generation != id.generation) { return NULL; }
    return clip;
}

static VANILLA_Playback* getPlayback(AnimationID id) {
    if (id.slot >= playbackLimit) { return NULL; }
    VANILLA_Playback* pb = &playbacks[id.slot];
    if (!pb->used || pb->generation != id.generation) { return NULL; }
    return pb;
}

static void freePlaybackSlot(uint32_t slot) {
    playbacks[slot].used = false;
    playbackFreeSlots[playbackFreeCount++] = slot;
    while (playbackHigh > 0 && !playbacks[playbackHigh - 1u].used) { --playbackHigh; }
}

VANILLA_EXPORT VANILLA_ANIMATION_RESULT VANILLA_ANIMATION_unregisterClip(AnimationID clipID) {
    if (!didstart || !clips) { return VANILLA_ANIMATION_ERR_DIDNT_START; }

    VANILLA_Clip* clip = getClip(clipID);
    if (!clip) { return VANILLA_ANIMATION_ERR_INVALID_HANDLE; }

    /* Its playbacks stop now (no final pose: the caller is unloading the clip). */
    for (uint32_t slot = 0; slot < playbackHigh; ++slot) {
        VANILLA_Playback* pb = &playbacks[slot];
        if (pb->used && pb->clip.slot == clipID.slot && pb->clip.generation == clipID.generation) {
            freePlaybackSlot(slot);
        }
    }

    /* Compact the arena: blocks after this one move down; playbacks hold IDs, so only clip pointers change. */
    const size_t start = clip->arenaOffset;
    const size_t bytes = clip->arenaBytes;
    const size_t tail  = arenaUsed - (start + bytes);

    if (tail > 0) { memmove(arena + start, arena + start + bytes, tail); }
    arenaUsed -= bytes;

    for (uint32_t i = 0; i < clipLimit; ++i) {
        VANILLA_Clip* other = &clips[i];
        if (!other->used || other == clip || other->arenaOffset < start) { continue; }

        other->arenaOffset -= bytes;
        other->times      = (double*)((unsigned char*)other->times - bytes);
        other->transforms = (unsigned char*)other->transforms - bytes;
        other->weightCounts = (uint32_t*)((unsigned char*)other->weightCounts - bytes);
        other->weights    = (float*)((unsigned char*)other->weights - bytes);
    }

    clip->used = false;
    clip->times = NULL;
    clip->transforms = NULL;
    clip->weightCounts = NULL;
    clip->weights = NULL;
    clipFreeSlots[clipFreeCount++] = clipID.slot;

    return VANILLA_ANIMATION_SUCCESS;
}

VANILLA_EXPORT VANILLA_ANIMATION_RESULT VANILLA_ANIMATION_setPlaybackPaused(AnimationID playbackID, bool paused) {
    if (!didstart || !clips) { return VANILLA_ANIMATION_ERR_DIDNT_START; }
    VANILLA_Playback* pb = getPlayback(playbackID);
    if (!pb) { return VANILLA_ANIMATION_ERR_INVALID_HANDLE; }

    pb->paused = paused;
    return VANILLA_ANIMATION_SUCCESS;
}

VANILLA_EXPORT VANILLA_ANIMATION_RESULT VANILLA_ANIMATION_setPlaybackSpeed(AnimationID playbackID, double speed) {
    if (!didstart || !clips) { return VANILLA_ANIMATION_ERR_DIDNT_START; }
    if (!(speed >= 0.0) || !isfinite(speed)) { return VANILLA_ANIMATION_ERR_INVALID_ARGUMENT; }
    VANILLA_Playback* pb = getPlayback(playbackID);
    if (!pb) { return VANILLA_ANIMATION_ERR_INVALID_HANDLE; }

    pb->speed = speed;
    return VANILLA_ANIMATION_SUCCESS;
}

VANILLA_EXPORT VANILLA_ANIMATION_RESULT VANILLA_ANIMATION_setPlaybackTime(AnimationID playbackID, double time) {
    if (!didstart || !clips) { return VANILLA_ANIMATION_ERR_DIDNT_START; }
    if (!isfinite(time)) { return VANILLA_ANIMATION_ERR_INVALID_ARGUMENT; }
    VANILLA_Playback* pb = getPlayback(playbackID);
    if (!pb) { return VANILLA_ANIMATION_ERR_INVALID_HANDLE; }

    const VANILLA_Clip* clip = getClip(pb->clip);
    if (!clip) { return VANILLA_ANIMATION_ERR_INVALID_HANDLE; }

    if (time < 0.0)            { time = 0.0; }
    if (time > clip->duration) { time = clip->duration; }

    pb->time     = time;
    pb->keyIdx   = findKey(clip, time);
    pb->finished = false;
    pb->dirty    = true;
    return VANILLA_ANIMATION_SUCCESS;
}

VANILLA_EXPORT VANILLA_ANIMATION_RESULT VANILLA_ANIMATION_getPlaybackTime(AnimationID playbackID, double* outTime) {
    if (!didstart || !clips) { return VANILLA_ANIMATION_ERR_DIDNT_START; }
    if (!outTime) { return VANILLA_ANIMATION_ERR_NULL_POINTER; }
    const VANILLA_Playback* pb = getPlayback(playbackID);
    if (!pb) { return VANILLA_ANIMATION_ERR_INVALID_HANDLE; }

    *outTime = pb->time;
    return VANILLA_ANIMATION_SUCCESS;
}

VANILLA_EXPORT VANILLA_ANIMATION_RESULT VANILLA_ANIMATION_startPlayback(AnimationID clipID, VANILLA_ANIMATION_MODE mode, VANILLA_ANIMATION_INTERPOLATION interpolation, const EntityID* entities, const EntityID* weightEntities, AnimationID* outPlayback) {
    if (!didstart || !clips) { return VANILLA_ANIMATION_ERR_DIDNT_START; }
    if (!outPlayback) { return VANILLA_ANIMATION_ERR_NULL_POINTER; }

    if (!validMode(mode) || !validInterpolation(interpolation)) { return VANILLA_ANIMATION_ERR_INVALID_ARGUMENT; }

    if (clipID.slot >= clipLimit) { return VANILLA_ANIMATION_ERR_INVALID_HANDLE; }
    VANILLA_Clip* clip = &clips[clipID.slot];
    if (!clip->used || clip->generation != clipID.generation) { return VANILLA_ANIMATION_ERR_INVALID_HANDLE; }

    if (clip->trackCount > 0 && !entities) { return VANILLA_ANIMATION_ERR_NULL_POINTER; }
    if (clip->weightTrackCount > 0 && !weightEntities) { return VANILLA_ANIMATION_ERR_NULL_POINTER; }

    if (playbackFreeCount == 0) { return VANILLA_ANIMATION_ERR_CAPACITY_REACHED; }
    const uint32_t slot = playbackFreeSlots[--playbackFreeCount];

    VANILLA_Playback* pb = &playbacks[slot];

    pb->clip = clipID;
    pb->mode = mode;
    pb->interpolation = interpolation;

    const bool reverse = mode == VANILLA_ANIMATION_MODE_REVERSE || mode == VANILLA_ANIMATION_MODE_REVERSE_LOOP;

    pb->time = reverse ? clip->duration : 0.0;
    pb->keyIdx = reverse ? clip->keyCount - 2u : 0u;

    pb->speed = 1.0;
    pb->paused = false;
    pb->dirty = false;
    pb->finished = false;
    pb->used = true;

    const size_t keyBytes = (size_t)clip->trackCount * transformSize;
    const unsigned char* keys = clip->transforms;
    const size_t initialKey = reverse ? (size_t)(clip->keyCount - 1u) : 0u;
    
    if (keyBytes) {
        memcpy(poses + (size_t)slot * poseStride, keys + initialKey * keyBytes, keyBytes);
        memcpy(playbackEntities + (size_t)slot * trackLimit, entities, (size_t)clip->trackCount * sizeof(EntityID));
    }
    if (clip->weightTrackCount > 0) {
        memcpy(weightPoses + (size_t)slot * weightLimit, clip->weights + initialKey * clip->weightCount, (size_t)clip->weightCount * sizeof(float));
        memcpy(playbackWeightEntities + (size_t)slot * trackLimit, weightEntities, (size_t)clip->weightTrackCount * sizeof(EntityID));
    }

    if (slot + 1u > playbackHigh) { playbackHigh = slot + 1u; }

    *outPlayback = (AnimationID){ slot, ++pb->generation };

    return VANILLA_ANIMATION_SUCCESS;
}

VANILLA_EXPORT VANILLA_ANIMATION_RESULT VANILLA_ANIMATION_stopPlayback(AnimationID playbackID) {
    if (!didstart || !clips) { return VANILLA_ANIMATION_ERR_DIDNT_START; }
    if (playbackID.slot >= playbackLimit) { return VANILLA_ANIMATION_ERR_INVALID_HANDLE; }

    VANILLA_Playback* pb = &playbacks[playbackID.slot];
    if (!pb->used || pb->generation != playbackID.generation) { return VANILLA_ANIMATION_ERR_INVALID_HANDLE; }

    freePlaybackSlot(playbackID.slot);
    return VANILLA_ANIMATION_SUCCESS;
}

VANILLA_EXPORT VANILLA_ANIMATION_RESULT VANILLA_ANIMATION_startTranslation(const void* start, const void* target, double duration, VANILLA_ANIMATION_INTERPOLATION interpolation, EntityID entity) {
    if (!didstart || !clips) { return VANILLA_ANIMATION_ERR_DIDNT_START; }
    if (!start || !target) { return VANILLA_ANIMATION_ERR_NULL_POINTER; }

    if (!(duration > 0.0) || !isfinite(duration)) { return VANILLA_ANIMATION_ERR_INVALID_ARGUMENT; }
    if (!validInterpolation(interpolation)) { return VANILLA_ANIMATION_ERR_INVALID_ARGUMENT; }

    uint32_t slot = 0;
    while (slot < translationLimit && translations[slot].used) { ++slot; }
    if (slot == translationLimit) { return VANILLA_ANIMATION_ERR_CAPACITY_REACHED; }

    VANILLA_Translation* tr = &translations[slot];

    tr->data = translationStart(slot);
    tr->entity = entity;

    tr->interpolation = interpolation;
    tr->duration = duration;
    tr->time = 0.0;

    tr->finished = false;
    tr->used = true;

    memcpy(tr->data, start, transformSize);
    memcpy(tr->data + transformSize, target, transformSize);
    memcpy(tr->data + 2u * transformSize, start, transformSize);

    if (slot + 1u > translationHigh) { translationHigh = slot + 1u; }

    return VANILLA_ANIMATION_SUCCESS;
}

VANILLA_EXPORT VANILLA_ANIMATION_RESULT VANILLA_ANIMATION_getSuggestions(const VANILLA_ANIMATION_TransformSuggestion** outSuggestions, uint32_t* outSuggestionCount) {
    if (!didstart || !clips) { return VANILLA_ANIMATION_ERR_DIDNT_START; }
    if (!outSuggestions || !outSuggestionCount) { return VANILLA_ANIMATION_ERR_NULL_POINTER; }

    *outSuggestions = suggestions;
    *outSuggestionCount = suggestionCount;

    return VANILLA_ANIMATION_SUCCESS;
}

VANILLA_EXPORT void VANILLA_ANIMATION_tick(double deltaTime) {
    suggestionCount = 0;
    if (!clips || !isfinite(deltaTime) || deltaTime < 0.0) { return; }

    for (uint32_t slot = 0; slot < playbackHigh; ++slot) {
        VANILLA_Playback* pb = &playbacks[slot];
        if (!pb->used || (pb->finished && !pb->dirty)) { continue; }

        /* Paused: nothing moves; a seek still emits its pose once. */
        if (pb->paused && !pb->dirty) { continue; }
        const double step = pb->paused ? 0.0 : deltaTime * pb->speed;
        pb->dirty = false;

        if (pb->clip.slot >= clipLimit) {
            pb->finished = true;
            continue;
        }

        VANILLA_Clip* clip = &clips[pb->clip.slot];
        if (!clip->used || clip->generation != pb->clip.generation) {
            pb->finished = true;
            continue;
        }

        const bool reverse = pb->mode == VANILLA_ANIMATION_MODE_REVERSE || pb->mode == VANILLA_ANIMATION_MODE_REVERSE_LOOP;
        const bool loop    = pb->mode == VANILLA_ANIMATION_MODE_LOOP    || pb->mode == VANILLA_ANIMATION_MODE_REVERSE_LOOP;

        if (reverse) {
            pb->time -= step;
            if (pb->time <= 0.0 && step > 0.0) {
                if (loop) {
                    pb->time = fmod(pb->time, clip->duration);
                    if (pb->time <= 0.0) { pb->time += clip->duration; }

                    pb->keyIdx = findKey(clip, pb->time);
                }
                else {
                    pb->time = 0.0;
                    pb->finished = true;
                }
            }
        }
        else {
            pb->time += step;
            if (pb->time >= clip->duration && step > 0.0) {
                if (loop) {
                    pb->time = fmod(pb->time, clip->duration);
                    pb->keyIdx = findKey(clip, pb->time);
                }
                else {
                    pb->time = clip->duration;
                    pb->finished = true;
                }
            }
        }

        const size_t keyBytes = (size_t)clip->trackCount * transformSize;

        const unsigned char* keys = clip->transforms;
        unsigned char* pose = poses + (size_t)slot * poseStride;

        const uint32_t weightCount = clip->weightCount;
        float* weightPose = weightPoses + (size_t)slot * weightLimit;

        if (pb->finished) {
            const size_t endKey = reverse ? 0u : (size_t)(clip->keyCount - 1u);
            if (keyBytes)    { memcpy(pose, keys + endKey * keyBytes, keyBytes); }
            if (weightCount) { memcpy(weightPose, clip->weights + endKey * weightCount, (size_t)weightCount * sizeof(float)); }
        }
        else {
            if (reverse) { while (pb->keyIdx > 0u && pb->time < clip->times[pb->keyIdx]) { --pb->keyIdx; } }
            else         { while (pb->keyIdx + 2u < clip->keyCount && pb->time >= clip->times[pb->keyIdx + 1u]) { ++pb->keyIdx; } }

            const double interval = clip->times[pb->keyIdx + 1u] - clip->times[pb->keyIdx];
            double f = (pb->time - clip->times[pb->keyIdx]) / interval;

            if (f < 0.0) { f = 0.0; }
            else if      (f > 1.0) { f = 1.0; }

            f = applyInterpolation(pb->interpolation, f);
            const unsigned char* a = keys + (size_t)pb->keyIdx * keyBytes;
            const unsigned char* b = a + keyBytes;

            for (uint32_t track = 0; track < clip->trackCount; ++track) {
                const size_t offset = (size_t)track * transformSize;
                lerpTransform(pose + offset, a + offset, b + offset, f);
            }

            /* Morph weights: plain blend between the two keys (same interpolation factor). */
            const float* wa = clip->weights + (size_t)pb->keyIdx * weightCount;
            const float* wb = wa + weightCount;
            const float wf = (float)f;
            for (uint32_t w = 0; w < weightCount; ++w) {
                weightPose[w] = wa[w] + (wb[w] - wa[w]) * wf;
            }
        }

        VANILLA_ANIMATION_TransformSuggestion* rec = &suggestions[suggestionCount++];

        rec->kind = VANILLA_ANIMATION_SUGGESTION_CLIP;

        rec->playback.slot = slot;
        rec->playback.generation = pb->generation;

        rec->entities = playbackEntities + (size_t)slot * trackLimit;
        rec->trackCount = clip->trackCount;
        rec->transforms = pose;
        rec->finished = pb->finished;

        rec->weightTrackCount = clip->weightTrackCount;
        rec->weightEntities = clip->weightTrackCount ? playbackWeightEntities + (size_t)slot * trackLimit : NULL;
        rec->weightCounts = clip->weightTrackCount ? clip->weightCounts : NULL;
        rec->weights = clip->weightTrackCount ? weightPose : NULL;
    }

    for (uint32_t slot = 0; slot < translationHigh; ++slot) {
        VANILLA_Translation* tr = &translations[slot];
        if (!tr->used || tr->finished) { continue; }

        tr->time += deltaTime;
        double progress = tr->time / tr->duration;

        if (progress >= 1.0) {
            progress = 1.0;
            tr->finished = true;
        }

        double f;
        switch (tr->interpolation) {
            case VANILLA_ANIMATION_INTERPOLATION_LINEAR:
                f = progress;
                break;
            case VANILLA_ANIMATION_INTERPOLATION_EASE:
                f = progress * progress * (3.0 - 2.0 * progress);
                break;
            case VANILLA_ANIMATION_INTERPOLATION_STEP:
                f = 0.0;   /* jumps to the target when finished */
                break;
            default:
                f = progress;
                break;
        }

        if (tr->finished) { memcpy(translationCurrent(slot), translationTarget(slot), transformSize); }
        else              { lerpTransform(translationCurrent(slot), translationStart(slot), translationTarget(slot), f); }

        VANILLA_ANIMATION_TransformSuggestion* rec = &suggestions[suggestionCount++];

        rec->kind = VANILLA_ANIMATION_SUGGESTION_TRANSLATION;

        rec->playback.slot = INVALID_SLOT;
        rec->playback.generation = 0;

        rec->entities = &tr->entity;
        rec->trackCount = 1;
        rec->transforms = translationCurrent(slot);
        rec->finished = tr->finished;

        rec->weightTrackCount = 0;
        rec->weightEntities = NULL;
        rec->weightCounts = NULL;
        rec->weights = NULL;

        if (tr->finished) {
            tr->used = false;
        }
    }

    while (translationHigh > 0 && !translations[translationHigh - 1u].used) { --translationHigh; }
}

VANILLA_EXPORT void VANILLA_setClipLimit(uint32_t limit) {
    if (didstart) { return; }
    clipLimit = limit;
}

VANILLA_EXPORT void VANILLA_setPlaybackLimit(uint32_t limit) {
    if (didstart) { return; }
    playbackLimit = limit;
}

VANILLA_EXPORT void VANILLA_setTrackLimit(uint32_t limit) {
    if (didstart) { return; }
    trackLimit = limit;
}

VANILLA_EXPORT void VANILLA_setWeightLimit(uint32_t limit) {
    if (didstart) { return; }
    weightLimit = limit;
}

VANILLA_EXPORT void VANILLA_setTranslationLimit(uint32_t limit) {
    if (didstart) { return; }
    translationLimit = limit;
}

VANILLA_EXPORT void VANILLA_setClipArenaSize(uint32_t size) {
    if (didstart) { return; }
    clipArenaSize = size;
}