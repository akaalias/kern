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
INCS    := -I$(APP)/Editor -I$(APP)/Platform -I$(APP)/ThirdParty $(SDL_CFLAGS)
# _GNU_SOURCE exposes POSIX helpers (mkstemp/unlink/mkdir) that glibc hides
# under -std=c11; harmless on macOS where they're visible by default.
CFLAGS  := -std=c11 -D_GNU_SOURCE -g -O1 -Wall -Wextra -Wno-unused-parameter $(SAN) $(INCS)
LDFLAGS := $(SAN)

# Pure core (no renderer) + layout code (renderer-dependent, exercised through
# the stub) + microui (for mu_color/mu_rect/mu_vec2 used by md_render).
CORE    := $(APP)/Editor/buffer.c $(APP)/Editor/editing.c $(APP)/Editor/undo.c \
           $(APP)/Editor/navigation.c $(APP)/Editor/md_render.c \
           $(APP)/Editor/recent.c $(APP)/Editor/commands.c \
           $(APP)/ThirdParty/microui.c
TESTS   := test_main.c stub_renderer.c clock_fake.c clipboard_fake.c \
           unit_editing.c unit_undo.c unit_buffer.c \
           unit_navigation.c unit_md_render.c unit_recent.c unit_status.c \
           unit_commands.c snapshot.c
SRC     := $(CORE) $(TESTS)
HDRS    := test.h ed_fixture.h stub_renderer.h

BIN     := kern_tests

$(BIN): $(SRC) $(HDRS)
	$(CC) $(CFLAGS) -o $@ $(CORE) $(TESTS) $(LDFLAGS)

.PHONY: test clean
test: $(BIN)
	UBSAN_OPTIONS=halt_on_error=1:print_stacktrace=1 ./$(BIN)

clean:
	rm -rf $(BIN) $(BIN).dSYM
