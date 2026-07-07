# tests/Makefile — headless test build for Kern's portable core.
#
# Compiles only the pure EditorState code (buffer/editing/undo) plus the SDL
# *headers* (needed for typedefs via editor_types.h). No SDL runtime, no
# OpenGL, no Cocoa — so it runs anywhere, including Linux CI.
# Built under AddressSanitizer + UndefinedBehaviorSanitizer; LeakSanitizer
# activates automatically on Linux.

ROOT    := ..
APP     := $(ROOT)/application/Kern/Kern
VENDOR  := $(ROOT)/application/Kern/Vendor

# Toolchain selection.
#   Linux CI: plain `cc` + LLVM tools straight off PATH.
#   macOS <=15: Apple `cc` + xcrun-fronted LLVM tools.
#   macOS 26+: Apple clang 17's AddressSanitizer runtime DEADLOCKS at process
#     startup — dyld's shared-cache iteration now mallocs during ASan init,
#     which re-enters ASan's own malloc interceptor and spins forever on the
#     init lock (visible as `make test` hanging with zero output; `sample` shows
#     __asan::AsanInitInternal -> ... -> sched_yield). Homebrew LLVM ships a
#     fixed compiler-rt, so when it's installed we use its clang AND its
#     matching llvm-profdata/llvm-cov (profraw versions must agree). Override
#     any of this with `make CC=... LLVM_PREFIX=...`.
# `$(LLVM_PREFIX)llvm-profdata` expands to a runnable command in all three
# cases: a Homebrew bin path, `xcrun ` (note the trailing space, built via
# $(_sp) since Make strips literal trailing spaces), or empty on Linux.
_empty :=
_sp := $(_empty) $(_empty)
LLVM_PREFIX ?=
ifeq ($(shell uname),Darwin)
  BREW_LLVM := $(firstword $(wildcard /opt/homebrew/opt/llvm /usr/local/opt/llvm))
  ifneq ($(BREW_LLVM),)
    # Override only Make's built-in `cc` default (origin `default`); a real
    # `make CC=...` or an exported $CC still wins.
    ifeq ($(origin CC),default)
      CC := $(BREW_LLVM)/bin/clang
    endif
    LLVM_PREFIX := $(BREW_LLVM)/bin/
  else
    LLVM_PREFIX := xcrun$(_sp)
  endif
endif
CC      ?= cc

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
# libm (sqrt, used by textview.c's circle/rounded-rect fills) is auto-linked on
# macOS via libSystem but must be explicit on Linux — omitting it links locally
# yet fails the Linux CI. Appended to every link line below.
MATHLIB := -lm
LDFLAGS := $(SAN) $(MATHLIB)

# Pure core (no renderer) + layout code (renderer-dependent, exercised through
# the stub). Geometry types (color/rect/vec2) come from gfx.h. textview.c joins
# this list: its handlers/render compile headlessly via the stub + platform_stub.
CORE    := $(APP)/Editor/buffer.c $(APP)/Editor/editing.c $(APP)/Editor/undo.c \
           $(APP)/Editor/navigation.c $(APP)/Editor/md_render.c \
           $(APP)/Editor/pos_render.c $(APP)/Editor/style_check.c \
           $(APP)/Editor/sub_render.c $(APP)/Editor/graph.c \
           $(APP)/Editor/recent.c $(APP)/Editor/commands.c \
           $(APP)/Editor/textview.c
TESTS   := test_main.c stub_renderer.c clock_fake.c clipboard_fake.c \
           platform_stub.c pos_tagger_fake.c \
           unit_editing.c unit_undo.c unit_buffer.c \
           unit_navigation.c unit_search.c unit_md_render.c unit_recent.c \
           unit_status.c unit_commands.c unit_utf8.c unit_textview.c \
           unit_pos.c unit_style.c unit_sub.c unit_graph.c snapshot.c
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
           $(APP)/Editor/pos_render.c $(APP)/Editor/style_check.c \
           $(APP)/Editor/sub_render.c $(APP)/Editor/graph.c \
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
# Gated core line-coverage floor (percent). CI and the pre-push hook both enforce
# it via `coverage-check` so they can never drift.
COV_FLOOR := 85
.PHONY: coverage
coverage:
	$(CC) -std=c11 -D_GNU_SOURCE -DKERN_HEADLESS_TEST -O0 \
	  -fprofile-instr-generate -fcoverage-mapping $(INCS) -o $(COV_BIN) $(SRC) $(MATHLIB)
	LLVM_PROFILE_FILE=$(COV_BIN).profraw ./$(COV_BIN) >/dev/null
	$(LLVM_PREFIX)llvm-profdata merge -sparse $(COV_BIN).profraw -o $(COV_BIN).profdata
	$(LLVM_PREFIX)llvm-cov report ./$(COV_BIN) -instr-profile=$(COV_BIN).profdata $(COVCORE)
	@rm -f $(COV_BIN) $(COV_BIN).profraw $(COV_BIN).profdata

# Gated coverage: print the report table AND enforce the line floor from the
# llvm-cov *export* JSON (data[0].totals.lines.percent), which is stable across
# platforms/llvm versions — unlike the report table's columns. THE single source
# of truth for the floor: ci.yml and .githooks/pre-push both just run this.
.PHONY: coverage-check
coverage-check:
	$(CC) -std=c11 -D_GNU_SOURCE -DKERN_HEADLESS_TEST -O0 \
	  -fprofile-instr-generate -fcoverage-mapping $(INCS) -o $(COV_BIN) $(SRC) $(MATHLIB)
	LLVM_PROFILE_FILE=$(COV_BIN).profraw ./$(COV_BIN) >/dev/null
	$(LLVM_PREFIX)llvm-profdata merge -sparse $(COV_BIN).profraw -o $(COV_BIN).profdata
	$(LLVM_PREFIX)llvm-cov report ./$(COV_BIN) -instr-profile=$(COV_BIN).profdata $(COVCORE)
	$(LLVM_PREFIX)llvm-cov export -summary-only ./$(COV_BIN) -instr-profile=$(COV_BIN).profdata $(COVCORE) > $(COV_BIN).json
	@python3 cov_floor.py $(COV_BIN).json $(COV_FLOOR); rc=$$?; \
	  rm -f $(COV_BIN) $(COV_BIN).profraw $(COV_BIN).profdata $(COV_BIN).json; \
	  exit $$rc

# Non-gated: coverage of textview.c's event/dispatch layer (unit_textview.c).
# The uncovered remainder is the do_render/process_frame GL chrome + the
# #ifdef'd-out editor_main, neither reachable from the headless harness.
.PHONY: coverage-textview
coverage-textview:
	$(CC) -std=c11 -D_GNU_SOURCE -DKERN_HEADLESS_TEST -O0 \
	  -fprofile-instr-generate -fcoverage-mapping $(INCS) -o $(COV_BIN) $(SRC)
	LLVM_PROFILE_FILE=$(COV_BIN).profraw ./$(COV_BIN) >/dev/null
	$(LLVM_PREFIX)llvm-profdata merge -sparse $(COV_BIN).profraw -o $(COV_BIN).profdata
	$(LLVM_PREFIX)llvm-cov report ./$(COV_BIN) -instr-profile=$(COV_BIN).profdata $(APP)/Editor/textview.c
	@rm -f $(COV_BIN) $(COV_BIN).profraw $(COV_BIN).profdata

# ---- performance harness ----
# Built WITHOUT sanitizers at -O2 for realistic timing. Measures the algorithm
# (wrap/search/load) via the deterministic stub renderer, not GL.
PERF_BIN := kern_perf
# navigation.c now mirrors the render geometry (hanging indent + per-span
# widths), so it links against md_render.c and its span providers (pos/style +
# the fake tagger) — the same minimal graph the test binary uses.
PERF_SRC := perf/perf_main.c stub_renderer.c clock_fake.c pos_tagger_fake.c \
            $(APP)/Editor/buffer.c $(APP)/Editor/undo.c $(APP)/Editor/recent.c $(APP)/Editor/navigation.c \
            $(APP)/Editor/md_render.c $(APP)/Editor/pos_render.c \
            $(APP)/Editor/style_check.c $(APP)/Editor/sub_render.c

$(PERF_BIN): $(PERF_SRC) stub_renderer.h clock_fake.h
	$(CC) -std=c11 -D_GNU_SOURCE -O2 $(INCS) -o $@ $(PERF_SRC) $(MATHLIB)

# Usage: make perf CORPUS=../test_100mb.txt        (add PERF_ARGS=--check for CI)
# Generate a corpus first: python3 ../tools/gen_prose.py 100MB ../test_100mb.txt
.PHONY: perf
perf: $(PERF_BIN)
	@test -n "$(CORPUS)" || { echo "set CORPUS=<file>  (e.g. python3 ../tools/gen_prose.py 100MB ../test_100mb.txt)"; exit 1; }
	./$(PERF_BIN) "$(CORPUS)" $(PERF_ARGS)

clean:
	rm -rf $(BIN) $(BIN).dSYM $(PERF_BIN) $(PERF_BIN).dSYM \
	       $(COV_BIN) $(COV_BIN).dSYM *.profraw *.profdata $(COV_BIN).json
