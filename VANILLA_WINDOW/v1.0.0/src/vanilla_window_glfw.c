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

#include "vanilla_window_glfw.h"

#include <stdio.h>
#include <string.h>

VANILLA_GLFW_State g_glfw;

bool VANILLA_GLFW_init(void) {
    memset(&g_glfw, 0, sizeof(g_glfw));

    if (!glfwInit()) {
        printf("[VANILLA] GLFW initialization failed\n");
        return false;
    }

    return true;
}

int VANILLA_GLFW_createWindow(unsigned int width, unsigned int height) {
    glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);
    glfwWindowHint(GLFW_DECORATED, GLFW_FALSE);
    glfwWindowHint(GLFW_RESIZABLE, GLFW_FALSE);

    g_glfw.window = glfwCreateWindow((int)width, (int)height, windowTitle, NULL, NULL);
    
    glfwHideWindow(g_glfw.window);

    if (!g_glfw.window) {
        printf("[VANILLA] GLFW window creation failed\n");
        glfwTerminate();
        return 1;
    }

    return 0;
}

void VANILLA_GLFW_shutdown(void) {
    if (g_glfw.window){
        glfwDestroyWindow(g_glfw.window);
        g_glfw.window = NULL;
    }

    glfwTerminate();
    memset(&g_glfw, 0, sizeof(g_glfw));
}
