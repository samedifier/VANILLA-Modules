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

#include "main.h"
#include "vanilla_animation.h"
#include <stdio.h>

uint32_t ANYCOREBuildID = 0;
ANYCORE_Stream* stream  = NULL;
bool didstart = false;

VANILLA_EXPORT void injectStream(ANYCORE_Stream* s) { stream = s; }

VANILLA_EXPORT void start(uint32_t buildid) {
    ANYCOREBuildID = buildid;
    if (GET_VERSION(buildid) != 1) {
        printf("[VANILLA ANIMATION] invalid ANYCORE version\n");
        return; 
    }

    if (VANILLA_ANIMATION_init() != 0) {
        printf("[VANILLA ANIMATION] failed to start\n");
        return;
    }

    printf("[VANILLA ANIMATION] initalized without an error\n");
    didstart = true;
}

VANILLA_EXPORT void loop() {
    if (didstart == false) { return; }
    VANILLA_ANIMATION_tick(stream->deltaTime);
}

VANILLA_EXPORT void clean() {
    VANILLA_ANIMATION_clean();
}

VANILLA_EXPORT uint32_t VANILLA_getVersion() { return VANILLA_VERSION; }