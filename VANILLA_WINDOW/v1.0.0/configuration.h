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

#ifndef CONFIGURATION_H
#define CONFIGURATION_H

#define OFF 0
#define ON  1

#define GET_PRECISION(id) ((id >> 16) & 0XFF)
#define GET_DIMENSION(id) ((id >> 8)  & 0XFF)
#define GET_VERSION(id)   (id & 0XFF)

#define VANILLA_EXPORT __attribute__((visibility("default")))

#include <sys/mman.h>
#define VANILLA_mmap(size) mmap(NULL, size, PROT_READ | PROT_WRITE, MAP_ANONYMOUS | MAP_PRIVATE, -1, 0)
#define VANILLA_munmap(ptr, size) munmap(ptr, size)
#define VANILLA_MAP_FAILED MAP_FAILED

static inline float VANILLA_minf(float a, float b) {
    return (a < b) ? a : b;
}

#define VANILLA_VERSION_MAJOR 1
#define VANILLA_VERSION_MINOR 0
#define VANILLA_VERSION_PATCH 0
#define VANILLA_VERSION_FLAGS 0

#define VANILLA_VERSION ((uint32_t)((VANILLA_VERSION_MAJOR << 24) | (VANILLA_VERSION_MINOR << 16) | (VANILLA_VERSION_PATCH << 8) | VANILLA_VERSION_FLAGS))

#endif // CONFIGURATION_H