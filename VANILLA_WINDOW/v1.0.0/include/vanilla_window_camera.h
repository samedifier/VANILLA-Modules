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

#ifndef VANILLA_WINDOW_CAMERA
#define VANILLA_WINDOW_CAMERA

typedef struct {
    float posx, posy, posz;
    float targetx, targety, targetz;
    float upx, upy, upz;

    float fov;
    float nearPlane;
    float farPlane;
} VANILLA_Camera;

extern VANILLA_Camera camera;

#endif // VANILLA_WINDOW_CAMERA