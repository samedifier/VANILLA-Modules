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

#ifndef VANILLA_MATH_H
#define VANILLA_MATH_H

#include <math.h>
#include "main.h"

static inline void VANILLA_MATH_identity(mat4* out) {
    for (int i = 0; i < 16; i++) {
        out->m[i] = 0.0f;
    }

    out->m[0]  = 1.0f;
    out->m[5]  = 1.0f;
    out->m[10] = 1.0f;
    out->m[15] = 1.0f;
}

static inline void VANILLA_MATH_perspective(mat4* out, float fov, float aspect, float znear, float zfar) {
    float f = 1.0f / tanf(fov * 0.5f);

    for (int i = 0; i < 16; i++) {
        out->m[i] = 0.0f;
    }

    out->m[0] = f / aspect;
    out->m[5] = -f;

    out->m[10] = zfar / (znear - zfar);
    out->m[11] = -1.0f;
    out->m[14] = (znear * zfar) / (znear - zfar);
    out->m[15] = 0.0f;
}

static inline void VANILLA_MATH_orthographic(
    mat4 *out,
    float left,
    float right,
    float bottom,
    float top,
    float znear,
    float zfar
) {
    for (int i = 0; i < 16; i++) {
        out->m[i] = 0.0f;
    }

    out->m[0] = 2.0f / (right - left);
    out->m[5] = 2.0f / (top - bottom);
    out->m[10] = 1.0f / (znear - zfar);

    out->m[12] = -(right + left) / (right - left);
    out->m[13] = -(top + bottom) / (top - bottom);
    out->m[14] = znear / (znear - zfar);

    out->m[15] = 1.0f;
}

static inline void VANILLA_MATH_lookAt(mat4* out, float eye_x, float eye_y, float eye_z, float center_x, float center_y, float center_z, float up_x, float up_y, float up_z) {
    float fx = center_x - eye_x;
    float fy = center_y - eye_y;
    float fz = center_z - eye_z;

    float flen = sqrtf(fx * fx + fy * fy + fz * fz);
    fx /= flen;
    fy /= flen;
    fz /= flen;

    float sx = fy * up_z - fz * up_y;
    float sy = fz * up_x - fx * up_z;
    float sz = fx * up_y - fy * up_x;

    float slen = sqrtf(sx * sx + sy * sy + sz * sz);
    sx /= slen;
    sy /= slen;
    sz /= slen;

    float ux = sy * fz - sz * fy;
    float uy = sz * fx - sx * fz;
    float uz = sx * fy - sy * fx;

    VANILLA_MATH_identity(out);

    out->m[0] = sx;
    out->m[1] = ux;
    out->m[2] = -fx;

    out->m[4] = sy;
    out->m[5] = uy;
    out->m[6] = -fy;

    out->m[8]  = sz;
    out->m[9]  = uz;
    out->m[10] = -fz;

    out->m[12] = -(sx * eye_x + sy * eye_y + sz * eye_z);
    out->m[13] = -(ux * eye_x + uy * eye_y + uz * eye_z);
    out->m[14] =  (fx * eye_x + fy * eye_y + fz * eye_z);
}

static inline void VANILLA_MATH_multiply(mat4* out, const mat4* a, const mat4* b) {
    mat4 tmp;

    for (int c = 0; c < 4; c++) {
        for (int r = 0; r < 4; r++) {
            tmp.m[c * 4 + r] =
                a->m[0 * 4 + r] * b->m[c * 4 + 0] +
                a->m[1 * 4 + r] * b->m[c * 4 + 1] +
                a->m[2 * 4 + r] * b->m[c * 4 + 2] +
                a->m[3 * 4 + r] * b->m[c * 4 + 3];
        }
    }

    *out = tmp;
}

#endif