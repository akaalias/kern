# Kern — Refactoring Opportunities

A living catalogue of candidate refactors toward the project's north star: **readability, minimalism, and C-idiomacy** — without sacrificing feature or performance parity.

> Companion docs: [TESTING_PLAN.md](./TESTING_PLAN.md) · [TESTING_PROGRESS.md](./TESTING_PROGRESS.md)

## How to use this document

- Every item here is a **candidate**, not committed work. Add, split, re-prioritise, or drop freely.
- **Rules of engagement** (so we keep parity):
  1. Don't start a refactor until the relevant test layer (see [TESTING_PLAN.md](./TESTING_PLAN.md)) is green over the affected code.
  2. One *behavior-preserving* change per PR; characterization snapshots must be byte-identical before/after.
  3. For hot-path rewrites, keep the old implementation behind a flag and **differential-test** old vs new on a corpus until green, then delete the old.
- Each entry records **what / why / where / parity-risk / guarded-by / status**.
- **Status:** `Proposed` · `Approved` · `In progress` · `Partial` · `Done` · `Dropped`.

## Priority snapshot

| # | Candidate | Payoff | Effort | Risk | Guarded by | Status |
|---|-----------|--------|--------|------|------------|--------|
| 1 | Remove/shrink vestigial microui usage | High (−~3k LOC, big clarity) | Med–High | Med | Snapshot + GUI smoke | **Done** |
| 2 | Replace `#define` global aliases with explicit `EditorState*`/`ViewState*` | High (testability, readability) | Med | Med | Unit + characterization | **Done** |
| 3 | Split `textview.c` (~1,330 LOC) into modules | High (readability) | Med | Low–Med | Feature + snapshot | Proposed |
| 4 | Cache per-line markdown span map (kill O(n²) re-parse) | High (perf + clarity) | Med | Med | Snapshot + perf | **Done** |
| 5 | Factor one `md_scan_list_marker` helper | Med (DRY) | Low | Low | Integration | Proposed |
| 6 | Incremental wrap invalidation + run-based metrics | High (perf at scale) | Med–High | Med | Perf + snapshot | **Partial** (resize-skip done; per-word deferred) |
| 7 | Smarter autosave for huge files | Med (perf/UX) | Low–Med | Low | Perf + unit | Proposed |
| 8 | Extract theme/palette + layout-metrics | Med (clarity) | Low | Low | Snapshot | Proposed |
| 9 | Unify prefix chords into the binding table | Med (minimalism) | Med | Low–Med | Feature | **Partial** (dup table removed; prefix handlers remain) |
| 10 | Determinism seams (clock, clipboard) | Med (decoupling + testability) | Low | Low | Unit + feature | **Done** |
| 11 | Decompose `editor_main` lifecycle | Med (readability) | Low–Med | Low | GUI smoke | Proposed |
| 12 | Fixed-buffer / truncation safety audit | Med (memory safety) | Low | Low | ASan + fuzz | Proposed |

---

## Structural

### 1. microui is vestigial — remove or shrink it
**What:** Drop the microui dependency (1,208 vendored lines in `ThirdParty/microui.c`) or reduce it to a tiny in-tree helper.
**Why:** microui is used as almost none of what it is — there are **no widgets** (no buttons/sliders/textboxes/panels/layout rows). It serves only as: (a) geometry/color types `mu_Color`/`mu_Rect`/`mu_Vec2`, (b) a single full-window borderless container used only to fetch a content rect, (c) a retained draw-command queue replayed in `do_render`, (d) a clip-rect stack, (e) input-state forwarding that no widget consumes. The editor already draws all text **directly** via `draw_md_text` → `r_draw_text`, bypassing microui. Replacing the command-queue + clip indirection with direct `r_draw_rect`/clip calls and a ~30-line geometry/color header would remove a large dependency and a layer of indirection.
**Where:** `Editor/textview.c:303-475` (`process_frame`), `:1061-1073` (`do_render` command replay), `:1318-1321` (`mu_init` + metric callbacks); `ThirdParty/microui.c` (whole file).
**Parity-risk:** Medium — touches the render path and clip handling; selection/search highlight rects (`mu_draw_rect`) and the content clip must reproduce exactly.
**Guarded by:** render snapshots (Phase B) + GUI smoke (Phase F).
**Status:** **Done.** Render + input converted to immediate mode, then `microui.c`/`microui.h`/`atlas.inl` (~3,000 lines) deleted and replaced by a ~30-line `Editor/gfx.h`. `r_draw_rect` samples a 1×1 white texel (provably equivalent to the atlas white pixel). Snapshots byte-identical; 0 leaks; the headless suite no longer links microui. `gfx.h` types were later renamed off the `mu_` prefix.

### 2. Replace `#define` global aliases with explicit state pointers
**What:** Remove the ~60 `#define cursor_line g_ed.cursor_line` style aliases; pass `EditorState*` / `ViewState*` explicitly.
**Why:** The aliases hide all data flow, make `textview.c` non-reentrant, and are the main reason the controller can't be unit-tested. Threading the structs is the enabling move for headless feature tests (Phase C) and reads far more honestly as C.
**Where:** `Editor/textview.c:20-21` (`g_ed`/`g_vs` statics) and `:24-73` (the alias block); ripples through the file.
**Parity-risk:** Medium — mechanical but broad; do it in slices guarded by characterization tests.
**Guarded by:** unit + characterization (Phase A/C).
**Status:** **Done.** The ~95-line `#define` alias block was deleted via a tokenizer-based transform (skips strings/comments, respects word boundaries); call sites now use `g_ed.`/`g_vs.` and the real `buf_*`/`nav_*`/`ed_*`/`md_*` APIs directly. App builds; suite green. The `g_ed`/`g_vs` statics stay (single-instance app state).

### 3. Split `textview.c` into focused modules
**What:** Break the file (~1,330 LOC) along its existing seams: app bootstrap/loop, input/controller, render, minibuffer, wikilink, recent-files/buffer-switch, search.
**Why:** Several distinct concerns share one file and one pile of statics. Splitting improves readability and makes ownership obvious. Naturally follows #2 (now done) and the Phase C seam extraction.
**Where:** `Editor/textview.c` (e.g. `editor_main`, `process_frame`, `do_render`, `handle_*_key`, `minibuf_*`, `wl_*`).
**Parity-risk:** Low–Medium now that #2 is done (moving functions, not changing them).
**Guarded by:** feature + snapshot (Phase B/D). **Best done after the keydown test seam exists** so the moved input logic is covered.

### 9. Unify prefix chords into the binding table
**What:** Extend `normal_bindings[]` to express prefixed chords (`C-x …`, ESC/meta …) so dispatch is one declarative table instead of `normal_bindings[]` + `handle_cx_prefix_key` + `handle_esc_prefix_key` + inline `section 5` special-cases.
**Why:** Today the same concept (a key → a command) is implemented three different ways. A single prefix-aware table is smaller, more uniform, and trivially testable/auditable; the keybinding help panel could even be generated from it.
**Where:** `Editor/textview.c` `handle_cx_prefix_key`, `handle_esc_prefix_key`, and the inline `Cmd-Shift-,/.` special-cases in the keydown handler.
**Parity-risk:** Low–Medium — must preserve every existing chord; table-driven feature tests cover this directly.
**Guarded by:** feature tests (Phase D).
**Status:** **Partial.** The duplicate `normal_bindings[]`/`check_binding` table was deleted (it cloned `Command`/`cmd_matches` for a single entry), so `kern_dispatch_key` is now the only binding table. The prefix handlers (`handle_cx_prefix_key`, `handle_esc_prefix_key`) and the `Cmd-Shift-,/.` special-cases are **not yet** folded in — that needs prefix support in the table plus the keydown test seam to guard it.

### 11. Decompose `editor_main` lifecycle
**What:** Split the all-in-one entry (SDL init, GL init, AppKit chrome, daily-note load, event loop, autosave, render) into named lifecycle steps.
**Why:** Readability; also clarifies the bootstrap vs per-frame boundary that Phase C formalises.
**Where:** `Editor/textview.c:1292-1577` (`editor_main`).
**Parity-risk:** Low.
**Guarded by:** GUI smoke (Phase F).

## Performance

### 4. Cache a per-line markdown span map
**What:** Parse each logical line's inline spans **once** into a small cached map (invalidated on edit), instead of re-parsing from the line start for every visual row.
**Why:** `md_draw_text` now parses from column 0 on each wrapped row to carry style across rows — correct, but O(rows × line_len), i.e. O(n²) for a long wrapped paragraph. A cached span map makes rendering O(n) and simplifies the draw loop.
**Where:** `Editor/md_render.c` (`md_draw_text`, `md_detect_span`); cache could hang off `Line` (`editor_types.h:20-25`, alongside `wrap_count`).
**Parity-risk:** Medium — must produce identical styling; differential-test old vs new.
**Guarded by:** render snapshots + perf (Phase B/E).
**Status:** **Done.** Inline spans are computed once per line and cached on `Line.md_spans` (lazy, invalidated by `line_dirty`, freed in the line lifecycle); `md_draw_text`/`md_col_x` take a `Line *` and consume the cache. Snapshots byte-identical; 0 leaks.

### 6. Incremental wrap invalidation + run-based text metrics
**What:** (a) Invalidate only the edited line's wrap cache instead of all lines; (b) measure text width per run rather than per character where possible.
**Why:** `buf_invalidate_all_wraps` walks every line on each window resize, and wrapping calls `r_get_text_width` per character — both scale poorly toward the 100 MB target. These are the headline perf hotspots.
**Where:** `Editor/buffer.c:321-323` (`buf_invalidate_all_wraps`), `Editor/textview.c` (resize handler), `Editor/navigation.c:46` (per-char metric in wrap).
**Parity-risk:** Medium — wrap points must match exactly; perf budgets + snapshots guard it.
**Guarded by:** perf + snapshots (Phase E/B).
**Status:** **Partial.** The parity-safe half shipped: `nav_maybe_reflow` skips the full re-wrap when a resize doesn't change the page width (resize same-width **377ms → 0.8ms**, ~480×), guarded by a unit test + perf scenario + snapshots. The per-word-metrics half is **deferred** — the renderer rounds width per character, so per-word measurement would shift wrap points in the real app, and the rounding-free stub can't catch that, so the net can't currently guard it.

### 7. Smarter autosave for huge files
**What:** Avoid rewriting the entire file every 3 s while dirty; e.g. throttle, skip when unchanged since last save, or atomic temp-write + rename, and back off for very large buffers.
**Why:** `buf_save` rewrites the whole buffer on a 3 s timer (`textview.c` autosave). At 100 MB this is a periodic multi-hundred-MB write → real lag.
**Where:** `Editor/buffer.c:255-268` (`buf_save`), `Editor/textview.c:1564-1573` (autosave timer).
**Parity-risk:** Low — pure I/O behavior; verify save correctness with unit tests.
**Guarded by:** perf + unit (Phase E/A).

## Clarity / minimalism

### 5. One `md_scan_list_marker` helper
**What:** Replace the three functions that each independently scan leading whitespace + a `- `/`N. ` marker with a single helper returning `(ws_len, marker_len, kind)`.
**Why:** `md_list_indent`, `md_list_marker_width`, and `md_is_list_item` duplicate the same scan (recently touched in the Tab-indent work). One helper is DRY and less error-prone.
**Where:** `Editor/md_render.c:9-48`.
**Parity-risk:** Low.
**Guarded by:** integration tests (Phase B).

### 8. Extract a theme/palette and layout-metrics
**What:** Centralise the inline color literals (`color(80,80,80,…)`, link/code/highlight colors) and layout constants (`TOP_PADDING 82`, `status_bar_h = text_height + 16`, etc.) into a named palette + metrics struct.
**Why:** Colors and spacing are magic numbers scattered across `md_render.c` and `textview.c`; centralising aids both readability and future theming.
**Where:** `Editor/md_render.c` (color literals in `md_draw_text`), `Editor/textview.c` (layout constants in `process_frame`/`do_render`).
**Parity-risk:** Low (values unchanged); snapshots confirm.
**Guarded by:** render snapshots (Phase B).

### 10. Determinism seams (clock + clipboard)
**What:** Inject a clock behind a tiny interface (for autosave timing, `status_time`, `SDL_GetTicks`) and abstract the clipboard.
**Why:** Decouples core logic from SDL/global time, which is both a readability/idiom win and the prerequisite for deterministic headless tests of timed behavior and copy/paste.
**Where:** `Editor/navigation.c`/`textview.c` time (`SDL_GetTicks`), `SDL_*ClipboardText`, and the autosave timer.
**Parity-risk:** Low.
**Guarded by:** unit + feature (Phase A/D).
**Status:** **Done.** Clock seam (`Editor/clock.h` + `clock_sdl.c`, test-side `clock_fake.c`) and clipboard seam (`Editor/clipboard.h` + `clipboard_sdl.c`, test-side `clipboard_fake.c`) both landed; status timing, autosave, and copy/yank go through them. Enabled deterministic `nav_status` + kill-ring/yank tests.

## Safety

### 12. Fixed-buffer / truncation audit
**What:** Audit fixed-size buffers and `snprintf`/`memcpy` sites for silent truncation or overflow; add explicit length handling or `log()` on truncation.
**Why:** Spots like `minibuf_text[1024]`, `recent_files[32][1024]`, prompt `snprintf`s, and per-line buffers should be confirmed safe; this is the memory-safety backstop (paired with ASan + fuzzing).
**Where:** `Editor/textview.c` (minibuffer, prompts, recent files), `Editor/buffer.c` (line/path handling).
**Parity-risk:** Low.
**Guarded by:** ASan + libFuzzer (`buf_load_file`, `md_detect_span`) (Phase A/E).

---

## Ideas parking lot (unprioritised)

- Formalise `renderer.h` as the **only** platform boundary and assert no `Editor/` file calls GL directly (it's nearly true already).
- Consider whether `stb_truetype.h` usage in `renderer.c` could be trimmed.
- Evaluate replacing manual MRU `recent_push` (O(n)) with a small ring once measured.
