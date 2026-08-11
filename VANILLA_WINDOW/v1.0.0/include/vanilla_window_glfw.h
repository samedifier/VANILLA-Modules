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

#ifndef VANILLA_WINDOW_GLFW_H
#define VANILLA_WINDOW_GLFW_H

#include "main.h"

#include <stdbool.h>
#include <GLFW/glfw3.h>

typedef struct{
    GLFWwindow* window;
} VANILLA_GLFW_State;

extern VANILLA_GLFW_State g_glfw;

#endif // VANILLA_WINDOW_GLFW_H