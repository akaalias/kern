# Kern Test Suite & Refactor-Safety Net — Plan

> Companion docs: [TESTING_PROGRESS.md](./TESTING_PROGRESS.md) (live status tracker) · [REFACTORING.md](./REFACTORING.md) (refactor candidates this net unlocks)

## Context

Kern is a ~4,300-line C macOS editor (microui + SDL2 + OpenGL behind a thin Swift/AppKit shell) with **zero tests and zero CI today**. The goal is to fortify it with as much coverage as possible so we can then **aggressively refactor toward readability, minimalism, and C-idiomacy while guaranteeing feature + performance parity**.

The architecture is favorable: there is already a pure data core (`EditorState` + `buffer.c`/`editing.c`/`undo.c`) that touches no SDL/GL, and `navigation.c`/`md_render.c` depend only on the 12-function `renderer.h` interface. The one obstacle to fast feature tests is that `textview.c` (1,580 lines) keeps event dispatch and rendering inline inside `editor_main`'s `for(;;)` loop — there is no callable "handle one event / render-from-state" seam.

**Confirmed decisions:** headless-first feature tests on the real code + a thin GUI smoke layer; extract the controller seam early (after a pure-core net exists); a portable Make/CMake headless build + GitHub Actions (Linux for headless, macOS for app build + smoke); a single-header minimal test framework with ASan/UBSan/LSan + coverage.

## Strategy: a headless-first test pyramid

The insight driving everything: **"test the real app" = exercise the real production code paths, not real pixels.** A headless harness feeds synthetic input through the actual binding table / `ed_*` / wrap / markdown layout into an offscreen **capture renderer** (records draw ops + metrics instead of issuing GL). This is fast, deterministic, runs in CI/cloud, and never touches your screen. Pixel-level GUI automation is reserved for a tiny smoke layer that only checks wiring.

```
Layer 4  GUI smoke (real window, cloud macOS CI)         ~handful, wiring only
Layer 3  Performance (headless, 100MB corpora, budgets)  load/jump/search/wrap
Layer 2  Feature/UX (headless, real binding table)       keybindings, editing, wikilinks, nav, search, save/open
Layer 1  Integration (nav + md_render via stub renderer) wrapping, layout, snapshots
Layer 0  Unit (pure core: buffer/editing/undo)           + ASan/UBSan/LSan everywhere
```

## Cross-cutting recommendations (the "what else")

These are the levers that actually deliver *parity-guaranteed refactoring*, beyond the five layers:

- **Sanitizers as the backbone (memory safety, #4):** every headless test binary built with `-fsanitize=address,undefined` (+ LeakSanitizer). This is the single biggest "certainty + memory safety" lever and the precondition for fearless refactor.
- **Characterization tests first:** with zero tests, capture *current observable behavior* (even quirks) as the spec before refactoring — so refactors must preserve it. This is how we lock in parity.
- **Snapshot / golden render tests (feature parity):** the capture renderer serializes a deterministic render model (text runs + positions + style/color + rects). Snapshot representative documents *before* refactor; diff *after* → mechanical parity proof for layout/markdown.
- **Property / invariant tests:** `insert(x) then backspace == identity`; `undo` round-trips; `load(save(buf)) == buf` (idempotent serialization); every wrap row width ≤ page width; cursor always in `[0,len]`. Invariants survive refactors better than example tests.
- **Differential testing during refactor:** when rewriting a hot function (e.g. wrapping, `md_draw_text`), keep the old version behind a flag and assert `old(input) == new(input)` over a corpus. Retire once green.
- **Fuzzing (libFuzzer) on the two parsers most likely to harbor memory bugs:** `buf_load_file` (random bytes / truncation / huge lines) and the markdown scanner `md_detect_span`/`md_draw_text`. Short fuzz runs in CI catch UB/overflows no example test will.
- **Coverage measurement (llvm-cov):** track core coverage as a number; serves the "as much coverage as possible" goal and shows refactor blind spots.
- **Determinism seams:** inject a clock (autosave 3s, `status_time`, `SDL_GetTicks` at `navigation.c:252/261`) and abstract the clipboard (`SDL_SetClipboardText`/`GetClipboardText`, `textview.c:522/529`) so copy/paste and timed behavior are testable headlessly.

## Phased implementation

### Phase A — Foundation + pure-core unit tests (no production refactor)
- Add `tests/` with a standalone **Makefile** (and optional CMake) that compiles the portable core (`buffer.c`, `editing.c`, `undo.c`), `microui.c` (for `mu_*` typedefs used via `editor_types.h`), SDL *headers only* (no `libSDL2.a`), and test files into a `kern_tests` binary — all under ASan+UBSan.
- Single-header harness `tests/test.h` (custom ~80 lines or vendored utest.h).
- Unit tests: `editing.c` (all `ed_*`, incl. the new `ed_indent_line`/`ed_dedent_line`), `undo.c` (push/coalesce/group/perform round-trips), `buffer.c` (`buf_load_file`/`buf_save` via temp files, line growth, region/mark, `buf_resolve_path`, filename completion).
- Invariants + characterization tests for current behavior.
- **GitHub Actions** job (Linux) builds + runs the suite under sanitizers on every push/PR.

### Phase B — Stub renderer + layout/integration tests + snapshot infra
- `tests/stub_renderer.c` implementing `renderer.h` (deterministic `r_get_text_width = len*W`, fixed `r_get_text_height`/`r_get_size`, draw ops recorded into a capture model). No GL.
- Tests for `navigation.c` (wrap breaks, cursor visibility, word motion metrics) and `md_render.c` (list indent/marker, headings, inline spans **including multi-row** bold/italic/code/links and the new `==highlight==`).
- Snapshot harness: serialize the capture model; golden files in `tests/snapshots/`.

### Phase C — Controller seam extraction (guarded by Phase A/B nets)
- Refactor `textview.c` minimally to expose testable seams without behavior change:
  - `kern_handle_event(EditorState*, ViewState*, const SDL_Event*)` (or higher-level `kern_dispatch_key(sym,mod)` + `kern_input_text(str)`) — extract the inline dispatch from `editor_main`'s loop.
  - `kern_render_to(renderer iface, EditorState*, ViewState*)` — render-from-state, so the capture renderer can drive a full frame.
  - Inject clock + clipboard seams.
  - `editor_main` becomes a thin bootstrap: `SDL_Init` → `r_init` → `while(event) kern_handle_event(...); kern_render_to(...)`.
- Each extraction is landed against characterization snapshots proving identical output. This phase doubles as the first readability win.

### Phase D — Feature / UX headless tests
- A headless driver builds `EditorState`+`ViewState`+capture renderer, feeds key/text/mouse events, asserts on document + cursor + render model.
- **Table-driven keybinding tests** mirroring `normal_bindings[]` (`textview.c:657`) so coverage stays in sync with the table; plus prefix chords (`C-x C-s`, `C-x C-f`, `C-x b`, ESC/meta) and the new `Cmd-Shift-,/.`.
- Scenario tests: write/delete/open/save, incremental search fwd/back, wikilink follow + autocomplete + back/forward navigation, list indent/outdent, font size, region copy/cut/paste/yank.

### Phase E — Performance harness + corpora
- `tools/gen_prose.py` (referenced in `.gitignore`, currently absent) generates deterministic corpora at 100K / 1M / 100MB; big files stay gitignored, regenerated in CI.
- `tests/perf/` headless benchmarks with budgets (monotonic clock, JSON/CSV output for trend tracking): file load, jump-to-end / jump-to-top (wrap + scroll recompute), search fwd/back, full re-wrap on resize (`buf_invalidate_all_wraps`), simulated scroll. Assert against thresholds; fail on regression.
- These benchmarks then **guard the optimization of the known hotspots** (per-char `r_get_text_width` wrapping `navigation.c:46`; `buf_invalidate_all_wraps` `buffer.c:321`; full-buffer autosave `buffer.c:255` every 3s) as early refactor targets.

### Phase F — GUI smoke + CI matrix finalization
- A handful of end-to-end smoke tests through the real window (SDL synthetic events via `SDL_PushEvent`, or XCUITest) verifying Swift→`editor_main` launch, a keystroke reaching the buffer, save, and clean shutdown.
- GitHub Actions matrix: **Linux job** (headless unit/integration/feature/perf under sanitizers + coverage + short fuzz) and **macOS job** (Xcode app build + GUI smoke). For local runs without hogging the laptop: the headless suite is just a CLI binary (run in background / another shell); the GUI smoke runs in cloud macOS CI or, if wanted locally, a macOS VM (tart/UTM/Virtualization.framework).

## Key files

- New: `tests/Makefile` (+ optional `CMakeLists.txt`), `tests/test.h`, `tests/stub_renderer.c`, `tests/unit_*.c`, `tests/feature_*.c`, `tests/integration_*.c`, `tests/perf/*.c`, `tests/snapshots/`, `tools/gen_prose.py`, `.github/workflows/ci.yml`.
- Modified (Phase C, behavior-preserving): `application/Kern/Kern/Editor/textview.c` (extract `kern_handle_event` / `kern_render_to` / clock + clipboard seams), small declarations in a new `textview.h` or existing headers.
- Reused as-is (test seams already clean): `editing.h`/`editing.c`, `undo.h`/`undo.c`, `buffer.h`/`buffer.c`, `navigation.h`/`navigation.c`, `md_render.h`/`md_render.c`, `Platform/renderer.h` (the 12-fn interface the stub implements).

## Verification

- `cd tests && make && ./kern_tests` runs green locally under ASan/UBSan/LSan; coverage report generated via llvm-cov.
- Snapshot tests: regenerate goldens on a known-good build, then prove later refactors produce byte-identical render models.
- Perf: `make perf && ./kern_perf` prints per-scenario timings vs budgets on the 100MB corpus; CI fails on threshold regressions.
- The Xcode app still builds and runs unchanged after the Phase C seam extraction (manual ⌘R smoke + the GUI smoke test), confirming no behavior/perf change.
- CI: both GitHub Actions jobs (Linux headless, macOS app+smoke) pass on PRs.

## Suggested first slice (to start once approved)

Phase A end-to-end on a thin vertical: `tests/Makefile` + `tests/test.h` + `tests/unit_editing.c` (covering `ed_insert_char`, `ed_backspace`, `ed_enter`, indent/outdent) + the Linux CI job under sanitizers. This proves the headless build + sanitizer + CI loop before we scale breadth.
