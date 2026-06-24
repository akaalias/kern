# Testing Initiative — Progress Log

Living status tracker so the initiative can be stopped and resumed at any point. **Update this file as work lands** (and append a dated session-log entry each time).

> Companion docs: [TESTING_PLAN.md](./TESTING_PLAN.md) (the strategy) · [REFACTORING.md](./REFACTORING.md) (candidates the net unlocks)

**Legend:** `[ ]` pending · `[~]` in progress · `[x]` done · `[-]` dropped/deferred

## Decisions locked (2026-06-24)
- Feature tests: **headless-first + thin GUI smoke** (real code, offscreen capture renderer; no pixel automation for the bulk).
- Controller seam: **extract early, after the pure-core net exists**, guarded by characterization snapshots.
- Build/CI: **portable Make/CMake headless build + GitHub Actions** (Linux for headless, macOS for app build + smoke).
- Framework: **single-header minimal harness** + ASan/UBSan/LSan + coverage.

## Phase checklist

**Phase A — Foundation + pure-core unit tests**
- [x] `tests/Makefile`: headless build of buffer/editing/undo + SDL/microui headers only, under ASan+UBSan (`SDL_CFLAGS` overridable for Linux libsdl2-dev)
- [x] `tests/test.h` single-header harness + `tests/test_main.c` runner
- [x] `tests/unit_editing.c` (ed_insert/backspace/enter incl. list continuation + indent/outdent) — 12 tests green, 0 leaks
- [x] `tests/unit_undo.c` (insert/backspace/enter/list-group/indent undo, coalesced-run, empty-stack no-op) — 7 tests; fixtures factored into `tests/ed_fixture.h`
- [~] `tests/unit_buffer.c` — done: load, CRLF strip, missing-file, overlong-line split, save/load byte round-trip, line-array growth past cap, region ordering (7 tests). Still pending: `buf_resolve_path` + filename completion (need a temp documents dir)
- [ ] Invariant + characterization tests
- [x] GitHub Actions Linux job (`.github/workflows/ci.yml`, clang + ASan/UBSan/LSan) — pending first remote run

**Phase B — Stub renderer + layout/integration + snapshots**
- [x] `tests/stub_renderer.{c,h}` — capture impl of `renderer.h` (deterministic 10×20 glyphs, 800×600 window → 700px page; records draw ops; stubs `SDL_GetTicks`). Build now links `navigation.c` + `md_render.c` + `microui.c`.
- [x] `tests/unit_navigation.c` (6 tests) — wrap breaks: empty, short, exact-page-width, mid-word break, last-space break, `nav_count_wraps` caching
- [x] `tests/unit_md_render.c` (6 tests) — **multi-row bold carry-over** (guards the wrapped-span fix), markers use base style, full bold line, `==highlight==` bg-per-glyph, wikilink bg, linear `md_col_x`
- [x] Snapshot harness (`tests/snapshot.c`) + `tests/snapshots/` goldens — renders representative docs (plain/inline/inline-wrap/lists/headings) through the real wrap + md_draw_text path into the stub's ordered op stream, serializes a deterministic model, and diffs vs committed goldens. Regenerate with `KERN_UPDATE_SNAPSHOTS=1 make test`. **This is the parity proof for future refactors.**
- [ ] More integration breadth: cursor visibility, word motion, headings/list-indent layout

**Phase C — Controller seam extraction (behavior-preserving)**
Approach: extract-then-test in small slices — pull self-contained pieces out of `textview.c` into modules compiled into BOTH the app and the headless test binary, growing automated coverage over `textview.c` before the riskier dispatch/de-globalization surgery. (The snapshot/unit nets cover the render+core path, NOT `textview.c`, so each slice is verified by app-build + run + new unit tests.)
- [x] Decompose slice 1: recent-files MRU → `Editor/recent.{c,h}` + `tests/unit_recent.c` (5 tests). App builds + runs; render snapshots unchanged.
- [x] Decompose slice 2: clipboard seam → `Editor/clipboard.h` + `Editor/clipboard_sdl.c` (app); the copy/yank commands now call `kern_clipboard_*` instead of SDL directly. Added 6 kill-ring unit tests (`ed_emacs_kill_line`/`copy_region`/`yank`, incl. multi-line + copy→yank round-trip). A test-side fake clipboard will be added when the dispatch is extracted.
- [x] Decompose slice 3: clock seam → `Editor/clock.h` + `Editor/clock_sdl.c` (app); `navigation.c` status timing + `textview.c` autosave now call `kern_now_ms()`. Test build links `tests/clock_fake.c` (settable) and the `SDL_GetTicks` stub is retired. Added 2 `nav_status` tests (message expiry + mode-indicator priority) via the fake clock.
- [~] Extract `kern_dispatch_key` + de-globalize the command table, in batches → `Editor/commands.{c,h}` (explicit `EditorState*`/`ViewState*`); `editor_main` calls `kern_dispatch_key` before the shrinking legacy table; each batch feature-tested in `tests/unit_commands.c`.
  - [x] Batch 1: cursor movement (C-a/C-e/C-f/C-b/C-n/C-p, M-f/M-b) — 7 tests.
  - [x] Batch 2a: table-only editing (C-d, Backspace, Return, C-t, C-o, C-/, C-Space, C-g) — 7 tests.
  - [x] Batch 2b: kill/yank/copy/case (C-k, C-w, C-y, M-w, M-d, M-DEL, M-u/M-l/M-c) — added `tests/clipboard_fake.c`; exposed the 5 ESC/meta-shared commands in `commands.h` and updated those call sites. 7 tests (incl. copy→yank via clipboard). The `clipboard_set_from_kill` helper moved into `commands.c`.
  - [ ] Batch 3: page/recenter/font (C-v, M-v, C-l, Cmd-=/Cmd--); buffer-ends + mark (C-x h, C-x C-x).
  - [ ] `cmd_goto_line` — after minibuffer state moves into ViewState.
- [ ] Extract `kern_input_text` (text insertion) + `kern_render_to(...)`; `editor_main` reduced to a thin bootstrap

**Phase D — Feature / UX headless tests** (unblocked once commands are de-globalized; grows per Phase C batch)
- [~] Keybinding feature tests via `kern_dispatch_key` — started with the 8 movement commands (`unit_commands.c`). Extend as each command batch migrates.
- [ ] Prefix chords (`C-x …`, ESC/meta), `Cmd-Shift-,/.`
- [ ] Scenarios: write/delete/open/save, search fwd/back, wikilink follow+autocomplete+nav, list indent, copy/cut/paste/yank, font size

**Phase E — Performance harness + corpora**
- [ ] `tools/gen_prose.py` (100K / 1M / 100MB, deterministic)
- [ ] `tests/perf/` benchmarks + budgets (load, jump end/top, search, re-wrap, scroll)
- [ ] Guarded optimization of hotspots (per-char wrap metrics, invalidate_all_wraps, autosave)

**Phase F — GUI smoke + CI matrix**
- [ ] Handful of end-to-end smoke tests (SDL_PushEvent / XCUITest)
- [ ] CI matrix: Linux headless (+coverage +short fuzz) and macOS (Xcode build + smoke)

## Cross-cutting (land alongside the phases)
- [x] Sanitizers wired into every headless build (A) — ASan+UBSan in `tests/Makefile`; LSan auto on Linux CI
- [ ] Coverage (llvm-cov) reporting (A/F)
- [ ] libFuzzer targets: `buf_load_file`, `md_detect_span` (E/F)
- [ ] Differential-test harness for hot-function rewrites (C/E)

## Session log
- **2026-06-24** — Architecture mapped; strategy decided (4 choices above); plan + this tracker written and promoted from `~/.claude/plans/` into `docs/`. Refactoring-candidates doc created (`REFACTORING.md`).
- **2026-06-24** — **Phase A first slice landed.** `tests/` headless build (Makefile + test.h + test_main.c) compiling buffer/editing/undo under ASan+UBSan; `unit_editing.c` with 12 tests (insert/backspace/enter/list-continuation/indent/outdent) — all green, 0 leaks (verified via macOS `leaks`). Linux CI workflow added. **No app code changed.** App source untouched; only new `tests/`, `.github/`, `.gitignore` guard.
- **2026-06-24** — Added `tests/unit_undo.c` (7 tests) and factored shared fixtures into `tests/ed_fixture.h`. Suite now **19 tests / 47 checks**, all green, 0 leaks. App code still untouched.
- **2026-06-24** — Added `tests/unit_buffer.c` (7 tests: load, CRLF strip, missing-file, overlong split, save/load byte round-trip, line-array growth, region ordering). **Pure-core unit layer (buffer/editing/undo) now covered: 26 tests / 81 checks, green, 0 leaks.** App code still untouched.
- **2026-06-24** — **Phase B started.** Added `tests/stub_renderer.{c,h}` (capture renderer, no GL) and wired `navigation.c` + `md_render.c` + `microui.c` into the headless build. New `unit_navigation.c` (6) + `unit_md_render.c` (6). Suite **38 tests / 109 checks, green, 0 leaks.** Multi-row bold test guards the earlier wrapped-span fix.
- **2026-06-24** — Fixed Linux CI build (`-D_GNU_SOURCE` exposes POSIX `mkstemp` that glibc hides under `-std=c11`; macOS built fine without it).
- **2026-06-24** — **Snapshot harness landed** (`tests/snapshot.c` + goldens in `tests/snapshots/`): rendered through the real wrap+md_draw_text path → deterministic serialized model diffed against committed goldens. Broadened to 7 scenarios covering every inline feature — plain, inline (bold/italic/code/wikilink/highlight), inline-wrap, lists, headings (#/##/###), **links (`[text](url)`)**, and **edges** (unterminated/empty/non-nesting delimiters). Suite now **45 tests / 116 checks, green, 0 leaks.** App code still untouched.

- **2026-06-24** — **Phase C started (first app-code change).** Extracted the recent-files MRU out of `textview.c` into `Editor/recent.{c,h}` (with `recent_count()`/`recent_get()`/`recent_reset()` accessors), wired into both the app (auto via folder-sync) and the headless build. `tests/unit_recent.c` (5 tests). App builds + launches; render snapshots unchanged. (`C-x b` confirmed working by user.) Suite **50 tests / 132 checks.**
- **2026-06-24** — **Phase C slice 2: clipboard seam.** `Editor/clipboard.h` + `Editor/clipboard_sdl.c`; copy/yank now go through `kern_clipboard_*` instead of touching SDL. Added 6 kill-ring unit tests. (Copy/yank confirmed working by user.) Suite **56 tests / 147 checks.**
- **2026-06-24** — **Phase C slice 3: clock seam.** `Editor/clock.h` + `Editor/clock_sdl.c`; `navigation.c`/`textview.c` time now via `kern_now_ms()`. Test build uses a settable `clock_fake.c`; retired the `SDL_GetTicks` stub. Added 2 `nav_status` tests (expiry + priority). App builds + launches. Suite **58 tests / 155 checks, green, 0 leaks.**

## Next action on resume
Continue migrating command batches into `commands.c` (each: de-globalize → remove from `textview.c`'s table/defs → add to `g_commands` → feature tests in `unit_commands.c` → app build + run + snapshots + leaks). Next batch (2b): kill/yank/copy/case (C-k, C-w, C-y, M-w, M-d, M-DEL, M-u/M-l/M-c) — add a test `clipboard_fake.c` so yank/copy assertions work, and expose the modal-handler-shared commands in `commands.h` (handle_esc_prefix calls several). Then batch 3 (page/recenter/font, buffer-ends/mark). Leave `cmd_goto_line` until minibuffer state moves into ViewState.
