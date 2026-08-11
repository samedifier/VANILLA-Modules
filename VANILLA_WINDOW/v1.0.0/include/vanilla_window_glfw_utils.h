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

#ifndef VANILLA_WINDOW_GLFW_UTILS_H
#define VANILLA_WINDOW_GLFW_UTILS_H

#include "vanilla_window_glfw.h"

bool VANILLA_GLFW_init(void);
VANILLA_RESULT VANILLA_GLFW_createWindow(unsigned int width, unsigned int height);
void VANILLA_GLFW_shutdown(void);

#endif // VANILLA_WINDOW_GLFW_UTILS_H