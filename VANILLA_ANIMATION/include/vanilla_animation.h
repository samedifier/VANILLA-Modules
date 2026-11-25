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

#ifndef VANILLA_ANIMATION_H
#define VANILLA_ANIMATION_H

#include "main.h"

int VANILLA_ANIMATION_init(void);
void VANILLA_ANIMATION_clean(void);
void VANILLA_ANIMATION_tick(double deltaTime);

#endif // VANILLA_ANIMATION_H