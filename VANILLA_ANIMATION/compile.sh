#!/bin/bash
set -e

CORE_INCLUDE="include"
CORE_SRC="src"

EXTRA_INCLUDE="."

CORE_OUT="build/vanilla_animation.so"

CC=gcc

rm -rf build; mkdir build

$CC -shared -fPIC \
    $CORE_SRC/main.c \
    $CORE_SRC/vanilla_animation.c \
    -I$CORE_INCLUDE \
    -I$EXTRA_INCLUDE \
    -Wall -O3 \
    -o $CORE_OUT \
    -lm

echo "done"