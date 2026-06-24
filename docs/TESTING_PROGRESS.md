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
- [ ] `tests/unit_undo.c` (push/coalesce/group/perform round-trips)
- [ ] `tests/unit_buffer.c` (load/save temp files, growth, region/mark, path resolve, completion)
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

## Next action on resume
Broaden Phase A: `tests/unit_undo.c` (push/coalesce/group/perform round-trips) and `tests/unit_buffer.c` (load/save via temp files, line-array growth, region/mark, `buf_resolve_path`, filename completion), wiring each new `suite_*` into `test_main.c`. Then add invariant/characterization tests (e.g. `load(save(buf)) == buf`, undo round-trips). Confirm the first GitHub Actions run is green.
