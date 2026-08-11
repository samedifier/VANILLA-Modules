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

#ifndef VANILLA_WINDOW_WRAPPER_H
#define VANILLA_WINDOW_WRAPPER_H

#include "primitives.h"
#include <stdint.h>
#include <stdio.h>

#include <dlfcn.h>
#define VANILLA_DLOPEN(path)          dlopen((path), RTLD_NOW)
#define VANILLA_DLSYM(h, name, field) *(void**)(&(field)) = dlsym((h), (name))
#define VANILLA_DLCLOSE(h)            dlclose(h)

typedef enum {
    VANILLA_SUCCESS = 0,

    VANILLA_ERR_NULL_POINTER,
    VANILLA_ERR_ALLOC_FAILED,
    VANILLA_ERR_ALREADY_STARTED,
    VANILLA_ERR_CAPACITY_REACHED,

    VANILLA_ERR_TEXTURE_SIZE_LIMIT,
    VANILLA_ERR_CREATE_TEXTURE,
    VANILLA_ERR_INVALID_TEXTURE,

    VANILLA_ERR_CREATE_SAMPLER,
    VANILLA_ERR_INVALID_SAMPLER,

    VANILLA_ERR_INVALID_MATERIAL,
    VANILLA_ERR_HAS_NO_MATERIAL,
    VANILLA_ERR_ADD_MATERIAL_COMPONENT,
    VANILLA_ERR_REMOVE_MATERIAL_COMPONENT,
    VANILLA_ERR_GET_MATERIAL_COMPONENT
} VANILLA_RESULT;

typedef enum {
    VANILLA_MEMORY_HOST_VISIBLE,
    VANILLA_MEMORY_HYBRID,
    VANILLA_MEMORY_DEVICE_LOCAL
} VANILLA_MemoryMode;

typedef enum {
    VANILLA_VERTEXFORMAT_R32_FLOAT,
    VANILLA_VERTEXFORMAT_R32G32_FLOAT,
    VANILLA_VERTEXFORMAT_R32G32B32_FLOAT,
    VANILLA_VERTEXFORMAT_R32G32B32A32_FLOAT
} VANILLA_VertexFormat;

typedef struct {
    uint32_t location;
    VANILLA_VertexFormat format;
    uint32_t anycoreOffset;
    uint32_t vanillaOffset;
    uint32_t size;
} VANILLA_VertexAttribute;

typedef struct {
    const VANILLA_VertexAttribute* attributes;
    uint32_t attributeCount;
} VANILLA_VertexLayout;

typedef struct { float m[16]; } mat4;

typedef struct { ID32 slot; ID16 generation; } TextureID;
typedef struct { ID32 slot; ID16 generation; } SamplerID;
typedef struct { ID32 slot; ID16 generation; } MaterialID;

typedef enum {
    VANILLA_TEXTUREFORMAT_R8_UNORM,
    VANILLA_TEXTUREFORMAT_RG8_UNORM,
    VANILLA_TEXTUREFORMAT_RGB8_UNORM,
    VANILLA_TEXTUREFORMAT_RGBA8_UNORM,

    VANILLA_TEXTUREFORMAT_R8_SRGB,
    VANILLA_TEXTUREFORMAT_RG8_SRGB,
    VANILLA_TEXTUREFORMAT_RGB8_SRGB,
    VANILLA_TEXTUREFORMAT_RGBA8_SRGB,

    VANILLA_TEXTUREFORMAT_R16_FLOAT,
    VANILLA_TEXTUREFORMAT_RG16_FLOAT,
    VANILLA_TEXTUREFORMAT_RGBA16_FLOAT,

    VANILLA_TEXTUREFORMAT_R32_FLOAT,
    VANILLA_TEXTUREFORMAT_RG32_FLOAT,
    VANILLA_TEXTUREFORMAT_RGBA32_FLOAT,

    VANILLA_TEXTUREFORMAT_DEPTH32_FLOAT,
    VANILLA_TEXTUREFORMAT_DEPTH24_STENCIL8
} VANILLA_TextureFormat;

typedef enum {
    VANILLA_MIPMAPMODE_NONE,
    VANILLA_MIPMAPMODE_GENERATE,
    VANILLA_MIPMAPMODE_PROVIDED
} VANILLA_MipmapMode;

typedef struct {
    uint32_t width;
    uint32_t height;

    VANILLA_TextureFormat format;
    
    VANILLA_MipmapMode mipmapMode;
    uint32_t mipLevels;

    const void* data;
    uint64_t dataSize;
} VANILLA_TextureCreateInfo;

typedef enum {
    VANILLA_FILTER_NEAREST,
    VANILLA_FILTER_LINEAR
} VANILLA_Filter;

typedef enum {
    VANILLA_MIPMAPFILTER_NEAREST,
    VANILLA_MIPMAPFILTER_LINEAR
} VANILLA_MipmapFilter;

typedef enum {
    VANILLA_ADDRESS_REPEAT,
    VANILLA_ADDRESS_CLAMP_TO_EDGE,
    VANILLA_ADDRESS_CLAMP_TO_BORDER,
    VANILLA_ADDRESS_MIRRORED_REPEAT
} VANILLA_AddressMode;

typedef enum {
    VANILLA_BORDER_FLOAT_TRANSPARENT_BLACK,
    VANILLA_BORDER_INT_TRANSPARENT_BLACK,

    VANILLA_BORDER_FLOAT_OPAQUE_BLACK,
    VANILLA_BORDER_INT_OPAQUE_BLACK,

    VANILLA_BORDER_FLOAT_OPAQUE_WHITE,
    VANILLA_BORDER_INT_OPAQUE_WHITE
} VANILLA_BorderColor;

typedef struct {
    VANILLA_Filter magFilter;
    VANILLA_Filter minFilter;

    VANILLA_MipmapFilter mipmapFilter;

    VANILLA_AddressMode addressU;
    VANILLA_AddressMode addressV;
    VANILLA_AddressMode addressW;

    uint32_t maxAnisotropy;

    VANILLA_BorderColor borderColor;
} VANILLA_SamplerCreateInfo;

typedef struct {
    TextureID albedo;
    TextureID normal;
    SamplerID sampler;

    Vec4f baseColor;

    float metallic;
    float roughness;
} VANILLA_MaterialCreateInfo;

typedef enum {
    VANILLA_MATERIALFLAGS1_USE_MATERIAL = 1u << 2,
    VANILLA_MATERIALFLAGS1_MAKE_UI      = 1u << 1
} VANILLA_MaterialFlags1;

typedef enum {
    VANILLA_PROJECTION_PERSPECTIVE,
    VANILLA_PROJECTION_ORTHOGRAPHIC
} VANILLA_ProjectionType;

typedef enum {
    VANILLA_CURSORMODE_NORMAL,
    VANILLA_CURSORMODE_HIDDEN,
    VANILLA_CURSORMODE_DISABLED
} VANILLA_CursorMode;

typedef struct { ID32 slot; ID16 generation; } EventBindID;

typedef enum {
    VANILLA_INPUTEVENT_KEY_A,
    VANILLA_INPUTEVENT_KEY_B,
    VANILLA_INPUTEVENT_KEY_C,
    VANILLA_INPUTEVENT_KEY_D,
    VANILLA_INPUTEVENT_KEY_E,
    VANILLA_INPUTEVENT_KEY_F,
    VANILLA_INPUTEVENT_KEY_G,
    VANILLA_INPUTEVENT_KEY_H,
    VANILLA_INPUTEVENT_KEY_I,
    VANILLA_INPUTEVENT_KEY_J,
    VANILLA_INPUTEVENT_KEY_K,
    VANILLA_INPUTEVENT_KEY_L,
    VANILLA_INPUTEVENT_KEY_M,
    VANILLA_INPUTEVENT_KEY_N,
    VANILLA_INPUTEVENT_KEY_O,
    VANILLA_INPUTEVENT_KEY_P,
    VANILLA_INPUTEVENT_KEY_Q,
    VANILLA_INPUTEVENT_KEY_R,
    VANILLA_INPUTEVENT_KEY_S,
    VANILLA_INPUTEVENT_KEY_T,
    VANILLA_INPUTEVENT_KEY_U,
    VANILLA_INPUTEVENT_KEY_V,
    VANILLA_INPUTEVENT_KEY_W,
    VANILLA_INPUTEVENT_KEY_X,
    VANILLA_INPUTEVENT_KEY_Y,
    VANILLA_INPUTEVENT_KEY_Z,

    VANILLA_INPUTEVENT_KEY_0,
    VANILLA_INPUTEVENT_KEY_1,
    VANILLA_INPUTEVENT_KEY_2,
    VANILLA_INPUTEVENT_KEY_3,
    VANILLA_INPUTEVENT_KEY_4,
    VANILLA_INPUTEVENT_KEY_5,
    VANILLA_INPUTEVENT_KEY_6,
    VANILLA_INPUTEVENT_KEY_7,
    VANILLA_INPUTEVENT_KEY_8,
    VANILLA_INPUTEVENT_KEY_9,

    VANILLA_INPUTEVENT_KEY_SPACE,
    VANILLA_INPUTEVENT_KEY_ENTER,
    VANILLA_INPUTEVENT_KEY_ESCAPE,
    VANILLA_INPUTEVENT_KEY_TAB,
    VANILLA_INPUTEVENT_KEY_BACKSPACE,

    VANILLA_INPUTEVENT_KEY_LEFT,
    VANILLA_INPUTEVENT_KEY_RIGHT,
    VANILLA_INPUTEVENT_KEY_UP,
    VANILLA_INPUTEVENT_KEY_DOWN,

    VANILLA_INPUTEVENT_KEY_LEFT_SHIFT,
    VANILLA_INPUTEVENT_KEY_RIGHT_SHIFT,
    VANILLA_INPUTEVENT_KEY_LEFT_CONTROL,
    VANILLA_INPUTEVENT_KEY_RIGHT_CONTROL,
    VANILLA_INPUTEVENT_KEY_LEFT_ALT,
    VANILLA_INPUTEVENT_KEY_RIGHT_ALT,
    VANILLA_INPUTEVENT_KEY_LEFT_SUPER,
    VANILLA_INPUTEVENT_KEY_RIGHT_SUPER,

    VANILLA_INPUTEVENT_KEY_F1,
    VANILLA_INPUTEVENT_KEY_F2,
    VANILLA_INPUTEVENT_KEY_F3,
    VANILLA_INPUTEVENT_KEY_F4,
    VANILLA_INPUTEVENT_KEY_F5,
    VANILLA_INPUTEVENT_KEY_F6,
    VANILLA_INPUTEVENT_KEY_F7,
    VANILLA_INPUTEVENT_KEY_F8,
    VANILLA_INPUTEVENT_KEY_F9,
    VANILLA_INPUTEVENT_KEY_F10,
    VANILLA_INPUTEVENT_KEY_F11,
    VANILLA_INPUTEVENT_KEY_F12,

    VANILLA_INPUTEVENT_KEY_INSERT,
    VANILLA_INPUTEVENT_KEY_DELETE,
    VANILLA_INPUTEVENT_KEY_HOME,
    VANILLA_INPUTEVENT_KEY_END,
    VANILLA_INPUTEVENT_KEY_PAGE_UP,
    VANILLA_INPUTEVENT_KEY_PAGE_DOWN,

    VANILLA_INPUTEVENT_KEY_CAPS_LOCK,
    VANILLA_INPUTEVENT_KEY_NUM_LOCK,
    VANILLA_INPUTEVENT_KEY_SCROLL_LOCK,

    VANILLA_INPUTEVENT_KEY_PRINT_SCREEN,
    VANILLA_INPUTEVENT_KEY_PAUSE,

    VANILLA_INPUTEVENT_KEY_KP_0,
    VANILLA_INPUTEVENT_KEY_KP_1,
    VANILLA_INPUTEVENT_KEY_KP_2,
    VANILLA_INPUTEVENT_KEY_KP_3,
    VANILLA_INPUTEVENT_KEY_KP_4,
    VANILLA_INPUTEVENT_KEY_KP_5,
    VANILLA_INPUTEVENT_KEY_KP_6,
    VANILLA_INPUTEVENT_KEY_KP_7,
    VANILLA_INPUTEVENT_KEY_KP_8,
    VANILLA_INPUTEVENT_KEY_KP_9,

    VANILLA_INPUTEVENT_KEY_KP_DECIMAL,
    VANILLA_INPUTEVENT_KEY_KP_DIVIDE,
    VANILLA_INPUTEVENT_KEY_KP_MULTIPLY,
    VANILLA_INPUTEVENT_KEY_KP_SUBTRACT,
    VANILLA_INPUTEVENT_KEY_KP_ADD,
    VANILLA_INPUTEVENT_KEY_KP_ENTER,
    VANILLA_INPUTEVENT_KEY_KP_EQUAL,

    VANILLA_INPUTEVENT_MOUSE_LEFT,
    VANILLA_INPUTEVENT_MOUSE_RIGHT,
    VANILLA_INPUTEVENT_MOUSE_MIDDLE,

    VANILLA_INPUTEVENT_MOUSE_BUTTON_4,
    VANILLA_INPUTEVENT_MOUSE_BUTTON_5,
    VANILLA_INPUTEVENT_MOUSE_BUTTON_6,
    VANILLA_INPUTEVENT_MOUSE_BUTTON_7,
    VANILLA_INPUTEVENT_MOUSE_BUTTON_8,

    VANILLA_INPUTEVENT_MOUSE_MOVE,
    VANILLA_INPUTEVENT_MOUSE_SCROLL,

    VANILLA_INPUTEVENT_WINDOW_CLOSE,
    VANILLA_INPUTEVENT_WINDOW_FOCUS_GAINED,
    VANILLA_INPUTEVENT_WINDOW_FOCUS_LOST,

    VANILLA_INPUTEVENT_WINDOW_MINIMIZED,
    VANILLA_INPUTEVENT_WINDOW_MAXIMIZED,
    VANILLA_INPUTEVENT_WINDOW_RESTORED,

    VANILLA_INPUTEVENT_WINDOW_RESIZED,

    VANILLA_INPUTEVENT_LIMIT
} VANILLA_InputEvent;

typedef enum {
    VANILLA_INPUTSTATE_NONE     = 0,
    VANILLA_INPUTSTATE_ACTIVE   = 1 << 0,
    VANILLA_INPUTSTATE_PRESSED  = 1 << 1,
    VANILLA_INPUTSTATE_RELEASED = 1 << 2
} VANILLA_InputState;

typedef struct {
    /* Sets the application name. */
    VANILLA_RESULT (*setAppName)(const char* appName);

    /* Sets the window title. */
    VANILLA_RESULT (*setWindowTitle)(const char* title);

    /* Sets the application version. */
    VANILLA_RESULT (*setAppVersion)(uint32_t major, uint32_t minor, uint32_t patch);

    /* Sets the window size. */
    void (*setWindowSize)(unsigned int width, unsigned int height);

    /* Sets the window position. */
    void (*setWindowPosition)(unsigned int x, unsigned int y);

    /* Sets the cursor position within the window. */
    void (*setCursorPosition)(unsigned int x, unsigned int y);

    /* Shows the window. */
    void (*showWindow)(void);

    /* Hides the window. */
    void (*hideWindow)(void);

    /* Sets the maximum number of vertices. */
    VANILLA_RESULT (*setVertexLimit)(uint64_t limit);

    /* Sets the maximum number of indices. */
    VANILLA_RESULT (*setIndexLimit)(uint64_t limit);

    /* Sets the memory allocation mode used by VANILLA. */
    VANILLA_RESULT (*setMemoryMode)(VANILLA_MemoryMode memoryMode);

    /* Sets the vertex buffer layout. */
    VANILLA_RESULT (*setVertexLayout)(uint32_t stride, const VANILLA_VertexAttribute* attributes, uint32_t attributeCount);

    /* Sets the maximum number of materials. */
    VANILLA_RESULT (*setMaterialLimit)(uint32_t limit);

    /* Sets the maximum number of textures. */
    VANILLA_RESULT (*setTextureLimit)(uint32_t limit);

    /* Sets the maximum size of a texture upload. */
    VANILLA_RESULT (*setTextureSizeLimit)(uint32_t limit);

    /* Sets the maximum number of samplers. */
    VANILLA_RESULT (*setSamplerLimit)(uint32_t limit);

    /* Enables vertical synchronization. */
    void (*enableVSync)(void);

    /* Disables vertical synchronization. */
    void (*disableVSync)(void);

    /* Sets the camera position. */
    void (*setCameraPosition)(Vec3f position);

    /* Sets the camera target. */
    void (*setCameraTarget)(Vec3f target);

    /* Sets the camera up direction. */
    void (*setCameraUp)(Vec3f up);

    /* Sets the camera field of view in degrees. */
    void (*setCameraFOV)(float degree);

    /* Sets the camera near clipping plane. */
    void (*setCameraNear)(float nearPlane);

    /* Sets the camera far clipping plane. */
    void (*setCameraFar)(float farPlane);

    /* Sets the camera projection type and orthographic size. */
    void (*setCameraProjection)(VANILLA_ProjectionType projection, float orthographicSize);

    /* Notifies VANILLA that a new ANYCORE chunk was allocated. */
    VANILLA_RESULT (*notifyNewChunkAlloc)(void);

    /* Notifies VANILLA that the last ANYCORE chunk was freed. */
    VANILLA_RESULT (*notifyLastChunkFree)(void);

    /* Dispatches pending input events to their bound functions. */
    void (*inputDispatch)(void);

    /* Binds a function to an input event. */
    bool (*bindInputEvent)(VANILLA_InputEvent event, void (*function)(VANILLA_InputState), EventBindID* outBindID);

    /* Removes a previously created input event binding. */
    void (*unbindInputEvent)(VANILLA_InputEvent event, EventBindID id);

    /* Creates a material and returns its identifier. */
    VANILLA_RESULT (*createMaterial)(VANILLA_MaterialCreateInfo materialCreateInfo, MaterialID* outMaterialID);

    /* Destroys a material. */
    VANILLA_RESULT (*destroyMaterial)(MaterialID materialID);

    /* Assigns a material and its flags to an entity. */
    VANILLA_RESULT (*setMaterialComponent)(EntityID entityID, MaterialID materialID, uint32_t materialFlags1, uint32_t materialFlags2, uint32_t materialFlags3);

    /* Removes the material component from an entity. */
    VANILLA_RESULT (*removeMaterialComponent)(EntityID entityID);

    /* Retrieves the material assigned to an entity. */
    VANILLA_RESULT (*getMaterialComponent)(EntityID entityID, MaterialID* outMaterialID);

    /* Checks whether an entity has a material component. */
    bool (*hasMaterialComponent)(EntityID entityID);

    /* Creates a sampler and returns its identifier. */
    VANILLA_RESULT (*createSampler)(const VANILLA_SamplerCreateInfo* createInfo, SamplerID* outSamplerID);

    /* Destroys a sampler. */
    VANILLA_RESULT (*destroySampler)(SamplerID samplerID);

    /* Creates a texture and returns its identifier. */
    VANILLA_RESULT (*createTexture)(const VANILLA_TextureCreateInfo* createInfo, TextureID* outTextureID);

    /* Destroys a texture. */
    VANILLA_RESULT (*destroyTexture)(TextureID textureID);

    /* Returns the VANILLA version. */
    uint32_t (*getVersion)(void);

    /* Returns the current window title. */
    const char* (*getWindowTitle)(void);

    /* Retrieves the current window size. */
    void (*getWindowSize)(unsigned int* width, unsigned int* height);

    /* Retrieves the current window position. */
    void (*getWindowPosition)(unsigned int* x, unsigned int* y);

    /* Retrieves the current cursor position within the window. */
    void (*getCursorPosition)(unsigned int* x, unsigned int* y);

    /* Returns whether the window is currently visible. */
    bool (*isWindowVisible)(void);

    /* Retrieves the primary monitor size. */
    void (*getMonitorSize)(unsigned int* width, unsigned int* height);

    /* Returns the current vertex limit. */
    uint64_t (*getVertexLimit)(void);

    /* Returns the current index limit. */
    uint64_t (*getIndexLimit)(void);

    /* Returns the current memory allocation mode. */
    VANILLA_MemoryMode (*getMemoryMode)(void);

    /* Returns whether vertical synchronization is enabled. */
    bool (*isVSyncEnabled)(void);
} VANILLA_WINDOW_Wrapper;

static inline bool VANILLA_WINDOW_Wrapper_init(VANILLA_WINDOW_Wrapper* w, void* libHandle) {
    if (!w || !libHandle) return false;

    VANILLA_DLSYM(libHandle, "VANILLA_setAppName", w->setAppName);
    VANILLA_DLSYM(libHandle, "VANILLA_setWindowTitle", w->setWindowTitle);
    VANILLA_DLSYM(libHandle, "VANILLA_setAppVersion", w->setAppVersion);

    VANILLA_DLSYM(libHandle, "VANILLA_setWindowSize", w->setWindowSize);
    VANILLA_DLSYM(libHandle, "VANILLA_setWindowPosition", w->setWindowPosition);

    VANILLA_DLSYM(libHandle, "VANILLA_setCursorPosition", w->setCursorPosition);

    VANILLA_DLSYM(libHandle, "VANILLA_showWindow", w->showWindow);
    VANILLA_DLSYM(libHandle, "VANILLA_hideWindow", w->hideWindow);

    VANILLA_DLSYM(libHandle, "VANILLA_setVertexLimit", w->setVertexLimit);
    VANILLA_DLSYM(libHandle, "VANILLA_setIndexLimit", w->setIndexLimit);

    VANILLA_DLSYM(libHandle, "VANILLA_setMemoryMode", w->setMemoryMode);
    VANILLA_DLSYM(libHandle, "VANILLA_setVertexLayout", w->setVertexLayout);

    VANILLA_DLSYM(libHandle, "VANILLA_setMaterialLimit", w->setMaterialLimit);
    VANILLA_DLSYM(libHandle, "VANILLA_setTextureLimit", w->setTextureLimit);
    VANILLA_DLSYM(libHandle, "VANILLA_setTextureSizeLimit", w->setTextureSizeLimit);
    VANILLA_DLSYM(libHandle, "VANILLA_setSamplerLimit", w->setSamplerLimit);

    VANILLA_DLSYM(libHandle, "VANILLA_enableVSync", w->enableVSync);
    VANILLA_DLSYM(libHandle, "VANILLA_disableVSync", w->disableVSync);

    VANILLA_DLSYM(libHandle, "VANILLA_setCameraPosition", w->setCameraPosition);
    VANILLA_DLSYM(libHandle, "VANILLA_setCameraTarget", w->setCameraTarget);
    VANILLA_DLSYM(libHandle, "VANILLA_setCameraUp", w->setCameraUp);

    VANILLA_DLSYM(libHandle, "VANILLA_setCameraFOV", w->setCameraFOV);
    VANILLA_DLSYM(libHandle, "VANILLA_setCameraNear", w->setCameraNear);
    VANILLA_DLSYM(libHandle, "VANILLA_setCameraFar", w->setCameraFar);

    VANILLA_DLSYM(libHandle, "VANILLA_setCameraProjection", w->setCameraProjection);

    VANILLA_DLSYM(libHandle, "VANILLA_notifyNewChunkAlloc", w->notifyNewChunkAlloc);
    VANILLA_DLSYM(libHandle, "VANILLA_notifyLastChunkFree", w->notifyLastChunkFree);

    VANILLA_DLSYM(libHandle, "VANILLA_inputDispatch", w->inputDispatch);
    VANILLA_DLSYM(libHandle, "VANILLA_bindInputEvent", w->bindInputEvent);
    VANILLA_DLSYM(libHandle, "VANILLA_unbindInputEvent", w->unbindInputEvent);

    VANILLA_DLSYM(libHandle, "VANILLA_createMaterial", w->createMaterial);
    VANILLA_DLSYM(libHandle, "VANILLA_destroyMaterial", w->destroyMaterial);
    VANILLA_DLSYM(libHandle, "VANILLA_setMaterialComponent", w->setMaterialComponent);
    VANILLA_DLSYM(libHandle, "VANILLA_removeMaterialComponent", w->removeMaterialComponent);
    VANILLA_DLSYM(libHandle, "VANILLA_getMaterialComponent", w->getMaterialComponent);
    VANILLA_DLSYM(libHandle, "VANILLA_hasMaterialComponent", w->hasMaterialComponent);

    VANILLA_DLSYM(libHandle, "VANILLA_createSampler", w->createSampler);
    VANILLA_DLSYM(libHandle, "VANILLA_destroySampler", w->destroySampler);

    VANILLA_DLSYM(libHandle, "VANILLA_createTexture", w->createTexture);
    VANILLA_DLSYM(libHandle, "VANILLA_destroyTexture", w->destroyTexture);

    VANILLA_DLSYM(libHandle, "VANILLA_getVersion", w->getVersion);

    VANILLA_DLSYM(libHandle, "VANILLA_getWindowTitle", w->getWindowTitle);
    VANILLA_DLSYM(libHandle, "VANILLA_getWindowSize", w->getWindowSize);
    VANILLA_DLSYM(libHandle, "VANILLA_getWindowPosition", w->getWindowPosition);

    VANILLA_DLSYM(libHandle, "VANILLA_getCursorPosition", w->getCursorPosition);

    VANILLA_DLSYM(libHandle, "VANILLA_isWindowVisible", w->isWindowVisible);
    VANILLA_DLSYM(libHandle, "VANILLA_getMonitorSize", w->getMonitorSize);

    VANILLA_DLSYM(libHandle, "VANILLA_getVertexLimit", w->getVertexLimit);
    VANILLA_DLSYM(libHandle, "VANILLA_getIndexLimit", w->getIndexLimit);

    VANILLA_DLSYM(libHandle, "VANILLA_getMemoryMode", w->getMemoryMode);
    VANILLA_DLSYM(libHandle, "VANILLA_isVSyncEnabled", w->isVSyncEnabled);

    return true;
}

static inline void VANILLA_WINDOW_injectWrapper(VANILLA_WINDOW_Wrapper* w, void* behaviourHandle) {
    void (*injectWrapper)(void*) = NULL;
    VANILLA_DLSYM(behaviourHandle, "injectVANILLAWINDOWWrapper", injectWrapper);
    if (injectWrapper) injectWrapper(w);
}

#endif // VANILLA_WINDOW_WRAPPER_H
