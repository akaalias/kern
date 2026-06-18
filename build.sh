#!/bin/bash
# Command-line build for MicroEdit. For an Xcode build, open MicroEdit.xcodeproj instead.
set -e
cd "$(dirname "$0")"

OS_NAME=`uname -o 2>/dev/null || uname -s`

if [ "$OS_NAME" == "Msys" ]; then
    GLFLAG="-lopengl32"
    OBJC_SRC=""
    OBJC_FLAGS=""
elif [ "$OS_NAME" == "Darwin" ]; then
    GLFLAG="-framework OpenGL"
    OBJC_SRC="src/macos_style.m"
    OBJC_FLAGS="-framework Cocoa"
else
    GLFLAG="-lGL"
    OBJC_SRC=""
    OBJC_FLAGS=""
fi

INCLUDES="-Isrc -Ivendor/microui -Ivendor/stb -Iassets/fonts"
# Code uses #include <SDL2/SDL.h>, so put the include *prefix* on the path
# (sdl2-config --cflags points inside the SDL2 dir, which doesn't satisfy that form).
SDL_CFLAGS="-I`sdl2-config --prefix`/include `sdl2-config --cflags`"
CFLAGS="$INCLUDES $SDL_CFLAGS -Wall -std=c11 -O3 -g"
LDFLAGS="`sdl2-config --libs` $GLFLAG $OBJC_FLAGS -lm"

SRC="src/textview.c src/buffer.c src/navigation.c src/editing.c src/undo.c \
     src/md_render.c src/renderer.c vendor/microui/microui.c $OBJC_SRC"

gcc $SRC $CFLAGS $LDFLAGS -o microedit

# Fonts are loaded via SDL_GetBasePath() (the binary's directory for a CLI build),
# so copy them next to the binary.
cp assets/fonts/*.ttf .

echo "Built ./microedit"
echo "Run: ./microedit testdata/test_markdown.txt"
