/* unit_textview.c — headless tests for textview.c's event-translation and
 * modal-dispatch layer (previously 0% covered).
 *
 * These drive REAL SDL_Events through editor_handle_event(), so the modal
 * routing precedence is exercised, not bypassed. The textview.c singletons are
 * reached via the tv_test_* seam (editor_loop.h); the few platform calls
 * (SDL input state, the X bridge) are faked by platform_stub.c. Time goes
 * through the fake clock for the autosave test. */
#include "test.h"
#include "platform_stub.h"
#include "editor_loop.h"
#include "editor_types.h"
#include "buffer.h"
#include "clock_fake.h"
#include "pos_render.h"
#include "style_check.h"
#include "renderer.h"
#include <SDL2/SDL.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

static EditorState *ED;
static ViewState   *VS;

/* ---- harness ---- */

static void tv_begin(void) {
  static int inited = 0;
  if (!inited) { r_init(); inited = 1; }   /* stub renderer; do_render needs it */
  tv_test_reset();
  kern_test_platform_reset();
  ED = tv_test_ed();
  VS = tv_test_vs();
  buf_init_empty(ED);                       /* one empty line to start */
}

/* Replace the buffer with `content` (newline-separated). */
static void load(const char *content) {
  buf_free_all_lines(ED);
  free(ED->lines);
  ED->lines = NULL; ED->line_count = ED->line_cap = 0;
  buf_init_empty(ED);
  int idx = 0;
  const char *start = content;
  for (const char *p = content;; p++) {
    if (*p == '\n' || *p == '\0') {
      int len = (int)(p - start);
      if (idx == 0) {
        line_ensure_cap(&ED->lines[0], len);
        memcpy(ED->lines[0].text, start, len);
        ED->lines[0].text[len] = '\0';
        ED->lines[0].len = len;
        line_dirty(&ED->lines[0]);
      } else {
        buf_insert_line_at(ED, idx, start, len);
      }
      idx++;
      if (*p == '\0') break;
      start = p + 1;
    }
  }
  ED->cursor_line = ED->cursor_col = ED->cursor_target_col = 0;
}

static void put_cursor(int line, int col) {
  ED->cursor_line = line; ED->cursor_col = col; ED->cursor_target_col = col;
}

static void key(Uint16 mod, SDL_Keycode sym) {
  SDL_Event e; memset(&e, 0, sizeof e);
  e.type = SDL_KEYDOWN;
  e.key.keysym.sym = sym;
  e.key.keysym.mod = mod;
  editor_handle_event(&e);
}

static void textinput(const char *s) {
  SDL_Event e; memset(&e, 0, sizeof e);
  e.type = SDL_TEXTINPUT;
  snprintf(e.text.text, sizeof(e.text.text), "%s", s);
  editor_handle_event(&e);
}

/* Type a literal string as a sequence of SDL_TEXTINPUT events (one per byte) —
 * the modifier-guard / mode routing each char goes through is the real path. */
static void type(const char *s) {
  for (const char *p = s; *p; p++) {
    char c[2] = { *p, '\0' };
    textinput(c);
  }
}

#define EXPECT_LINE(i, str) CHECK_SEQ(ED->lines[(i)].text, (str))

/* make a fresh sandbox documents dir for file-touching tests */
static void fresh_docs_dir(char *out, int outsz) {
  char tmpl[] = "/tmp/kern_tv_XXXXXX";
  char *d = mkdtemp(tmpl);
  CHECK(d != NULL);
  snprintf(out, outsz, "%s", d ? d : "/tmp");
  buf_set_documents_dir(out);
}

/* ---------------------------------------------------------------- prefix chords */

static void test_cx_prefix_sets_and_clears(void) {
  tv_begin(); load("hi");
  key(KMOD_CTRL, SDLK_x);
  CHECK_IEQ(VS->ctrl_x_prefix, 1);
  /* C-x t toggles typewriter and clears the prefix */
  key(0, SDLK_t);
  CHECK_IEQ(VS->ctrl_x_prefix, 0);
  CHECK_IEQ(VS->typewriter_mode, 1);
}

static void test_cx_y_toggles_syntax(void) {
  tv_begin(); load("hi");
  CHECK_IEQ(VS->syntax_mask, 0);          /* off by default */
  key(KMOD_CTRL, SDLK_x);
  key(0, SDLK_y);                          /* C-x y turns it on */
  CHECK_IEQ(VS->ctrl_x_prefix, 0);         /* prefix cleared */
  CHECK_IEQ(VS->syntax_mask, SYNTAX_MASK_ALL);
  key(KMOD_CTRL, SDLK_x);
  key(0, SDLK_y);                          /* C-x y again turns it off */
  CHECK_IEQ(VS->syntax_mask, 0);
}

static void test_cx_s_toggles_style_check(void) {
  tv_begin(); load("hi");
  CHECK_IEQ(VS->style_mask, 0);            /* off by default */
  key(KMOD_CTRL, SDLK_x);
  key(0, SDLK_s);                          /* C-x s turns it on */
  CHECK_IEQ(VS->ctrl_x_prefix, 0);
  CHECK_IEQ(VS->style_mask, STYLE_MASK_ALL);
  key(KMOD_CTRL, SDLK_x);
  key(0, SDLK_s);                          /* C-x s again turns it off */
  CHECK_IEQ(VS->style_mask, 0);
}

static void test_cx_cf_opens_find_minibuffer(void) {
  tv_begin(); load("hi");
  key(KMOD_CTRL, SDLK_x);
  key(KMOD_CTRL, SDLK_f);
  CHECK_IEQ(VS->minibuf_active, 1);
  CHECK_SEQ(VS->minibuf_prompt, "Find file (Documents): ");
}

static void test_cx_unrecognized_still_clears_prefix(void) {
  tv_begin(); load("hi");
  key(KMOD_CTRL, SDLK_x);
  key(0, SDLK_z);                 /* no C-x z binding */
  CHECK_IEQ(VS->ctrl_x_prefix, 0);  /* prefix consumed regardless */
}

static void test_esc_clears_mark_else_starts_meta(void) {
  /* with an active mark, ESC quits the mark and does NOT start meta */
  tv_begin(); load("hi");
  buf_mark_set(ED);
  key(0, SDLK_ESCAPE);
  CHECK_IEQ(ED->mark_active, 0);
  CHECK_IEQ(VS->esc_prefix, 0);
  /* with no mark, ESC starts the meta prefix */
  tv_begin(); load("hi");
  key(0, SDLK_ESCAPE);
  CHECK_IEQ(VS->esc_prefix, 1);
}

static void test_meta_f_forward_word(void) {
  tv_begin(); load("hello world");
  put_cursor(0, 0);
  key(0, SDLK_ESCAPE);            /* start meta */
  CHECK_IEQ(VS->esc_prefix, 1);
  key(0, SDLK_f);                 /* M-f */
  CHECK_IEQ(VS->esc_prefix, 0);   /* prefix cleared */
  CHECK_IEQ(ED->cursor_col, 6);   /* past "hello", at the start of "world" */
}

/* --------------------------------------------------------- modal routing precedence */

static void test_minibuffer_beats_search(void) {
  /* both flags set: the minibuffer handler (checked first) must consume Enter
     and leave the search flag untouched (ordering at the 1243-1244 chain) */
  tv_begin(); load("x");
  VS->minibuf_active = 1;
  VS->search_active  = 1;
  VS->minibuf_callback = NULL;
  key(0, SDLK_RETURN);
  CHECK_IEQ(VS->minibuf_active, 0);  /* minibuffer handled it */
  CHECK_IEQ(VS->search_active, 1);   /* search never reached */
}

static void test_search_beats_ctrl_x_prefix(void) {
  tv_begin(); load("x");
  VS->search_active  = 1;
  VS->ctrl_x_prefix  = 1;
  key(0, SDLK_ESCAPE);               /* search ESC ends search, returns handled */
  CHECK_IEQ(VS->search_active, 0);
  CHECK_IEQ(VS->ctrl_x_prefix, 1);   /* C-x prefix handler never reached */
}

/* ----------------------------------------------------------------- text-input gating */

static void test_textinput_modifier_guard(void) {
  tv_begin(); load("");
  put_cursor(0, 0);
  kern_test_set_modstate(KMOD_CTRL);   /* a chord is in flight */
  textinput("a");
  CHECK_IEQ(ED->lines[0].len, 0);      /* dropped, not inserted */
  kern_test_set_modstate(KMOD_NONE);
  textinput("a");
  EXPECT_LINE(0, "a");                  /* now it inserts */
}

static void test_suppress_next_text(void) {
  tv_begin(); load("a");
  put_cursor(0, 1);
  VS->suppress_next_text = 1;          /* e.g. set by an Option chord */
  textinput("b");
  EXPECT_LINE(0, "a");                  /* swallowed */
  CHECK_IEQ(VS->suppress_next_text, 0); /* flag cleared */
  textinput("c");
  EXPECT_LINE(0, "ac");                 /* next one lands */
}

static void test_literal_tab_dropped(void) {
  tv_begin(); load("a");
  put_cursor(0, 1);
  textinput("\t");                      /* indentation is keydown-handled */
  EXPECT_LINE(0, "a");
}

/* --------------------------------------------------------------------- arrow keys */

static void test_shift_arrow_sets_mark_once(void) {
  tv_begin(); load("hello world");
  put_cursor(0, 0);
  key(KMOD_SHIFT, SDLK_RIGHT);
  CHECK_IEQ(ED->mark_active, 1);
  CHECK_IEQ(ED->mark_col, 0);        /* mark planted at the start */
  CHECK_IEQ(ED->cursor_col, 1);
  key(KMOD_SHIFT, SDLK_RIGHT);
  CHECK_IEQ(ED->mark_col, 0);        /* NOT re-planted */
  CHECK_IEQ(ED->cursor_col, 2);
}

static void test_type_star_wraps_region_to_bold(void) {
  /* select "world", type "**" → it surrounds the region (region stays active) */
  tv_begin(); load("hello world");
  put_cursor(0, 6);
  buf_mark_set(ED);
  put_cursor(0, 11);                  /* region = "world" */
  type("*");
  EXPECT_LINE(0, "hello *world*");
  CHECK_IEQ(ED->mark_active, 1);      /* mark survives so the next * stacks */
  type("*");
  EXPECT_LINE(0, "hello **world**");
}

static void test_type_eq_wraps_region_to_highlight(void) {
  /* select "world", type "==" → highlight markup (region stays active) */
  tv_begin(); load("hello world");
  put_cursor(0, 6);
  buf_mark_set(ED);
  put_cursor(0, 11);                  /* region = "world" */
  type("=");
  EXPECT_LINE(0, "hello =world=");
  CHECK_IEQ(ED->mark_active, 1);
  type("=");
  EXPECT_LINE(0, "hello ==world==");
}

static void test_type_char_without_region_inserts_normally(void) {
  tv_begin(); load("hi");
  put_cursor(0, 2);
  type("*");                          /* no mark: plain insert at the caret */
  EXPECT_LINE(0, "hi*");
  CHECK_IEQ(ED->mark_active, 0);
}

static void test_alt_arrow_word_jump(void) {
  tv_begin(); load("hello world");
  put_cursor(0, 0);
  key(KMOD_ALT, SDLK_RIGHT);         /* word jump, not char */
  CHECK_IEQ(ED->cursor_col, 6);
}

static void test_vertical_keeps_target_column(void) {
  tv_begin(); load("longline\nhi\nlongline");
  put_cursor(0, 8);                  /* end of a long line */
  key(0, SDLK_DOWN);                 /* short line clamps the visible column */
  CHECK_IEQ(ED->cursor_line, 1);
  key(0, SDLK_DOWN);                 /* target column restored on the long line */
  CHECK_IEQ(ED->cursor_line, 2);
  CHECK_IEQ(ED->cursor_col, 8);
}

/* ------------------------------------------------------------------------- isearch */

static void test_isearch_forward_and_repeat(void) {
  tv_begin(); load("foo bar foo baz");
  put_cursor(0, 0);
  key(KMOD_CTRL, SDLK_s);
  CHECK_IEQ(VS->search_active, 1);
  type("foo");
  CHECK_SEQ(VS->search_buf, "foo");
  CHECK(VS->search_match_line >= 0);
  int first = VS->search_match_col;
  key(KMOD_CTRL, SDLK_s);            /* next match */
  CHECK(VS->search_match_col > first);
}

static void test_isearch_direction_flip_and_abort(void) {
  tv_begin(); load("foo bar foo baz");
  put_cursor(0, 0);
  key(KMOD_CTRL, SDLK_s);
  type("foo");
  key(KMOD_CTRL, SDLK_s);            /* advance to the 2nd foo */
  int second = VS->search_match_col;
  key(KMOD_CTRL, SDLK_r);            /* reverse: back to the 1st */
  CHECK(VS->search_match_col < second);
  key(KMOD_CTRL, SDLK_g);            /* abort */
  CHECK_IEQ(VS->search_active, 0);
  CHECK_IEQ(VS->search_match_line, -1);
}

/* ---------------------------------------------------------------- minibuffer editing */

static void test_find_file_creates_buffer(void) {
  char dir[256]; fresh_docs_dir(dir, sizeof dir);
  tv_begin();
  key(KMOD_CTRL, SDLK_x);
  key(KMOD_CTRL, SDLK_f);
  CHECK_IEQ(VS->minibuf_active, 1);
  type("newnote.md");
  key(0, SDLK_RETURN);               /* fires open_or_create_file */
  CHECK_IEQ(VS->minibuf_active, 0);
  CHECK(strstr(ED->filepath, "newnote.md") != NULL);
  buf_set_documents_dir("");
}

static void test_minibuffer_tab_completion(void) {
  char dir[256]; fresh_docs_dir(dir, sizeof dir);
  char path[512]; snprintf(path, sizeof path, "%s/foobar.md", dir);
  buf_save_text(path, "x", 1);       /* a file to complete against */
  tv_begin();
  key(KMOD_CTRL, SDLK_x);
  key(KMOD_CTRL, SDLK_f);
  type("foo");
  key(0, SDLK_TAB);                  /* accept the ghost completion */
  CHECK_SEQ(VS->minibuf_text, "foobar.md");
  buf_set_documents_dir("");
}

static void test_save_as_writes_file(void) {
  char dir[256]; fresh_docs_dir(dir, sizeof dir);
  tv_begin(); load("hello save");
  key(KMOD_CTRL, SDLK_x);
  key(KMOD_CTRL, SDLK_w);            /* write-file (save as) */
  CHECK_IEQ(VS->minibuf_active, 1);
  type("saved.md");
  key(0, SDLK_RETURN);
  char path[512]; snprintf(path, sizeof path, "%s/saved.md", dir);
  FILE *f = fopen(path, "rb");
  CHECK(f != NULL);
  if (f) fclose(f);
  buf_set_documents_dir("");
}

/* --------------------------------------------------------------------- wikilink nav */

static void test_follow_wikilink_and_history(void) {
  char dir[256]; fresh_docs_dir(dir, sizeof dir);
  char origin[512], target[512];
  snprintf(origin, sizeof origin, "%s/origin.md", dir);
  snprintf(target, sizeof target, "%s/Target.md", dir);
  buf_save_text(origin, "see [[Target.md]] x", 19);
  buf_save_text(target, "i am target", 11);

  tv_begin();
  load("see [[Target.md]] x");
  snprintf(ED->filepath, sizeof ED->filepath, "%s", origin);
  ED->filename = "origin.md";
  put_cursor(0, 8);                  /* inside [[Target.md]] */
  key(KMOD_GUI, SDLK_RETURN);        /* Cmd-Enter follows */
  CHECK(strstr(ED->filepath, "Target.md") != NULL);

  key(KMOD_GUI | KMOD_SHIFT, SDLK_LEFT);   /* back */
  CHECK(strstr(ED->filepath, "origin.md") != NULL);
  key(KMOD_GUI | KMOD_SHIFT, SDLK_RIGHT);  /* forward */
  CHECK(strstr(ED->filepath, "Target.md") != NULL);
  buf_set_documents_dir("");
}

static void test_follow_wikilink_none_at_cursor(void) {
  tv_begin();
  load("no link here");
  put_cursor(0, 0);
  key(KMOD_GUI, SDLK_RETURN);
  CHECK_SEQ(VS->status_msg, "No wikilink at cursor");
}

static void test_wikilink_dropdown_accept(void) {
  char dir[256]; fresh_docs_dir(dir, sizeof dir);
  char path[512]; snprintf(path, sizeof path, "%s/Project.md", dir);
  buf_save_text(path, "x", 1);
  tv_begin();
  load("");
  put_cursor(0, 0);
  type("[[Pro");                     /* typing a wikilink query */
  editor_tick();                     /* wikilink_refresh runs here -> dropdown */
  key(0, SDLK_RETURN);               /* accept the single match */
  EXPECT_LINE(0, "[[Project.md]]");
  buf_set_documents_dir("");
}

static void test_wikilink_dropdown_escape_dismisses(void) {
  char dir[256]; fresh_docs_dir(dir, sizeof dir);
  char path[512]; snprintf(path, sizeof path, "%s/Project.md", dir);
  buf_save_text(path, "x", 1);
  tv_begin();
  load("");
  put_cursor(0, 0);
  type("[[Pro");
  editor_tick();
  key(0, SDLK_ESCAPE);               /* dismiss the dropdown */
  editor_tick();
  key(0, SDLK_RETURN);               /* Enter now splits the line instead of accepting */
  CHECK_IEQ(ED->line_count, 2);
  EXPECT_LINE(0, "[[Pro");
  buf_set_documents_dir("");
}

/* -------------------------------------------------------------------- autosave timer */

static void test_autosave_fires_after_interval(void) {
  char dir[256]; fresh_docs_dir(dir, sizeof dir);
  tv_begin(); load("autosave me");
  snprintf(ED->filepath, sizeof ED->filepath, "%s/auto.md", dir);
  ED->dirty = 1;

  kern_clock_set(10000);
  editor_tick();                     /* first tick anchors the timer; no save yet */
  char path[512]; snprintf(path, sizeof path, "%s/auto.md", dir);
  FILE *f = fopen(path, "rb");
  CHECK(f == NULL);                  /* nothing written before the interval */
  if (f) fclose(f);

  kern_clock_set(10000 + 3000);      /* past AUTOSAVE_INTERVAL_MS */
  editor_tick();
  f = fopen(path, "rb");
  CHECK(f != NULL);                  /* now it auto-saved */
  if (f) fclose(f);
  CHECK_SEQ(VS->status_msg, "Auto-saved");
  buf_set_documents_dir("");
}

static void test_autosave_skips_clean_and_scratch(void) {
  char dir[256]; fresh_docs_dir(dir, sizeof dir);
  /* clean buffer with a path: no write */
  tv_begin(); load("clean");
  snprintf(ED->filepath, sizeof ED->filepath, "%s/clean.md", dir);
  ED->dirty = 0;
  kern_clock_set(20000); editor_tick();
  kern_clock_set(20000 + 5000); editor_tick();
  char p1[512]; snprintf(p1, sizeof p1, "%s/clean.md", dir);
  FILE *f = fopen(p1, "rb");
  CHECK(f == NULL);
  if (f) fclose(f);

  /* dirty *scratch* (no path): nothing to save, no crash */
  tv_begin(); load("scratch");
  ED->filepath[0] = '\0';
  ED->dirty = 1;
  kern_clock_set(30000); editor_tick();
  kern_clock_set(30000 + 5000); editor_tick();
  /* reaching here without a write/crash is the assertion */
  CHECK(ED->filepath[0] == '\0');
  buf_set_documents_dir("");
}

/* --------------------------------------------------------------------------- suite */

void suite_textview(void) {
  /* prefix chords */
  RUN(test_cx_prefix_sets_and_clears);
  RUN(test_cx_y_toggles_syntax);
  RUN(test_cx_s_toggles_style_check);
  RUN(test_cx_cf_opens_find_minibuffer);
  RUN(test_cx_unrecognized_still_clears_prefix);
  RUN(test_esc_clears_mark_else_starts_meta);
  RUN(test_meta_f_forward_word);
  /* modal routing precedence */
  RUN(test_minibuffer_beats_search);
  RUN(test_search_beats_ctrl_x_prefix);
  /* text-input gating */
  RUN(test_textinput_modifier_guard);
  RUN(test_suppress_next_text);
  RUN(test_literal_tab_dropped);
  /* arrow keys */
  RUN(test_shift_arrow_sets_mark_once);
  RUN(test_type_star_wraps_region_to_bold);
  RUN(test_type_eq_wraps_region_to_highlight);
  RUN(test_type_char_without_region_inserts_normally);
  RUN(test_alt_arrow_word_jump);
  RUN(test_vertical_keeps_target_column);
  /* isearch */
  RUN(test_isearch_forward_and_repeat);
  RUN(test_isearch_direction_flip_and_abort);
  /* minibuffer editing */
  RUN(test_find_file_creates_buffer);
  RUN(test_minibuffer_tab_completion);
  RUN(test_save_as_writes_file);
  /* wikilink nav */
  RUN(test_follow_wikilink_and_history);
  RUN(test_follow_wikilink_none_at_cursor);
  RUN(test_wikilink_dropdown_accept);
  RUN(test_wikilink_dropdown_escape_dismisses);
  /* autosave */
  RUN(test_autosave_fires_after_interval);
  RUN(test_autosave_skips_clean_and_scratch);

  /* leave globals clean for any later suite */
  tv_test_reset();
  buf_set_documents_dir("");
}
