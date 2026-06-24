# tests/Makefile — headless test build for Kern's portable core.
#
# Compiles only the pure EditorState code (buffer/editing/undo) plus the SDL
# and microui *headers* (needed for typedefs via editor_types.h). No SDL
# runtime, no OpenGL, no Cocoa — so it runs anywhere, including Linux CI.
# Built under AddressSanitizer + UndefinedBehaviorSanitizer; LeakSanitizer
# activates automatically on Linux.

CC      ?= cc
ROOT    := ..
APP     := $(ROOT)/application/Kern/Kern
VENDOR  := $(ROOT)/application/Kern/Vendor

# SDL is needed for headers only (typedefs via editor_types.h), never linked.
# Defaults to the macOS-configured vendored headers; CI overrides this with
# `SDL_CFLAGS=` to use a Linux libsdl2-dev install on the default include path.
SDL_CFLAGS ?= -I$(VENDOR)/SDL2/include

SAN     := -fsanitize=address,undefined -fno-omit-frame-pointer
INCS    := -I$(APP)/Editor -I$(APP)/ThirdParty $(SDL_CFLAGS)
CFLAGS  := -std=c11 -g -O1 -Wall -Wextra -Wno-unused-parameter $(SAN) $(INCS)
LDFLAGS := $(SAN)

CORE    := $(APP)/Editor/buffer.c $(APP)/Editor/editing.c $(APP)/Editor/undo.c
TESTS   := test_main.c unit_editing.c unit_undo.c
SRC     := $(CORE) $(TESTS)

BIN     := kern_tests

$(BIN): $(SRC) test.h
	$(CC) $(CFLAGS) -o $@ $(CORE) $(TESTS) $(LDFLAGS)

.PHONY: test clean
test: $(BIN)
	UBSAN_OPTIONS=halt_on_error=1:print_stacktrace=1 ./$(BIN)

clean:
	rm -rf $(BIN) $(BIN).dSYM
