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

static uint16_t g_textInputBuffer[4096];
static uint32_t g_textInputCount = 0;

static void VANILLA_GLFW_charCallback(GLFWwindow* window, unsigned int codepoint) {
    if (codepoint <= 0xFFFF) {
        if (g_textInputCount >= 4096)
            return;

        g_textInputBuffer[g_textInputCount++] = (uint16_t)codepoint;
    }
    else if (codepoint <= 0x10FFFF) {
        if (g_textInputCount + 1 >= 4096) { return; }

        codepoint -= 0x10000;

        g_textInputBuffer[g_textInputCount++] = 0xD800 + (codepoint >> 10);
        g_textInputBuffer[g_textInputCount++] = 0xDC00 + (codepoint & 0x3FF);
    }
}

bool VANILLA_GLFW_init(void) {
    memset(&g_glfw, 0, sizeof(g_glfw));

    if (!glfwInit()) {
        printf("[VANILLA] GLFW initialization failed\n");
        return false;
    }

    return true;
}

void VANILLA_GLFW_pollEvents(void) {
    g_textInputCount = 0;
    glfwPollEvents();
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

    glfwSetCharCallback(g_glfw.window, VANILLA_GLFW_charCallback);

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

///

VANILLA_EXPORT const char* VANILLA_getWindowTitle(void) {
    return windowTitle;
}

VANILLA_EXPORT void VANILLA_getWindowSize(unsigned int* width, unsigned int* height) {
    if (width == NULL || height == NULL) { return; }

    if (width)  { *width  = windowWidth; }
    if (height) { *height = windowHeight; }
}

VANILLA_EXPORT void VANILLA_getWindowPosition(unsigned int* x, unsigned int* y) {
    if (x == NULL || y == NULL) { return; }

    if (x) { *x = windowPositionX; }
    if (y) { *y = windowPositionY; }
}

VANILLA_EXPORT void VANILLA_getCursorPosition(unsigned int* x, unsigned int* y) {
    if (x == NULL || y == NULL) { return; }

    double dx, dy;
    glfwGetCursorPos(g_glfw.window, &dx, &dy);

    if (x) { *x = (unsigned int)dx; }
    if (y) { *y = (unsigned int)dy; }
}

VANILLA_EXPORT bool VANILLA_isWindowVisible(void) {
    return showWindow;
}

VANILLA_EXPORT void VANILLA_getMonitorSize(unsigned int* width, unsigned int* height) {
    if (width == NULL || height == NULL) { return; }

    GLFWmonitor* monitor = glfwGetPrimaryMonitor();
    const GLFWvidmode* mode = glfwGetVideoMode(monitor);

    unsigned int monitorWidth = mode->width;
    unsigned int monitorHeight = mode->height;

    if (width)  { *width  = monitorWidth;  }
    if (height) { *height = monitorHeight; }
}

VANILLA_EXPORT const uint16_t* VANILLA_getTextInput(uint32_t* outCount) {
    if (outCount) { *outCount = g_textInputCount; }
    return g_textInputBuffer;
}