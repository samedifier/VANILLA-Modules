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

#include "vanilla_window_input_utils.h"

#include <GLFW/glfw3.h>
#include <stdlib.h>

uint32_t eventFunctionLimit = 16;

static VANILLA_InputEventList input;

static void VANILLA_InputPushEvent(VANILLA_InputEvent event, VANILLA_InputState state) {
    if (event >= VANILLA_INPUTEVENT_LIMIT) {
        return;
    }

    if (input.eventFlags[event]) {
        for (uint32_t i = 0; i < input.count; i++) {
            if (input.events[i] == event) {
                input.states[i] = state;
                return;
            }
        }
        return;
    }

    if (input.count >= VANILLA_INPUTEVENT_LIMIT) {
        return;
    }

    input.events[input.count] = event;
    input.states[input.count] = state;

    input.eventFlags[event] = true;
    input.count++;
}


static void VANILLA_InputRemoveEvent(VANILLA_InputEvent event) {
    if (event >= VANILLA_INPUTEVENT_LIMIT) { return; }
    if (!input.eventFlags[event]) { return; }

    for (uint32_t i = 0; i < input.count; i++) {
        if (input.events[i] == event) {
            input.count--;

            input.events[i] = input.events[input.count];
            input.states[i] = input.states[input.count];
            input.eventFlags[event] = false;
            return;
        }
    }
}

static VANILLA_InputEvent VANILLA_InputKeyToEvent(int key) {
    switch (key) {
        case GLFW_KEY_A: return VANILLA_INPUTEVENT_KEY_A;
        case GLFW_KEY_B: return VANILLA_INPUTEVENT_KEY_B;
        case GLFW_KEY_C: return VANILLA_INPUTEVENT_KEY_C;
        case GLFW_KEY_D: return VANILLA_INPUTEVENT_KEY_D;
        case GLFW_KEY_E: return VANILLA_INPUTEVENT_KEY_E;
        case GLFW_KEY_F: return VANILLA_INPUTEVENT_KEY_F;
        case GLFW_KEY_G: return VANILLA_INPUTEVENT_KEY_G;
        case GLFW_KEY_H: return VANILLA_INPUTEVENT_KEY_H;
        case GLFW_KEY_I: return VANILLA_INPUTEVENT_KEY_I;
        case GLFW_KEY_J: return VANILLA_INPUTEVENT_KEY_J;
        case GLFW_KEY_K: return VANILLA_INPUTEVENT_KEY_K;
        case GLFW_KEY_L: return VANILLA_INPUTEVENT_KEY_L;
        case GLFW_KEY_M: return VANILLA_INPUTEVENT_KEY_M;
        case GLFW_KEY_N: return VANILLA_INPUTEVENT_KEY_N;
        case GLFW_KEY_O: return VANILLA_INPUTEVENT_KEY_O;
        case GLFW_KEY_P: return VANILLA_INPUTEVENT_KEY_P;
        case GLFW_KEY_Q: return VANILLA_INPUTEVENT_KEY_Q;
        case GLFW_KEY_R: return VANILLA_INPUTEVENT_KEY_R;
        case GLFW_KEY_S: return VANILLA_INPUTEVENT_KEY_S;
        case GLFW_KEY_T: return VANILLA_INPUTEVENT_KEY_T;
        case GLFW_KEY_U: return VANILLA_INPUTEVENT_KEY_U;
        case GLFW_KEY_V: return VANILLA_INPUTEVENT_KEY_V;
        case GLFW_KEY_W: return VANILLA_INPUTEVENT_KEY_W;
        case GLFW_KEY_X: return VANILLA_INPUTEVENT_KEY_X;
        case GLFW_KEY_Y: return VANILLA_INPUTEVENT_KEY_Y;
        case GLFW_KEY_Z: return VANILLA_INPUTEVENT_KEY_Z;

        case GLFW_KEY_0: return VANILLA_INPUTEVENT_KEY_0;
        case GLFW_KEY_1: return VANILLA_INPUTEVENT_KEY_1;
        case GLFW_KEY_2: return VANILLA_INPUTEVENT_KEY_2;
        case GLFW_KEY_3: return VANILLA_INPUTEVENT_KEY_3;
        case GLFW_KEY_4: return VANILLA_INPUTEVENT_KEY_4;
        case GLFW_KEY_5: return VANILLA_INPUTEVENT_KEY_5;
        case GLFW_KEY_6: return VANILLA_INPUTEVENT_KEY_6;
        case GLFW_KEY_7: return VANILLA_INPUTEVENT_KEY_7;
        case GLFW_KEY_8: return VANILLA_INPUTEVENT_KEY_8;
        case GLFW_KEY_9: return VANILLA_INPUTEVENT_KEY_9;

        case GLFW_KEY_SPACE:     return VANILLA_INPUTEVENT_KEY_SPACE;
        case GLFW_KEY_ENTER:     return VANILLA_INPUTEVENT_KEY_ENTER;
        case GLFW_KEY_ESCAPE:    return VANILLA_INPUTEVENT_KEY_ESCAPE;
        case GLFW_KEY_TAB:       return VANILLA_INPUTEVENT_KEY_TAB;
        case GLFW_KEY_BACKSPACE: return VANILLA_INPUTEVENT_KEY_BACKSPACE;

        case GLFW_KEY_LEFT:  return VANILLA_INPUTEVENT_KEY_LEFT;
        case GLFW_KEY_RIGHT: return VANILLA_INPUTEVENT_KEY_RIGHT;
        case GLFW_KEY_UP:    return VANILLA_INPUTEVENT_KEY_UP;
        case GLFW_KEY_DOWN:  return VANILLA_INPUTEVENT_KEY_DOWN;

        case GLFW_KEY_LEFT_SHIFT:    return VANILLA_INPUTEVENT_KEY_LEFT_SHIFT;
        case GLFW_KEY_RIGHT_SHIFT:   return VANILLA_INPUTEVENT_KEY_RIGHT_SHIFT;
        case GLFW_KEY_LEFT_CONTROL:  return VANILLA_INPUTEVENT_KEY_LEFT_CONTROL;
        case GLFW_KEY_RIGHT_CONTROL: return VANILLA_INPUTEVENT_KEY_RIGHT_CONTROL;
        case GLFW_KEY_LEFT_ALT:      return VANILLA_INPUTEVENT_KEY_LEFT_ALT;
        case GLFW_KEY_RIGHT_ALT:     return VANILLA_INPUTEVENT_KEY_RIGHT_ALT;
        case GLFW_KEY_LEFT_SUPER:    return VANILLA_INPUTEVENT_KEY_LEFT_SUPER;
        case GLFW_KEY_RIGHT_SUPER:   return VANILLA_INPUTEVENT_KEY_RIGHT_SUPER;

        case GLFW_KEY_F1:  return VANILLA_INPUTEVENT_KEY_F1;
        case GLFW_KEY_F2:  return VANILLA_INPUTEVENT_KEY_F2;
        case GLFW_KEY_F3:  return VANILLA_INPUTEVENT_KEY_F3;
        case GLFW_KEY_F4:  return VANILLA_INPUTEVENT_KEY_F4;
        case GLFW_KEY_F5:  return VANILLA_INPUTEVENT_KEY_F5;
        case GLFW_KEY_F6:  return VANILLA_INPUTEVENT_KEY_F6;
        case GLFW_KEY_F7:  return VANILLA_INPUTEVENT_KEY_F7;
        case GLFW_KEY_F8:  return VANILLA_INPUTEVENT_KEY_F8;
        case GLFW_KEY_F9:  return VANILLA_INPUTEVENT_KEY_F9;
        case GLFW_KEY_F10: return VANILLA_INPUTEVENT_KEY_F10;
        case GLFW_KEY_F11: return VANILLA_INPUTEVENT_KEY_F11;
        case GLFW_KEY_F12: return VANILLA_INPUTEVENT_KEY_F12;

        case GLFW_KEY_INSERT:    return VANILLA_INPUTEVENT_KEY_INSERT;
        case GLFW_KEY_DELETE:    return VANILLA_INPUTEVENT_KEY_DELETE;
        case GLFW_KEY_HOME:      return VANILLA_INPUTEVENT_KEY_HOME;
        case GLFW_KEY_END:       return VANILLA_INPUTEVENT_KEY_END;
        case GLFW_KEY_PAGE_UP:   return VANILLA_INPUTEVENT_KEY_PAGE_UP;
        case GLFW_KEY_PAGE_DOWN: return VANILLA_INPUTEVENT_KEY_PAGE_DOWN;

        case GLFW_KEY_CAPS_LOCK:   return VANILLA_INPUTEVENT_KEY_CAPS_LOCK;
        case GLFW_KEY_NUM_LOCK:    return VANILLA_INPUTEVENT_KEY_NUM_LOCK;
        case GLFW_KEY_SCROLL_LOCK: return VANILLA_INPUTEVENT_KEY_SCROLL_LOCK;

        case GLFW_KEY_PRINT_SCREEN: return VANILLA_INPUTEVENT_KEY_PRINT_SCREEN;
        case GLFW_KEY_PAUSE:        return VANILLA_INPUTEVENT_KEY_PAUSE;

        case GLFW_KEY_KP_0: return VANILLA_INPUTEVENT_KEY_KP_0;
        case GLFW_KEY_KP_1: return VANILLA_INPUTEVENT_KEY_KP_1;
        case GLFW_KEY_KP_2: return VANILLA_INPUTEVENT_KEY_KP_2;
        case GLFW_KEY_KP_3: return VANILLA_INPUTEVENT_KEY_KP_3;
        case GLFW_KEY_KP_4: return VANILLA_INPUTEVENT_KEY_KP_4;
        case GLFW_KEY_KP_5: return VANILLA_INPUTEVENT_KEY_KP_5;
        case GLFW_KEY_KP_6: return VANILLA_INPUTEVENT_KEY_KP_6;
        case GLFW_KEY_KP_7: return VANILLA_INPUTEVENT_KEY_KP_7;
        case GLFW_KEY_KP_8: return VANILLA_INPUTEVENT_KEY_KP_8;
        case GLFW_KEY_KP_9: return VANILLA_INPUTEVENT_KEY_KP_9;

        case GLFW_KEY_KP_DECIMAL:
            return VANILLA_INPUTEVENT_KEY_KP_DECIMAL;

        case GLFW_KEY_KP_DIVIDE:
            return VANILLA_INPUTEVENT_KEY_KP_DIVIDE;

        case GLFW_KEY_KP_MULTIPLY:
            return VANILLA_INPUTEVENT_KEY_KP_MULTIPLY;

        case GLFW_KEY_KP_SUBTRACT:
            return VANILLA_INPUTEVENT_KEY_KP_SUBTRACT;

        case GLFW_KEY_KP_ADD:
            return VANILLA_INPUTEVENT_KEY_KP_ADD;

        case GLFW_KEY_KP_ENTER:
            return VANILLA_INPUTEVENT_KEY_KP_ENTER;

        case GLFW_KEY_KP_EQUAL:
            return VANILLA_INPUTEVENT_KEY_KP_EQUAL;

        default:
            return VANILLA_INPUTEVENT_LIMIT;
    }
}

static void VANILLA_InputKeyCallback(GLFWwindow* window, int key, int scancode, int action, int mods) {
    VANILLA_InputEvent event = VANILLA_InputKeyToEvent(key);

    if (event >= VANILLA_INPUTEVENT_LIMIT) { return; }

    switch (action) {
        case GLFW_PRESS:
            input.isHeld[event] = true;
            VANILLA_InputPushEvent(event, VANILLA_INPUTSTATE_PRESSED);
            break;

        case GLFW_RELEASE:
            input.isHeld[event] = false;
            VANILLA_InputPushEvent(event, VANILLA_INPUTSTATE_RELEASED);
            break;

        case GLFW_REPEAT:
            break;

        default:
            break;
    }
}

static void VANILLA_InputMouseButtonCallback(GLFWwindow* window, int button, int action, int mods) {
    VANILLA_InputEvent event;

    switch (button) {
        case GLFW_MOUSE_BUTTON_LEFT:
            event = VANILLA_INPUTEVENT_MOUSE_LEFT;
            break;

        case GLFW_MOUSE_BUTTON_RIGHT:
            event = VANILLA_INPUTEVENT_MOUSE_RIGHT;
            break;

        default:
            return;
    }

    switch (action) {
        case GLFW_PRESS:
            input.isHeld[event] = true;
            VANILLA_InputPushEvent(event, VANILLA_INPUTSTATE_PRESSED );
            break;

        case GLFW_RELEASE:
            input.isHeld[event] = false;
            VANILLA_InputPushEvent(event, VANILLA_INPUTSTATE_RELEASED);
            break;
        default:
            break;
    }
}

static void VANILLA_InputCursorPosCallback(GLFWwindow* window, double x, double y) {
    VANILLA_InputPushEvent(VANILLA_INPUTEVENT_MOUSE_MOVE, VANILLA_INPUTSTATE_ACTIVE);
}

void VANILLA_InputFree(void) {
    if (input.bindings) {
        for (uint32_t i = 0; i < VANILLA_INPUTEVENT_LIMIT; i++) {
            if (input.bindings[i].functions) { VANILLA_munmap(input.bindings[i].functions, eventFunctionLimit * sizeof(void (*)(VANILLA_InputState))); }
            if (input.bindings[i].generations) { VANILLA_munmap(input.bindings[i].generations, eventFunctionLimit * sizeof(uint16_t)); }
        }
        VANILLA_munmap(input.bindings, VANILLA_INPUTEVENT_LIMIT * sizeof(VANILLA_InputEventBinding));
    }

    if (input.events) { VANILLA_munmap(input.events, VANILLA_INPUTEVENT_LIMIT * sizeof(VANILLA_InputEvent)); }
    if (input.states) { VANILLA_munmap(input.states, VANILLA_INPUTEVENT_LIMIT * sizeof(VANILLA_InputState)); }
    if (input.isHeld) { VANILLA_munmap(input.isHeld, VANILLA_INPUTEVENT_LIMIT * sizeof(bool)); }
    if (input.eventFlags) { VANILLA_munmap(input.eventFlags, VANILLA_INPUTEVENT_LIMIT * sizeof(bool)); }

    input.events = NULL;
    input.states = NULL;
    input.isHeld = NULL;
    input.eventFlags = NULL;
    input.bindings = NULL;
    input.count = 0;
}

void VANILLA_InputInit(void* window) {
    GLFWwindow* glfwWindow = (GLFWwindow*)window;

    input.events = VANILLA_mmap(VANILLA_INPUTEVENT_LIMIT * sizeof(VANILLA_InputEvent));
    input.states = VANILLA_mmap(VANILLA_INPUTEVENT_LIMIT * sizeof(VANILLA_InputState));
    input.isHeld = VANILLA_mmap(VANILLA_INPUTEVENT_LIMIT * sizeof(bool));
    input.eventFlags = VANILLA_mmap(VANILLA_INPUTEVENT_LIMIT * sizeof(bool));
    input.bindings = VANILLA_mmap(VANILLA_INPUTEVENT_LIMIT * sizeof(VANILLA_InputEventBinding));

    if (!input.events || !input.states || !input.isHeld || !input.eventFlags || !input.bindings) { VANILLA_InputFree(); return; }
    input.count = 0;

    for (uint32_t i = 0; i < VANILLA_INPUTEVENT_LIMIT; i++) {
        input.isHeld[i] = false;
        input.eventFlags[i] = false;

        input.bindings[i].functionCount = 0;

        input.bindings[i].functions = VANILLA_mmap(eventFunctionLimit * sizeof(void (*)(VANILLA_InputState)));
        input.bindings[i].generations = VANILLA_mmap(eventFunctionLimit * sizeof(uint16_t));

        if (!input.bindings[i].functions || !input.bindings[i].generations) { VANILLA_InputFree(); return; }

        for (uint32_t j = 0; j < eventFunctionLimit; j++) {
            input.bindings[i].functions[j] = NULL;
            input.bindings[i].generations[j] = 0;
        }
    }

    glfwSetKeyCallback(glfwWindow, VANILLA_InputKeyCallback);
    glfwSetMouseButtonCallback(glfwWindow, VANILLA_InputMouseButtonCallback);
    glfwSetCursorPosCallback(glfwWindow, VANILLA_InputCursorPosCallback);
}

void VANILLA_InputPoll(void) {
    glfwPollEvents();
}

VANILLA_EXPORT void VANILLA_inputDispatch(void) {
    uint32_t i = 0;

    while (i < input.count) {
        VANILLA_InputEvent event = input.events[i];
        VANILLA_InputState state = input.states[i];
        VANILLA_InputEventBinding* binding = &input.bindings[event];

        for (uint32_t j = 0; j < eventFunctionLimit; j++) {
            if (!binding->functions[j]) { continue; }
            binding->functions[j](state);
        }

        if (state == VANILLA_INPUTSTATE_RELEASED) {
            VANILLA_InputRemoveEvent(event);
            continue;
        }

        if (input.isHeld[event]) {
            input.states[i] = VANILLA_INPUTSTATE_ACTIVE;
            i++;
            continue;
        }

        VANILLA_InputRemoveEvent(event);
    }
}

VANILLA_EXPORT bool VANILLA_bindInputEvent( VANILLA_InputEvent event, void (*function)(VANILLA_InputState), EventBindID* outBindID) {
    if (event >= VANILLA_INPUTEVENT_LIMIT) { return false; }
    if (!function) { return false; }

    VANILLA_InputEventBinding* binding = &input.bindings[event];

    for (uint32_t i = 0; i < eventFunctionLimit; i++) {
        if (binding->functions[i]) { continue; }

        binding->functions[i] = function;
        binding->functionCount++;

        if (outBindID != NULL) {
            outBindID->slot = i;
            outBindID->generation = binding->generations[i];
        }

        return true;
    }

    return false;
}

VANILLA_EXPORT void VANILLA_unbindInputEvent(VANILLA_InputEvent event, EventBindID id) {
    if (event >= VANILLA_INPUTEVENT_LIMIT) { return; }
    VANILLA_InputEventBinding* binding = &input.bindings[event];

    if (id.slot >= eventFunctionLimit) { return; }
    if (!binding->functions[id.slot]) { return; }
    if (binding->generations[id.slot] != id.generation) { return; }

    binding->functions[id.slot] = NULL;
    binding->functionCount--;

    binding->generations[id.slot]++;
}