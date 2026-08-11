#!/bin/bash
set -e

CORE_INCLUDE="include"
CORE_SRC="src"

EXTRA_INCLUDE="."

CORE_OUT="build/vanilla_window.so"

CC=gcc

rm -rf build; mkdir build

$CC -shared -fPIC \
    $CORE_SRC/main.c \
    $CORE_SRC/vanilla_window_glfw.c \
    $CORE_SRC/vanilla_window_vulkan_instance.c \
    $CORE_SRC/vanilla_window_vulkan_surface.c \
    $CORE_SRC/vanilla_window_vulkan_device.c \
    $CORE_SRC/vanilla_window_vulkan_swapchain.c \
    $CORE_SRC/vanilla_window_vulkan_pipelinelayout.c \
    $CORE_SRC/vanilla_window_vulkan_pipeline.c \
    $CORE_SRC/vanilla_window_vulkan_commandpool.c \
    $CORE_SRC/vanilla_window_vulkan_commandbuffer.c \
    $CORE_SRC/vanilla_window_vulkan_sync.c \
    $CORE_SRC/vanilla_window_vulkan_draw.c \
    $CORE_SRC/vanilla_analyzer.c \
    $CORE_SRC/vanilla_window_vulkan_buffers.c \
    $CORE_SRC/vanilla_window_vulkan_descriptor.c \
    $CORE_SRC/vanilla_window_vulkan_depth.c \
    $CORE_SRC/vanilla_window_vulkan_memory.c \
    $CORE_SRC/vanilla_window_vulkan_texture.c \
    $CORE_SRC/vanilla_window_vulkan_sampler.c \
    $CORE_SRC/vanilla_window_vulkan_material.c \
    $CORE_SRC/vanilla_window_input.c \
    -I$CORE_INCLUDE \
    -I$EXTRA_INCLUDE \
    -Wall -O3 \
    -o $CORE_OUT \
    -lglfw -lvulkan -lm

echo "done"
