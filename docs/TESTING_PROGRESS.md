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
- [ ] `tests/stub_renderer.c` (deterministic metrics, capture model, no GL)
- [ ] `tests/integration_navigation.c` (wrap breaks, cursor visibility, word motion)
- [ ] `tests/integration_md_render.c` (list/heading/inline incl. multi-row spans + `==highlight==`)
- [ ] Snapshot harness + `tests/snapshots/` goldens

**Phase C — Controller seam extraction (behavior-preserving)**
- [ ] Extract `kern_handle_event` / `kern_dispatch_key` / `kern_input_text` from `editor_main`
- [ ] Extract `kern_render_to(renderer, EditorState*, ViewState*)`
- [ ] Inject clock seam (autosave/status/SDL_GetTicks) + clipboard seam
- [ ] `editor_main` reduced to thin bootstrap; characterization snapshots identical

**Phase D — Feature / UX headless tests**
- [ ] Table-driven keybinding tests mirroring `normal_bindings[]` + prefix chords + Cmd-Shift-,/.
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

## Next action on resume
Finish `unit_buffer.c`: `buf_resolve_path` + filename completion (point `buf_set_documents_dir` at a `mkdtemp` dir, drop a few files, assert). Add a couple of invariant tests (`load(save(buf)) == buf` over generated content). Then **Phase B**: `tests/stub_renderer.c` implementing `renderer.h` (deterministic metrics) to unit-test `navigation.c` wrapping and `md_render.c` layout — including the multi-row inline spans and `==highlight==`. Confirm the GitHub Actions runs are green.
