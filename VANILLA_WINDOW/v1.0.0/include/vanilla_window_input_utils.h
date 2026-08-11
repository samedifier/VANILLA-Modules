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

#ifndef VANILLA_WINDOW_INPUT_UTILS_H
#define VANILLA_WINDOW_INPUT_UTILS_H

#include "vanilla_window_input.h"

void VANILLA_InputInit(void* window);
void VANILLA_InputFree(void);
void VANILLA_InputPoll(void);

VANILLA_EXPORT void VANILLA_inputDispatch(void);
VANILLA_EXPORT bool VANILLA_bindInputEvent(VANILLA_InputEvent event, void (*function)(VANILLA_InputState), EventBindID* outBindID);
VANILLA_EXPORT void VANILLA_unbindInputEvent(VANILLA_InputEvent event, EventBindID id);

#endif // VANILLA_WINDOW_INPUT_UTILS_H