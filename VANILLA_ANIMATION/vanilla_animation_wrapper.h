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

#ifndef VANILLA_ANIMATION_WRAPPER_H
#define VANILLA_ANIMATION_WRAPPER_H

#include "primitives.h"

#include <stdbool.h>
#include <stdint.h>
#include <dlfcn.h>

#ifndef __VANILLA_WRAPPER_REDEF
#define __VANILLA_WRAPPER_REDEF
#define VANILLA_DLOPEN(path)          dlopen((path), RTLD_NOW)
#define VANILLA_DLSYM(h, name, field) (*(void**)(&field) = dlsym((h), (name)))
#define VANILLA_DLCLOSE(h)            dlclose(h)
#endif

typedef struct {
    uint32_t slot;
    uint16_t generation;
} AnimationID;

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

typedef struct {
    /* Returns the VANILLA ANIMATION version. */
    uint32_t (*getVersion) (void);

    /* Registers a baked clip (transform tracks and / or morph weight tracks, see VANILLA_ANIMATION_ClipCreateInfo). Copied. */
    VANILLA_ANIMATION_RESULT (*registerClip) (const VANILLA_ANIMATION_ClipCreateInfo* info, AnimationID* outClip);

    /* Frees a clip and its memory (later clips are compacted). Its playbacks are stopped. */
    VANILLA_ANIMATION_RESULT (*unregisterClip) (AnimationID clip);

    /* Plays a clip: entities[i] receives transform track i, weightEntities[i] weight track i (copied; either
       may be NULL when the clip has no such tracks). Weights go to VANILLA WINDOW's setEntityMorphWeights. */
    VANILLA_ANIMATION_RESULT (*startPlayback) (AnimationID clipID, VANILLA_ANIMATION_MODE mode, VANILLA_ANIMATION_INTERPOLATION interpolation, const EntityID* entities, const EntityID* weightEntities, AnimationID* outPlayback);

    /* Stops a playback and frees its slot (finished playbacks keep their slot until stopped). */
    VANILLA_ANIMATION_RESULT (*stopPlayback) (AnimationID playback);

    /* Playback controls. Speed >= 0 (default 1; direction comes from the mode). setPlaybackTime clamps to
       [0, duration], restarts a finished playback and emits the pose next tick even while paused. */
    VANILLA_ANIMATION_RESULT (*setPlaybackPaused) (AnimationID playback, bool paused);
    VANILLA_ANIMATION_RESULT (*setPlaybackSpeed) (AnimationID playback, double speed);
    VANILLA_ANIMATION_RESULT (*setPlaybackTime) (AnimationID playback, double time);
    VANILLA_ANIMATION_RESULT (*getPlaybackTime) (AnimationID playback, double* outTime);

    /* Starts a transform transition for an entity. */
    VANILLA_ANIMATION_RESULT (*startTranslation) (const void* start, const void* target, double duration, VANILLA_ANIMATION_INTERPOLATION interpolation, EntityID entity);

    /* Returns this frame's transform suggestions (clip poses and translations); apply them to ANYCORE. */
    VANILLA_ANIMATION_RESULT (*getSuggestions) (const VANILLA_ANIMATION_TransformSuggestion** outSuggestions, uint32_t* outSuggestionCount);

    /* Limits, before start (defaults: 256 clips, 1024 playbacks, 64 tracks per clip (transform and weight tracks
       each), 256 morph weights per clip, 4096 translations, 64 MB arena). */
    void (*setClipLimit) (uint32_t limit);
    void (*setPlaybackLimit) (uint32_t limit);
    void (*setTrackLimit) (uint32_t limit);
    void (*setWeightLimit) (uint32_t limit);
    void (*setTranslationLimit) (uint32_t limit);
    void (*setClipArenaSize) (uint32_t size);
} VANILLA_ANIMATION_Wrapper;

static inline bool VANILLA_ANIMATION_Wrapper_init(VANILLA_ANIMATION_Wrapper* w, void* libHandle) {
    if (!w || !libHandle) { return false; }

    *w = (VANILLA_ANIMATION_Wrapper){0};

    VANILLA_DLSYM(libHandle, "VANILLA_getVersion", w->getVersion);
    VANILLA_DLSYM(libHandle, "VANILLA_ANIMATION_registerClip", w->registerClip);
    VANILLA_DLSYM(libHandle, "VANILLA_ANIMATION_unregisterClip", w->unregisterClip);
    VANILLA_DLSYM(libHandle, "VANILLA_ANIMATION_startPlayback", w->startPlayback);
    VANILLA_DLSYM(libHandle, "VANILLA_ANIMATION_stopPlayback", w->stopPlayback);
    VANILLA_DLSYM(libHandle, "VANILLA_ANIMATION_setPlaybackPaused", w->setPlaybackPaused);
    VANILLA_DLSYM(libHandle, "VANILLA_ANIMATION_setPlaybackSpeed", w->setPlaybackSpeed);
    VANILLA_DLSYM(libHandle, "VANILLA_ANIMATION_setPlaybackTime", w->setPlaybackTime);
    VANILLA_DLSYM(libHandle, "VANILLA_ANIMATION_getPlaybackTime", w->getPlaybackTime);
    VANILLA_DLSYM(libHandle, "VANILLA_ANIMATION_startTranslation", w->startTranslation);
    VANILLA_DLSYM(libHandle, "VANILLA_ANIMATION_getSuggestions", w->getSuggestions);
    VANILLA_DLSYM(libHandle, "VANILLA_setClipLimit", w->setClipLimit);
    VANILLA_DLSYM(libHandle, "VANILLA_setPlaybackLimit", w->setPlaybackLimit);
    VANILLA_DLSYM(libHandle, "VANILLA_setTrackLimit", w->setTrackLimit);
    VANILLA_DLSYM(libHandle, "VANILLA_setWeightLimit", w->setWeightLimit);
    VANILLA_DLSYM(libHandle, "VANILLA_setTranslationLimit", w->setTranslationLimit);
    VANILLA_DLSYM(libHandle, "VANILLA_setClipArenaSize", w->setClipArenaSize);

    return
        w->getVersion != NULL &&
        w->registerClip != NULL &&
        w->unregisterClip != NULL &&
        w->startPlayback != NULL &&
        w->stopPlayback != NULL &&
        w->setPlaybackPaused != NULL &&
        w->setPlaybackSpeed != NULL &&
        w->setPlaybackTime != NULL &&
        w->getPlaybackTime != NULL &&
        w->startTranslation != NULL &&
        w->getSuggestions != NULL &&
        w->setClipLimit != NULL &&
        w->setPlaybackLimit != NULL &&
        w->setTrackLimit != NULL &&
        w->setWeightLimit != NULL &&
        w->setTranslationLimit != NULL &&
        w->setClipArenaSize != NULL;
}

static inline void VANILLA_ANIMATION_injectWrapper(VANILLA_ANIMATION_Wrapper* w, void* behaviourHandle) {
    if (!w || !behaviourHandle) { return; }
    void (*injectWrapper) (VANILLA_ANIMATION_Wrapper*) = NULL;
    VANILLA_DLSYM(behaviourHandle, "injectVANILLAANIMATIONWrapper", injectWrapper);
    if (injectWrapper) { injectWrapper(w); }
}

#endif /* VANILLA_ANIMATION_WRAPPER_H */