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

#include "vanilla_window_glfw_utils.h"
#include "vanilla_window_vulkan_utils.h"

#include "vanilla_analyzer.h"
#include "vanilla_window_threading.h"
#include "vanilla_window_camera.h"
#include "vanilla_window_input_utils.h"

#include "standardcomponent_utils.h"

#include <stdio.h>

uint32_t ANYCOREBuildID = 0;
ANYCORE_Stream* stream  = NULL;
bool didstart = false;

char* applicationName = "main";
char* windowTitle = "main";
bool didWindowTitleSet = true;

uint32_t majorVersion = 1;
uint32_t minorVersion = 0;
uint32_t patchVersion = 0;

unsigned int windowWidth  = 1280;
unsigned int windowHeight = 720;
bool didWindowSizeSet = false;

unsigned int cursorPositionX = 0;
unsigned int cursorPositionY = 0;
bool didCursorPositionSet = false;

unsigned int windowPositionX = 0;
unsigned int windowPositionY = 0;
bool didWindowPositionSet = false;

VANILLA_CursorMode cursorMode;
bool didCursowModeSet;

bool showWindow = true;
bool didShowWindowSet = true;

VANILLA_ProjectionType projectionType = VANILLA_PROJECTION_PERSPECTIVE;
float orthographicSize = 4.0f;

bool enableVSync = true;
bool didVSyncSet = false;

uint32_t stride = sizeof(Vertex);
uint64_t vertexLimit = 4 * 1024 * 1024;
uint64_t indexLimit  = 6 * 1024 * 1024;

Vec3f skyColor = (Vec3f){ 0.3f, 0.3f, 0.3f };

VANILLA_Camera camera = {
    .posx = 0.0f,
    .posy = 0.0f,
    .posz = -5.0f,

    .targetx = 0.0f,
    .targety = 0.0f,
    .targetz = 1.0f,

    .upx = 0.0f,
    .upy = 1.0f,
    .upz = 0.0f,

    .fov = 1.3962634015954636f,
    .nearPlane = 0.1f,
    .farPlane = 1000.0f,

    .uioffsetx = 0.0f,
    .uioffsety = 0.0f,
};

_Static_assert(sizeof(Vertex) == 64, "Vertex must be 64 bytes (default layout offsets)");

static VANILLA_VertexAttribute defaultAttributes[] = {
    {
        .location = 0,
        .format = VANILLA_VERTEXFORMAT_R32G32B32A32_FLOAT,
        .anycoreOffset = offsetof(Vertex, px),
        .vanillaOffset = 0,
        .size = 4 * sizeof(float)
    },
    {
        .location = 1,
        .format = VANILLA_VERTEXFORMAT_R32G32B32A32_FLOAT,
        .anycoreOffset = offsetof(Vertex, ny),
        .vanillaOffset = 16,
        .size = 4 * sizeof(float)
    },
    {
        .location = 2,
        .format = VANILLA_VERTEXFORMAT_R32G32B32A32_FLOAT,
        .anycoreOffset = offsetof(Vertex, tx),
        .vanillaOffset = 32,
        .size = 4 * sizeof(float)
    },
    {   /* TEXCOORD_1 */
        .location = 3,
        .format = VANILLA_VERTEXFORMAT_R32G32_FLOAT,
        .anycoreOffset = offsetof(Vertex, u1),
        .vanillaOffset = 48,
        .size = 2 * sizeof(float)
    },
    {   /* COLOR_0, RGBA8 */
        .location = 4,
        .format = VANILLA_VERTEXFORMAT_R8G8B8A8_UNORM,
        .anycoreOffset = offsetof(Vertex, color),
        .vanillaOffset = 56,
        .size = sizeof(uint32_t)
    }
};

/* Default: the 64-byte Vertex (main.h), stride = sizeof(Vertex). */
VANILLA_VertexLayout vertexLayout = {
    .attributes = defaultAttributes,
    .attributeCount = 5
};

VANILLA_EXPORT void injectStream(ANYCORE_Stream* s) { stream = s; }

VANILLA_EXPORT void start(uint32_t buildid) {
    ANYCOREBuildID = buildid;
    if (GET_VERSION(buildid) != 1) {
        printf("[VANILLA WINDOW] invalid ANYCORE version\n");
        return;
    }

    if (GET_DIMENSION(buildid) != 2 && GET_DIMENSION(buildid) != 3) {
        printf("[VANILLA WINDOW] invalid ANYCORE dimension\n");
        return;
    }

    if (pipelineInfos == NULL || vulkan.pipelineCount == 0) {
        printf("[VANILLA WINDOW] no pipeline info is given\n");
        return;
    }

    StandardComponent_init(&materialComponent, stream->chunkLimit, sizeof(MaterialID));

    VANILLA_GLFW_init();
    VANILLA_GLFW_createWindow(windowWidth, windowHeight);

    VANILLA_InputInit(g_glfw.window);

    if (VANILLA_VK_init())       { return; }
    if (VANILLA_ThreadingInit()) { return; }

    printf("[VANILLA WINDOW] initalized without an error\n");
    didstart = true;
}

static bool onceinloop = false;
VANILLA_EXPORT void loop() {
    if (didstart == false) { return; }
    if (onceinloop == false) {
        VANILLA_CreateDescriptor();
        if (vulkan.computePipelineCount > 0) {
            VANILLA_VK_createComputeDescriptor();
        }
        if (enablePostProcessing) {
            VANILLA_CreatePostProcessingDescriptor();
        }
    }

    VANILLA_analyzeStream();

    if (didWindowTitleSet) {
        glfwSetWindowTitle(g_glfw.window, windowTitle);
        didWindowTitleSet = false;
    }

    if (didWindowPositionSet) {
        glfwSetWindowPos(g_glfw.window, windowPositionX, windowPositionY);
        didWindowPositionSet = false;
    }

    if (didCursorPositionSet) {
        glfwSetCursorPos(g_glfw.window, (double)cursorPositionX, (double)cursorPositionY);
        didCursorPositionSet = false;
    }

    if (didWindowSizeSet) {
        glfwSetWindowSize(g_glfw.window, windowWidth, windowHeight);
        didWindowSizeSet = false;   /* the size check below picks up the change once the window has it */
    }

    /*
     * The framebuffer is the truth: resize requests are asynchronous, and acquire / present may report
     * the swapchain out of date. 0 x 0 = minimized: nothing is drawn until the window comes back.
     */
    int framebufferWidth = 0, framebufferHeight = 0;
    glfwGetFramebufferSize(g_glfw.window, &framebufferWidth, &framebufferHeight);
    const bool minimized = framebufferWidth <= 0 || framebufferHeight <= 0;

    if (!minimized) {
        const bool sizeChanged =
            (uint32_t)framebufferWidth  != vulkan.swapchainExtent.width ||
            (uint32_t)framebufferHeight != vulkan.swapchainExtent.height;

        if (sizeChanged || didVSyncSet || VANILLA_VK_swapchainOutOfDate) {
            if (!VANILLA_VK_recreateSwapchain()) {
                didVSyncSet = false;
            }
        }
    }
    
    if (didCursowModeSet) {
        switch (cursorMode) {
            case VANILLA_CURSORMODE_NORMAL:
                glfwSetInputMode(g_glfw.window, GLFW_CURSOR, GLFW_CURSOR_NORMAL);
                break;
            case VANILLA_CURSORMODE_HIDDEN:
                glfwSetInputMode(g_glfw.window, GLFW_CURSOR, GLFW_CURSOR_HIDDEN);
                break;
            case VANILLA_CURSORMODE_DISABLED:
                glfwSetInputMode(g_glfw.window, GLFW_CURSOR, GLFW_CURSOR_DISABLED);
                break;
            default:
                glfwSetInputMode(g_glfw.window, GLFW_CURSOR, GLFW_CURSOR_NORMAL);
                break;
        }
    }

    if (didShowWindowSet) {
        if (showWindow) {
            glfwShowWindow(g_glfw.window);
        }
        else {
            glfwHideWindow(g_glfw.window);
        }
    }

    /* A new MSAA sample count rebuilds pipelines/targets here, between frames. */
    VANILLA_VK_applyAntiAliasingChanges();

    if (!minimized) { VANILLA_VK_drawFrame(); }
    VANILLA_GLFW_pollEvents();

    onceinloop = true;
}

VANILLA_EXPORT void clean() {
    VANILLA_ThreadingShutdown();
    vkDeviceWaitIdle(vulkan.device);

    VANILLA_VK_destroySyncObjects();

    VANILLA_VK_destroyCommandBuffers();
    VANILLA_VK_destroyCulling();
    VANILLA_VK_destroySkinning();
    VANILLA_VK_cleanupIndirectBuffer();
    VANILLA_VK_destroyAliveFlagsBuffer();
    VANILLA_VK_destroyGrid();
    VANILLA_VK_destroyEnvironment();
    VANILLA_VK_destroyShadows();
    VANILLA_VK_destroyLightning();

    VANILLA_VK_destroyCommandPool();

    VANILLA_VK_destroyDepthResources();

    if (enablePostProcessing) {
        VANILLA_VK_destroyPostProcessingPipeline();
        VANILLA_VK_destroyPostProcessingPipelineLayout();

        VANILLA_CleanupPostProcessingDescriptor();

        VANILLA_VK_destroyPostProcessingImages();
        VANILLA_VK_destroyPipelineImages();
    }

    if (vulkan.computePipelineCount > 0) {
        VANILLA_VK_destroyComputePipelines();
        VANILLA_VK_destroyComputeDescriptor();
        VANILLA_VK_destroyComputeBuffers();
    }

    VANILLA_VK_destroyMsaaTargets();
    VANILLA_VK_destroyHdr();
    VANILLA_VK_destroyPipeline();
    VANILLA_VK_destroyPipelineLayout();

    VANILLA_CleanupDescriptor();

    if (vulkan.vramModelList != NULL) {
        VANILLA_munmap(vulkan.vramModelList, stream->chunkLimit * CHUNKSIZE * sizeof(uint32_t));
        vulkan.vramModelList = NULL;
    }

    VANILLA_CleanupPersistentBuffers(vulkan.device);

    VANILLA_VK_destroyImageViews();
    VANILLA_VK_destroySwapchain();

    VANILLA_VK_destroyMaterialBuffer();

    VANILLA_destroySamplers();
    VANILLA_VK_texturefree();

    VANILLA_VK_destroyLogicalDevice();

    VANILLA_VK_destroySurface();

    VANILLA_VK_destroyInstance();

    StandardComponent_free(&materialComponent);
    VANILLA_GLFW_shutdown();
}

VANILLA_EXPORT VANILLA_WINDOW_RESULT VANILLA_setAppName(const char* appName) {
    if (didstart)        { return VANILLA_WINDOW_ERR_ALREADY_STARTED; }
    if (appName == NULL) { return VANILLA_WINDOW_ERR_NULL_POINTER; }
    applicationName = (char*)appName;

    return VANILLA_WINDOW_SUCCESS;
}

VANILLA_EXPORT VANILLA_WINDOW_RESULT VANILLA_setWindowTitle(const char* title) {
    if (title == NULL) { return VANILLA_WINDOW_ERR_NULL_POINTER; }
    windowTitle = (char*)title;
    didWindowTitleSet = true;

    return VANILLA_WINDOW_SUCCESS;
}

VANILLA_EXPORT VANILLA_WINDOW_RESULT VANILLA_setAppVersion(const uint32_t major, const uint32_t minor, const uint32_t patch) {
    if (didstart) { return VANILLA_WINDOW_ERR_ALREADY_STARTED; }
    majorVersion = major;
    minorVersion = minor;
    patchVersion = patch;

    return VANILLA_WINDOW_SUCCESS;
}

VANILLA_EXPORT void VANILLA_setWindowSize(unsigned int width, unsigned int height) {
    windowWidth  = width;
    windowHeight = height;
    didWindowSizeSet = true;
}

VANILLA_EXPORT void VANILLA_setWindowPosition(unsigned int x, unsigned int y) {
    windowPositionX = x;
    windowPositionY = y;
    didWindowPositionSet = true;
}

VANILLA_EXPORT void VANILLA_setCursorPosition(unsigned int x, unsigned int y) {
    cursorPositionX = x;
    cursorPositionY = y;
    didCursorPositionSet = true;
}

VANILLA_EXPORT void VANILLA_showWindow() {
    if (showWindow == true) { return; }
    showWindow = true;
    didShowWindowSet = true;
}
VANILLA_EXPORT void VANILLA_hideWindow() {
    if (showWindow == false) { return; }
    showWindow = false;
    didShowWindowSet = true;
}

VANILLA_EXPORT VANILLA_WINDOW_RESULT VANILLA_setVertexLimit(uint64_t limit) {
    if (didstart) { return VANILLA_WINDOW_ERR_ALREADY_STARTED; }
    if (limit <= 0) { limit = 1; }
    vertexLimit = limit;

    return VANILLA_WINDOW_SUCCESS;
}

VANILLA_EXPORT VANILLA_WINDOW_RESULT VANILLA_setIndexLimit(uint64_t limit) {
    if (didstart) { return VANILLA_WINDOW_ERR_ALREADY_STARTED; }
    if (limit <= 0) { limit = 1; }
    indexLimit = limit;

    return VANILLA_WINDOW_SUCCESS;
}

VANILLA_EXPORT VANILLA_WINDOW_RESULT VANILLA_setVertexLayout(uint32_t _stride, const VANILLA_VertexAttribute* attributes, uint32_t attributeCount) {
    if (didstart) { return VANILLA_WINDOW_ERR_ALREADY_STARTED; }
    stride = _stride;
    vertexLayout.attributes = attributes;
    vertexLayout.attributeCount = attributeCount;

    return VANILLA_WINDOW_SUCCESS;
}

VANILLA_EXPORT void VANILLA_setSkyColor(Vec3f color) {
    skyColor.x = color.x;
    skyColor.y = color.y;
    skyColor.z = color.z;
}

VANILLA_EXPORT void VANILLA_setCameraPosition(Vec3f position) {
    camera.posx = position.x;
    camera.posy = position.y;
    camera.posz = position.z;
}

VANILLA_EXPORT void VANILLA_setCameraUIOffset(float offsetx, float offsety) {
    camera.uioffsetx = offsetx;
    camera.uioffsety = offsety;
}

VANILLA_EXPORT void VANILLA_setCameraTarget(Vec3f target) {
    camera.targetx = target.x;
    camera.targety = target.y;
    camera.targetz = target.z;
}

VANILLA_EXPORT void VANILLA_setCameraUp(Vec3f up) {
    camera.upx = up.x;
    camera.upy = up.y;
    camera.upz = up.z;
}

VANILLA_EXPORT void VANILLA_setCameraFOV(float degree) { camera.fov = degree * ((float)VANILLA_PI / 180.0f); }
VANILLA_EXPORT void VANILLA_setCameraNear(float nearPlane) { camera.nearPlane = nearPlane; }
VANILLA_EXPORT void VANILLA_setCameraFar(float farPlane) { camera.farPlane = farPlane; }

VANILLA_EXPORT void VANILLA_setCameraProjection(VANILLA_ProjectionType projection, float _orthographicSize) {
    projectionType = projection;
    orthographicSize = _orthographicSize;
}

VANILLA_EXPORT void VANILLA_enableVSync()  { enableVSync = true;  didVSyncSet = true; }
VANILLA_EXPORT void VANILLA_disableVSync() { enableVSync = false; didVSyncSet = true; }

VANILLA_EXPORT VANILLA_WINDOW_RESULT VANILLA_notifyNewChunkAlloc(void) {
    StandardComponent_RESULT scresult;

    scresult = StandardComponent_allocNewChunk(&materialComponent);
    if (scresult != STANDARDCOMPONENT_SUCCESS) { return 1; }// return scresult; }

    return VANILLA_WINDOW_SUCCESS;
}

VANILLA_EXPORT VANILLA_WINDOW_RESULT VANILLA_notifyLastChunkFree(void) {
    StandardComponent_RESULT scresult;

    scresult = StandardComponent_freeLastChunk(&materialComponent);
    if (scresult != STANDARDCOMPONENT_SUCCESS) { return 1; }// return scresult; }

    return VANILLA_WINDOW_SUCCESS;
}

VANILLA_EXPORT uint32_t VANILLA_getVersion() { return VANILLA_VERSION; }

VANILLA_EXPORT uint64_t VANILLA_getVertexLimit(void) { return vertexLimit; }
VANILLA_EXPORT uint64_t VANILLA_getIndexLimit(void)  { return indexLimit; }

VANILLA_EXPORT bool VANILLA_isVSyncEnabled(void) { return enableVSync; }
