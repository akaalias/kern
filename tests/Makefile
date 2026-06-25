# tests/Makefile — headless test build for Kern's portable core.
#
# Compiles only the pure EditorState code (buffer/editing/undo) plus the SDL
# *headers* (needed for typedefs via editor_types.h). No SDL runtime, no
# OpenGL, no Cocoa — so it runs anywhere, including Linux CI.
# Built under AddressSanitizer + UndefinedBehaviorSanitizer; LeakSanitizer
# activates automatically on Linux.

CC      ?= cc
ROOT    := ..
APP     := $(ROOT)/application/Kern/Kern
VENDOR  := $(ROOT)/application/Kern/Vendor

# On macOS the LLVM coverage tools live behind xcrun; on Linux they're direct.
ifeq ($(shell uname),Darwin)
  XCRUN := xcrun
endif

# SDL is needed for headers only (typedefs via editor_types.h), never linked.
# Defaults to the macOS-configured vendored headers; CI overrides this with
# `SDL_CFLAGS=` to use a Linux libsdl2-dev install on the default include path.
SDL_CFLAGS ?= -I$(VENDOR)/SDL2/include

SAN     := -fsanitize=address,undefined -fno-omit-frame-pointer
INCS    := -I$(APP)/Editor -I$(APP)/Platform -I$(APP)/ThirdParty $(SDL_CFLAGS)
# _GNU_SOURCE exposes POSIX helpers (mkstemp/unlink/mkdir) that glibc hides
# under -std=c11; harmless on macOS where they're visible by default.
# KERN_HEADLESS_TEST compiles textview.c's event/dispatch + modal handlers into
# the test binary (editor_main + the real-SDL render loop stay #ifdef'd out) and
# enables the tv_test_* seam.
CFLAGS  := -std=c11 -D_GNU_SOURCE -g -O1 -Wall -Wextra -Wno-unused-parameter \
           -DKERN_HEADLESS_TEST $(SAN) $(INCS)
LDFLAGS := $(SAN)

# Pure core (no renderer) + layout code (renderer-dependent, exercised through
# the stub). Geometry types (color/rect/vec2) come from gfx.h. textview.c joins
# this list: its handlers/render compile headlessly via the stub + platform_stub.
CORE    := $(APP)/Editor/buffer.c $(APP)/Editor/editing.c $(APP)/Editor/undo.c \
           $(APP)/Editor/navigation.c $(APP)/Editor/md_render.c \
           $(APP)/Editor/recent.c $(APP)/Editor/commands.c \
           $(APP)/Editor/textview.c
TESTS   := test_main.c stub_renderer.c clock_fake.c clipboard_fake.c \
           platform_stub.c \
           unit_editing.c unit_undo.c unit_buffer.c \
           unit_navigation.c unit_search.c unit_md_render.c unit_recent.c \
           unit_status.c unit_commands.c unit_utf8.c unit_textview.c snapshot.c
SRC     := $(CORE) $(TESTS)
HDRS    := test.h ed_fixture.h stub_renderer.h platform_stub.h \
           $(APP)/Editor/utf8.h $(APP)/Editor/editor_loop.h

# Production files the gated coverage floor (>=85% lines) measures: the testable
# Editor core, which is ~99% saturated. textview.c is intentionally NOT here — it
# is a partly-tested shell (its event/dispatch layer is covered by unit_textview.c,
# but the do_render/process_frame GL chrome is not), so folding it into the gated
# number would mask the core's saturation. Use `make coverage-textview` to see it.
COVCORE := $(APP)/Editor/buffer.c $(APP)/Editor/editing.c $(APP)/Editor/undo.c \
           $(APP)/Editor/navigation.c $(APP)/Editor/md_render.c \
           $(APP)/Editor/recent.c $(APP)/Editor/commands.c

BIN     := kern_tests

$(BIN): $(SRC) $(HDRS)
	$(CC) $(CFLAGS) -o $@ $(CORE) $(TESTS) $(LDFLAGS)

.PHONY: test clean
test: $(BIN)
	UBSAN_OPTIONS=halt_on_error=1:print_stacktrace=1 ./$(BIN)

# ---- coverage ----
# clang source-based coverage of the testable core (requires clang + llvm-cov).
# Reports line/function/region/branch coverage for COVCORE only.
COV_BIN := kern_cov
.PHONY: coverage
coverage:
	$(CC) -std=c11 -D_GNU_SOURCE -DKERN_HEADLESS_TEST -O0 \
	  -fprofile-instr-generate -fcoverage-mapping $(INCS) -o $(COV_BIN) $(SRC)
	LLVM_PROFILE_FILE=$(COV_BIN).profraw ./$(COV_BIN) >/dev/null
	$(XCRUN) llvm-profdata merge -sparse $(COV_BIN).profraw -o $(COV_BIN).profdata
	$(XCRUN) llvm-cov report ./$(COV_BIN) -instr-profile=$(COV_BIN).profdata $(COVCORE)
	@rm -f $(COV_BIN) $(COV_BIN).profraw $(COV_BIN).profdata

# Non-gated: coverage of textview.c's event/dispatch layer (unit_textview.c).
# The uncovered remainder is the do_render/process_frame GL chrome + the
# #ifdef'd-out editor_main, neither reachable from the headless harness.
.PHONY: coverage-textview
coverage-textview:
	$(CC) -std=c11 -D_GNU_SOURCE -DKERN_HEADLESS_TEST -O0 \
	  -fprofile-instr-generate -fcoverage-mapping $(INCS) -o $(COV_BIN) $(SRC)
	LLVM_PROFILE_FILE=$(COV_BIN).profraw ./$(COV_BIN) >/dev/null
	$(XCRUN) llvm-profdata merge -sparse $(COV_BIN).profraw -o $(COV_BIN).profdata
	$(XCRUN) llvm-cov report ./$(COV_BIN) -instr-profile=$(COV_BIN).profdata $(APP)/Editor/textview.c
	@rm -f $(COV_BIN) $(COV_BIN).profraw $(COV_BIN).profdata

# ---- performance harness ----
# Built WITHOUT sanitizers at -O2 for realistic timing. Measures the algorithm
# (wrap/search/load) via the deterministic stub renderer, not GL.
PERF_BIN := kern_perf
PERF_SRC := perf/perf_main.c stub_renderer.c clock_fake.c \
            $(APP)/Editor/buffer.c $(APP)/Editor/navigation.c

$(PERF_BIN): $(PERF_SRC) stub_renderer.h clock_fake.h
	$(CC) -std=c11 -D_GNU_SOURCE -O2 $(INCS) -o $@ $(PERF_SRC)

# Usage: make perf CORPUS=../test_100mb.txt        (add PERF_ARGS=--check for CI)
# Generate a corpus first: python3 ../tools/gen_prose.py 100MB ../test_100mb.txt
.PHONY: perf
perf: $(PERF_BIN)
	@test -n "$(CORPUS)" || { echo "set CORPUS=<file>  (e.g. python3 ../tools/gen_prose.py 100MB ../test_100mb.txt)"; exit 1; }
	./$(PERF_BIN) "$(CORPUS)" $(PERF_ARGS)

clean:
	rm -rf $(BIN) $(BIN).dSYM $(PERF_BIN) $(PERF_BIN).dSYM \
	       $(COV_BIN) $(COV_BIN).dSYM *.profraw *.profdata
