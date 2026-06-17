#!/bin/bash

OS_NAME=`uname -o 2>/dev/null || uname -s`

if [ $OS_NAME == "Msys" ]; then
    GLFLAG="-lopengl32"
    OBJC_SRC=""
    OBJC_FLAGS=""
elif [ $OS_NAME == "Darwin" ]; then
    GLFLAG="-framework OpenGL"
    OBJC_SRC="macos_style.m"
    OBJC_FLAGS="-framework Cocoa"
else
    GLFLAG="-lGL"
    OBJC_SRC=""
    OBJC_FLAGS=""
fi

CFLAGS="-I../src -I/opt/homebrew/include -Wall `sdl2-config --libs` $GLFLAG $OBJC_FLAGS -lm -O3 -g"

gcc -std=c11 -pedantic textview.c buffer.c renderer.c ../src/microui.c $CFLAGS $OBJC_SRC -o textview

# rebuild .app bundle if on macOS (skip if already bundling)
if [ $OS_NAME == "Darwin" ] && [ -z "$BUNDLING" ]; then
    export BUNDLING=1
    bash bundle_app.sh
fi
