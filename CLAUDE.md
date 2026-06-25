# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## What this is

Kern is a macOS text editor — Emacs keybindings/editing model with an iA Writer look. A thin SwiftUI shell launches an editor written in C (SDL2 + OpenGL, `stb_truetype` for fonts). There is **no UI toolkit**: the editor draws immediately through a 12-function renderer interface. (The repo/folder is named `microui` for historical reasons — that dependency was removed; geometry types now come from `Editor/gfx.h`.)

## Features

**Keep this list current — add an entry whenever a user-facing feature lands.** It exists so a session can answer "do we have X?" without spelunking. Keybindings are tabulated in `README.md`.

- **Daily notes** — launch opens today's `YYYY-MM-DD.md` (seeded with a date heading); `Cmd-Shift-T` jumps to (or creates) today's note at any time; the buffer auto-saves every few seconds while modified.
- **Emacs editing model** — movement, kill/yank ring, operation-based grouped undo (`C-/`), open-line, transpose, word case ops (`editing.c` / `undo.c`, dispatched via `commands.c`).
- **Mark & region** — `C-Space` mark, cut/copy/yank synced with the macOS clipboard, select-whole-buffer, exchange point/mark.
- **Search** — incremental `C-s`/`C-r`; `⌘F` search with match highlights (`navigation.c`).
- **Live markdown** — headings / bold / italic styled as you type (`md_render.c`).
- **Notes & wikilinks** — `[[` autocomplete, `Cmd-Enter` follows the link, `Cmd-Shift-←/→` history; `Cmd-Shift-N` extracts the selection into a new linked note (`textview.c`, `recent.c`).
- **Files** — find/open with inline completion, save, save-as, recent-buffer switch (MRU); all paths sandboxed under the app container.
- **View** — font size `⌘=`/`⌘-`, recenter `C-l`, goto-line `M-g`, list indent/outdent (`Tab`/`Shift-Tab`).
- **Typewriter mode** — `C-x t` toggles; the active line pins at the golden ratio (~38% down the page) and the buffer scrolls under the cursor. Virtual whitespace below EOF lets the last line still pin. Shares `nav_pin_cursor` with `C-l` recenter; flag is `ViewState.typewriter_mode` (`navigation.c` / `commands.c`).
- **Syntax highlighting** — `C-x y` toggles part-of-speech highlighting. Unlike iA Writer's five-hue rainbow, the palette is **value, not hue** (Tufte): a luminance ramp by grammatical importance — verb (brightest) > noun > adjective > adverb >> function words (determiner/preposition/pronoun/conjunction/particle, dimmed so the grammatical glue recedes). What you read is the prose's content rhythm, figure vs ground. Tagging is behind a seam (`pos_tagger.h`): the app backs it with `NSLinguisticTagger` lexical-class scheme (`pos_tagger_nl.m` — same engine iA uses), tests with a fixture lexicon (`tests/pos_tagger_fake.c`). Per-line spans are lazily cached on the `Line` (`pos_spans`, invalidated by `line_dirty` like `md_spans`); `pos_render.c` owns the cache, the value palette, and a determiner→noun post-correction (a gerund right after "a/the" — "a heading" — is retagged from verb to noun). `md_draw_text` applies the per-span value, layering over markdown so bold/italic keep their weight. Bit-per-class mask is `ViewState.syntax_mask` (0 = off).
- **Style check** — `C-x s` toggles iA-Writer-style strike-through of cuttable text. A third per-line span layer (`style_check.c` → `Line.style_spans`, invalidated by `line_dirty` like `md_spans`/`pos_spans`); `md_draw_text` greys a struck word and draws a lightly-scribbled strike line (the `wave` wobble, one `r_draw_rect` per dash). Pure C — curated word/phrase lists (fillers, redundancies, and clichés/generalizations), whole-word/longest-match, no tagger or renderer dependency, fully headless-tested (`tests/unit_style.c`). Categories are `StyleCategory` (`STYLE_FILLER`/`STYLE_REDUNDANCY`/`STYLE_CLICHE`); bit-per-category mask is `ViewState.style_mask` (0 = off). Detection is precision-first (context-free entries only). Lists live in `k_patterns[]` in `style_check.c`.
- **X (Twitter) publishing** — a paper-plane **title-bar button** (built in `Platform/macos_style.m`) posts the current note (or marked region); it's shown only while an X account is connected (the editor loop polls `kern_x_is_connected()` and calls `kern_titlebar_set_x_connected()` on change). Clicking it calls the `kern_publish_to_x()` bridge in `textview.c`. Results report via both a macOS notification (`UNUserNotificationCenter`, authorized at launch in `AppDelegate`) and the C status bar. OAuth 2.0 PKCE via a loopback listener, Keychain tokens, **Settings → X** tab to connect. Client id injected at build from gitignored `Config/Secrets.xcconfig` → `Info.plist`. Lives in `App/KernApp.swift` + the `kern_x_*` bridge in `textview.c`; setup in `README.md`. (See the main-thread gotcha below.)

## Commands

All test/perf/coverage commands run from `tests/` (that's where the Makefile lives).

```sh
# Headless test suite (ASan + UBSan; LSan on Linux). ~200 tests.
cd tests && make test

# Coverage report for the testable core (clang llvm-cov)
cd tests && make coverage

# Performance harness (no sanitizers, -O2). Needs a corpus:
python3 tools/gen_prose.py 100MB test_100mb.txt   # from repo root; test_100mb.txt is gitignored
cd tests && make perf CORPUS=../test_100mb.txt    # add PERF_ARGS=--check for CI budgets

# Build the macOS app (fast, ~3s)
xcodebuild -project application/Kern/Kern.xcodeproj -scheme Kern -configuration Debug build CODE_SIGNING_ALLOWED=NO
```

- **Pre-push gate:** `.githooks/pre-push` runs all three CI checks locally before a push and aborts on failure. Activate once per clone with `git config core.hooksPath .githooks`; bypass a single push with `git push --no-verify`. It caches a 25MB perf corpus at `tests/perf_corpus.txt` (gitignored; override with `KERN_PERF_CORPUS`).
- **Running a single test:** the harness has no name filter. To narrow, comment out `RUN(...)` lines or `suite_*()` calls in `tests/test_main.c`.
- **Snapshot goldens:** after an *intentional* render change, regenerate with `KERN_UPDATE_SNAPSHOTS=1 make test` (goldens live in `tests/snapshots/`).
- **Run the built app:** `open` the product under `~/Library/Developer/Xcode/DerivedData/Kern-*/Build/Products/Debug/Kern.app`, or ⌘R in Xcode.

## How to verify a change (important)

The test build compiles the core **plus** `Editor/textview.c` — built with `-DKERN_HEADLESS_TEST`, which `#ifdef`s out only `editor_main`/`resize_event_watcher` (the real-SDL init + blocking loop) and exposes the `tv_test_*` seam. So:
- Changes to `buffer/editing/undo/navigation/md_render/recent/commands.c` → verify with `make test` (and `make coverage`).
- Changes to `textview.c`'s **event/dispatch + modal layer** (the `editor_handle_event` switch, prefix chords, wikilink nav, minibuffer, isearch, autosave in `editor_tick`) → covered by `tests/unit_textview.c`, which pumps synthetic `SDL_Event`s through `editor_handle_event` against the stub renderer + `tests/platform_stub.c`. Verify with `make test`.
- Changes to the parts still **not** compiled in tests — `editor_main` itself, the real-SDL render/init path, AppKit chrome, anything Swift — verify by building the app with `xcodebuild` and launching it. Confirm the app actually has a live pid before claiming it works.

## Architecture

**The renderer seam.** `Platform/renderer.h` is a 12-function interface (`r_draw_rect`, `r_draw_text`, `r_get_text_width`, `r_set_font_style`, `r_get_size`, …). The app implements it with SDL/GL (`Platform/renderer.c`); the tests implement it with a deterministic capture stub (`tests/stub_renderer.{c,h}`, 10×20px glyphs, 800×600 window, records every draw op). Any `Editor/` code that needs metrics or drawing goes through this seam, which is what makes layout testable headlessly.

**State lives in two structs.** `EditorState` (document model — buffer, cursor, mark, kill ring, undo; no SDL) and `ViewState` (UI — scroll, fonts, search/minibuffer/prefix flags, status). Defined in `Editor/editor_types.h`. **Every core function takes explicit `EditorState*`/`ViewState*` pointers.** The only globals are `g_ed`/`g_vs` singletons inside `textview.c`.

**Two tiers of `Editor/` code:**
- *Pure core* — `buffer.c` (lines, file I/O, kill buffer, mark/region, sandbox paths), `editing.c` (all `ed_*` ops), `undo.c` (operation-based, grouped). No renderer dependency.
- *Renderer-dependent but still headless-testable* — `navigation.c` (wrap, cursor/visual mapping, click-to-cursor, search), `md_render.c` (inline markdown spans + drawing), `commands.c` (the command table + `kern_dispatch_key`), `recent.c` (MRU). These depend only on `renderer.h`, so they compile into *both* the app and the test binary.
- *Partly headless-testable shell* — `textview.c` (~1,400 lines): its per-event work (`editor_handle_event`) and per-frame work (`editor_tick`) are extracted from `editor_main` so the modal/dispatch layer (minibuffer, wikilinks, buffer-switch, prefix chords, isearch, autosave) is driven in `tests/unit_textview.c` via the `tv_test_*` seam (`Editor/editor_loop.h`). The remainder — `editor_main`'s real-SDL init + blocking loop and the GL render path — stays app-only.

**Command dispatch.** `commands.c` owns the single binding table `g_commands[]` + `kern_dispatch_key(ed, vs, kmod, sym)`. `textview.c`'s keydown handler calls `kern_dispatch_key` first; only things needing local UI state are handled outside it: `M-g` (minibuffer), the `C-x …` and `ESC`/meta prefix chords (`handle_*_prefix_key`), and the arrow keys (which delegate to the `commands.c` movement functions, layering shift-select + word-jump). Feature tests drive `kern_dispatch_key` directly in `tests/unit_commands.c`.

**Lazy per-line caches.** `Line` carries `wrap_count` (cached visual-row count, `-1` = dirty) and `md_spans`/`md_span_count` (cached inline-span map, `-1` = not computed). `line_dirty()` invalidates both on edit; `buf_invalidate_all_wraps()` clears wraps on a page-width or font-size change. `nav_maybe_reflow` skips the full re-wrap when a resize doesn't change page width.

**Determinism seams.** Time goes through `Editor/clock.h` (`kern_now_ms()`; app uses `clock_sdl.c`, tests use a settable `clock_fake.c`). Clipboard goes through `Editor/clipboard.h` (app `clipboard_sdl.c`, tests `clipboard_fake.c`). Use these, not SDL directly, in `Editor/` code.

## Conventions / gotchas

- **Always measure wrap and column math in `FONT_REGULAR`.** A frame ends with the status bar's `FONT_MONO` active; if an event-time wrap recompute measures in mono, it caches wrong wrap counts → phantom rows and mis-placed click-to-cursor. `navigation.c`'s `body_width()` and the wrap functions force `FONT_REGULAR` regardless of ambient style — preserve this in any wrap/metric change.
- **Columns are byte-indexed**, not codepoint-indexed (UTF-8 inserts advance the cursor by byte length).
- **File paths are sandboxed** via `buf_set_documents_dir()` (set from Swift at launch); when unset (CLI/test builds) paths are used verbatim. User paths resolve under the documents dir via `buf_resolve_path`.
- **Test layout:** `tests/ed_fixture.h` provides `ed_load`/`ed_teardown`/`LINE()`; `tests/test.h` provides `CHECK`/`CHECK_IEQ`/`CHECK_SEQ` and `RUN`. Each `unit_*.c` exposes one `suite_*()` listed in `test_main.c`. Snapshots prove render parity; they re-implement the text pass, so a `do_render` text-loop refactor won't be caught by goldens.
- **Coverage reality:** core is ~99% lines / 100% functions; the residual uncovered lines/branches are malloc/IO-failure paths and defensive guards (need fault injection, not more tests). The gated coverage floor (≥85%, `make coverage`) measures `COVCORE` = the core only — `textview.c` is **deliberately excluded** so its partly-tested shell doesn't mask the core's saturation. `textview.c` is ~53% lines (event/dispatch + modal handlers tested; uncovered remainder is the `do_render`/`process_frame` GL chrome and the `#ifdef`'d-out `editor_main`); see it with `make coverage-textview`.
- **Pumping events in tests:** drive `editor_handle_event` with synthetic `SDL_Event`s (`tests/unit_textview.c` has `key()`/`textinput()`/`type()` builders). The modal-routing precedence is real — don't bypass it by calling `handle_*` directly. The wikilink dropdown (`wl_active`) is recomputed inside `do_render`, so activate it by typing then calling `editor_tick()` before sending the accept/dismiss key. `tv_test_reset()` clears `g_ed`/`g_vs` + all modal file-statics between tests; `platform_stub.c` fakes the SDL input-state calls and the `kern_x_*` bridge. **Keep `editor_main` loop-shape-preserving** (wait → drain via `editor_handle_event` → `editor_tick`) — that's what keeps it both snappy and testable.
- **The main thread is parked by `editor_main`.** It runs SDL's blocking event loop and never returns, so the main thread / main actor is stuck for the app's life. Any Swift feature added to the app **must run off the main actor and report UI through the C status bar** (`kern_x_set_status`). `DispatchQueue.main.async`, main-actor `Task {}`, SwiftUI live updates, and `ASWebAuthenticationSession` all silently deadlock. Background threads (`Task.detached`, GCD, `NWListener`, `URLSession`, `NSWorkspace.open`) work fine. The X publishing layer (`App/KernApp.swift`) is the reference pattern.

## Docs

`docs/TESTING_PLAN.md` (strategy), `docs/TESTING_PROGRESS.md` (living status log — update it as testing/refactor work lands), `docs/REFACTORING.md` (catalogue of candidate refactors with status). Commit messages end with a `Co-Authored-By` trailer. **Never create branches or PRs — all work commits directly to `main` and pushes there.**
