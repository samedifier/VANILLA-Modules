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

#ifndef MAIN_UTILS_H
#define MAIN_UTILS_H

#include "main.h"

VANILLA_EXPORT void injectStream(ANYCORE_Stream* s);
VANILLA_EXPORT void start(uint32_t buildid);
VANILLA_EXPORT void loop();
VANILLA_EXPORT void clean();

VANILLA_EXPORT void VANILLA_WINDOW_setAppName(const char* appName);
VANILLA_EXPORT void VANILLA_WINDOW_setAppVersion(const uint32_t major, const uint32_t minor, const uint32_t patch);
VANILLA_EXPORT void VANILLA_WINDOW_setWindowSize(unsigned int width, unsigned int height);

VANILLA_EXPORT void VANILLA_WINDOW_setVertexLimit(uint64_t limit);
VANILLA_EXPORT void VANILLA_WINDOW_setVertexConfiguration(uint32_t stride, const VANILLA_VertexAttribute* attributes, uint32_t attributeCount);

VANILLA_EXPORT void VANILLA_SetCameraPosition(Vec3f position);
VANILLA_EXPORT void VANILLA_SetCameraTarget(Vec3f target);
VANILLA_EXPORT void VANILLA_SetCameraUp(Vec3f up);

VANILLA_EXPORT void VANILLA_SetCameraFOV(float degrees);
VANILLA_EXPORT void VANILLA_SetCameraNear(float nearPlane);
VANILLA_EXPORT void VANILLA_SetCameraFar(float farPlane);

VANILLA_EXPORT VANILLA_RESULT VANILLA_notifyNewChunkAlloc(void);
VANILLA_EXPORT VANILLA_RESULT VANILLA_notifyLastChunkFree(void);

#endif // MAIN_UTILS_H