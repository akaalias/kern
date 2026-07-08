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
#include "graph.h"
#include "editor_types.h"
#include "buffer.h"
#include "clock_fake.h"
#include "clipboard.h"
#include "pos_render.h"
#include "style_check.h"
#include "renderer.h"
#include "stub_renderer.h"
#include <SDL2/SDL.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

/* textview.c bridges the app normally reaches from AppKit / Swift. */
void kern_publish_to_x(void);                     /* title-bar Publish button */
/* menu-bar bridges (each drives the same path as its keyboard chord) */
void kern_menu_save(void);
void kern_menu_switch_buffer(void);
void kern_menu_undo(void);
void kern_menu_select_all(void);
void kern_menu_kill_line(void);
void kern_menu_bold(void);
void kern_menu_highlight(void);
void kern_menu_typewriter(void);   int kern_typewriter_enabled(void);
void kern_menu_page_borders(void); int kern_page_borders_enabled(void);
void kern_menu_graph_view(void);   int kern_graph_enabled(void);
void kern_menu_font_bigger(void);
void kern_menu_bottom(void);
void kern_menu_goto_line(void);
void kern_menu_search_fwd(void);
void kern_menu_extract_note(void);
void kern_menu_fetch_news(void);
void kern_x_publish_done(int ok, const char *info);  /* async publish result */
void kern_x_tweet_done(int ok, const char *id, const char *name,
                       const char *handle, const char *date,
                       const char *text);            /* async tweet-fetch result */
void kern_x_feed_done(int ok, const char *text);     /* async news-feed result */
void kern_x_bookmarks_done(int ok, const char *text);   /* async bookmarks result */
int  kern_feed_skip_post(const char *text);          /* news-feed noise filter */
void kern_feed_quote_text(const char *text, char *out, int outsz);  /* "> " prefixer */
/* kern_reply_scan + KernReplyTarget come from editor_loop.h */

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

static void test_cx_p_toggles_page_furniture(void) {
  tv_begin(); load("hi");
  CHECK_IEQ(VS->page_furniture_hidden, 0);  /* shown by default */
  key(KMOD_CTRL, SDLK_x);
  key(0, SDLK_p);                           /* C-x p hides borders/gutters */
  CHECK_IEQ(VS->ctrl_x_prefix, 0);          /* prefix cleared */
  CHECK_IEQ(VS->page_furniture_hidden, 1);
  key(KMOD_CTRL, SDLK_x);
  key(0, SDLK_p);                           /* C-x p again shows them */
  CHECK_IEQ(VS->page_furniture_hidden, 0);
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

/* Backspace and Delete with an active selection remove the whole region (the
   reported bug: the marked text wasn't deleted). Driven through the real event
   dispatch. */
static void test_backspace_deletes_marked_region(void) {
  tv_begin(); load("hello world");
  put_cursor(0, 0);
  buf_mark_set(ED);
  put_cursor(0, 6);                  /* select "hello " */
  key(0, SDLK_BACKSPACE);
  EXPECT_LINE(0, "world");
  CHECK_IEQ(ED->mark_active, 0);

  tv_begin(); load("hello world");
  put_cursor(0, 0);
  buf_mark_set(ED);
  put_cursor(0, 6);
  key(0, SDLK_DELETE);
  EXPECT_LINE(0, "world");
  CHECK_IEQ(ED->mark_active, 0);
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

/* "Opened after": the file open right before we open another becomes a
   predecessor of it. Driven through the real C-x C-f open path. */
static void test_opened_after_records_predecessor(void) {
  char dir[256]; fresh_docs_dir(dir, sizeof dir);
  char xxx[512]; snprintf(xxx, sizeof xxx, "%s/XXX.md", dir);
  buf_save_text(xxx, "x", 1);
  tv_begin(); load("x");
  snprintf(ED->filepath, sizeof ED->filepath, "%s", xxx);   /* we're in XXX.md */
  ED->filename = "XXX.md";
  key(KMOD_CTRL, SDLK_x); key(KMOD_CTRL, SDLK_f);
  type("Foo.md");
  key(0, SDLK_RETURN);                     /* open Foo.md while XXX.md was open */
  CHECK(strstr(ED->filepath, "Foo.md") != NULL);
  char preds[8][256];
  int n = tv_test_opened_after(ED->filepath, preds, 8);
  CHECK_IEQ(n, 1);
  CHECK_SEQ(preds[0], "XXX.md");
  buf_set_documents_dir("");
}

/* Predecessors accumulate most-recent-first and never duplicate (an existing
   predecessor is lifted to the front instead of being appended again). */
static void test_opened_after_accumulates_and_dedups(void) {
  char dir[256]; fresh_docs_dir(dir, sizeof dir);
  char target[512]; snprintf(target, sizeof target, "%s/Target.md", dir);
  tv_begin(); load("");
  char a[512]; snprintf(a, sizeof a, "%s/A.md", dir);
  snprintf(ED->filepath, sizeof ED->filepath, "%s", a);   /* start in A.md */
  ED->filename = "A.md";

  key(KMOD_CTRL, SDLK_x); key(KMOD_CTRL, SDLK_f); type("Target.md"); key(0, SDLK_RETURN);
  key(KMOD_CTRL, SDLK_x); key(KMOD_CTRL, SDLK_f); type("B.md");      key(0, SDLK_RETURN);
  key(KMOD_CTRL, SDLK_x); key(KMOD_CTRL, SDLK_f); type("Target.md"); key(0, SDLK_RETURN);

  char preds[8][256];
  int n = tv_test_opened_after(target, preds, 8);
  CHECK_IEQ(n, 2);
  CHECK_SEQ(preds[0], "B.md");    /* most recent predecessor first */
  CHECK_SEQ(preds[1], "A.md");

  /* re-opening Target from B lifts B to the front; no duplicate entry */
  key(KMOD_CTRL, SDLK_x); key(KMOD_CTRL, SDLK_f); type("A.md");      key(0, SDLK_RETURN);
  key(KMOD_CTRL, SDLK_x); key(KMOD_CTRL, SDLK_f); type("Target.md"); key(0, SDLK_RETURN);
  n = tv_test_opened_after(target, preds, 8);
  CHECK_IEQ(n, 2);
  CHECK_SEQ(preds[0], "A.md");
  CHECK_SEQ(preds[1], "B.md");
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

/* A bare [[Target]] resolves to the on-disk Target.md, like Obsidian — both
   link forms open the same note instead of the bare one spawning a second,
   extensionless file. */
static void test_follow_wikilink_bare_name_resolves_md(void) {
  char dir[256]; fresh_docs_dir(dir, sizeof dir);
  char origin[512], target[512];
  snprintf(origin, sizeof origin, "%s/origin.md", dir);
  snprintf(target, sizeof target, "%s/Target.md", dir);
  buf_save_text(origin, "see [[Target]] x", 16);
  buf_save_text(target, "i am target", 11);

  tv_begin();
  load("see [[Target]] x");
  snprintf(ED->filepath, sizeof ED->filepath, "%s", origin);
  ED->filename = "origin.md";
  put_cursor(0, 8);                  /* inside [[Target]] */
  key(KMOD_GUI, SDLK_RETURN);        /* Cmd-Enter follows */
  CHECK(strstr(ED->filepath, "Target.md") != NULL);
  EXPECT_LINE(0, "i am target");
  buf_set_documents_dir("");
}

/* Following a link to a note that doesn't exist yet creates it as .md, so the
   new note shows up in the [[ autocomplete and the graph like any other. */
static void test_follow_wikilink_creates_md_note(void) {
  char dir[256]; fresh_docs_dir(dir, sizeof dir);
  char origin[512];
  snprintf(origin, sizeof origin, "%s/origin.md", dir);
  buf_save_text(origin, "see [[Ghost]] x", 15);

  tv_begin();
  load("see [[Ghost]] x");
  snprintf(ED->filepath, sizeof ED->filepath, "%s", origin);
  ED->filename = "origin.md";
  put_cursor(0, 8);
  key(KMOD_GUI, SDLK_RETURN);
  CHECK(strstr(ED->filepath, "Ghost.md") != NULL);
  buf_set_documents_dir("");
}

/* [[Target|Alias]]: Cmd-Enter follows the file before the '|', not the
   display alias. */
static void test_follow_wikilink_alias(void) {
  char dir[256]; fresh_docs_dir(dir, sizeof dir);
  char origin[512], target[512];
  snprintf(origin, sizeof origin, "%s/origin.md", dir);
  snprintf(target, sizeof target, "%s/Target.md", dir);
  buf_save_text(origin, "see [[Target|a nice alias]] x", 29);
  buf_save_text(target, "i am target", 11);

  tv_begin();
  load("see [[Target|a nice alias]] x");
  snprintf(ED->filepath, sizeof ED->filepath, "%s", origin);
  ED->filename = "origin.md";
  put_cursor(0, 20);                 /* inside the alias half */
  key(KMOD_GUI, SDLK_RETURN);
  CHECK(strstr(ED->filepath, "Target.md") != NULL);
  EXPECT_LINE(0, "i am target");
  buf_set_documents_dir("");
}

static void test_follow_wikilink_none_at_cursor(void) {
  tv_begin();
  load("no link here");
  put_cursor(0, 0);
  key(KMOD_GUI, SDLK_RETURN);
  CHECK_SEQ(VS->status_msg, "No wikilink at cursor");
}

/* Cmd-Enter over an http(s):// URL opens it in the browser (instead of the
   wikilink follow) — caret left-of, inside, and right-of the URL all count. */
static void test_follow_url_opens_browser(void) {
  const char *url = "https://blabla.com";

  /* caret inside the URL */
  tv_begin();
  load("see https://blabla.com here");
  put_cursor(0, 10);
  key(KMOD_GUI, SDLK_RETURN);
  CHECK_SEQ(kern_test_last_opened_url(), url);

  /* caret at the left edge (on the first char) */
  tv_begin();
  load("see https://blabla.com here");
  put_cursor(0, 4);
  key(KMOD_GUI, SDLK_RETURN);
  CHECK_SEQ(kern_test_last_opened_url(), url);

  /* caret just past the last char (right edge) */
  tv_begin();
  load("see https://blabla.com here");
  put_cursor(0, 22);
  key(KMOD_GUI, SDLK_RETURN);
  CHECK_SEQ(kern_test_last_opened_url(), url);
}

/* Cmd-Enter away from any URL still falls through to the wikilink follow. */
static void test_follow_url_absent_falls_through_to_wikilink(void) {
  tv_begin();
  load("no link here");
  put_cursor(0, 0);
  key(KMOD_GUI, SDLK_RETURN);
  CHECK(kern_test_last_opened_url() == NULL);
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

/* ---- margin notes (Cmd-Shift-M -> markdown footnote) ---- */

/* Cmd-Shift-M in typewriter mode opens the margin-note input; typed text goes to
   the note (not the buffer); Enter inserts a [^id] marker at the caret and
   appends a "[^id]: <note>" definition at the bottom — no == highlighting. */
static void test_margin_note_creates_footnote(void) {
  tv_begin();
  load("Hello world");
  VS->typewriter_mode = 1;
  put_cursor(0, 11);                          /* end of the line */
  key(KMOD_GUI | KMOD_SHIFT, SDLK_m);         /* Cmd-Shift-M */
  type("a side note");                        /* goes to the note, not the doc */
  key(0, SDLK_RETURN);                        /* commit */

  CHECK(strstr(ED->lines[0].text, "[^") != NULL);     /* marker on the caret line */
  CHECK(strstr(ED->lines[0].text, "==") == NULL);     /* no highlight wrapping */
  const char *last = ED->lines[ED->line_count - 1].text;
  CHECK(last[0] == '[' && last[1] == '^');             /* definition appended */
  CHECK(strstr(last, "]: a side note") != NULL);
}

/* Esc cancels the input: no marker, no definition, buffer untouched. */
static void test_margin_note_escape_cancels(void) {
  tv_begin();
  load("Hello world");
  VS->typewriter_mode = 1;
  put_cursor(0, 11);
  key(KMOD_GUI | KMOD_SHIFT, SDLK_m);
  type("discard me");
  key(0, SDLK_ESCAPE);
  CHECK_IEQ(ED->line_count, 1);
  EXPECT_LINE(0, "Hello world");
}

/* In normal mode on a narrow window there's no room for the note strip, so
   Cmd-Shift-M is a no-op (notes stay as bottom-of-document footnotes). */
static void test_margin_note_no_room(void) {
  stub_set_metrics(10, 20, 800, 600);   /* page margin too small for the strip */
  tv_begin();
  load("Hello world");
  VS->typewriter_mode = 0;
  put_cursor(0, 11);
  key(KMOD_GUI | KMOD_SHIFT, SDLK_m);
  key(0, SDLK_RETURN);
  CHECK(strstr(ED->lines[0].text, "[^") == NULL);     /* no footnote marker */
}

/* In normal mode on a wide-enough window, margin notes work just like in
   typewriter mode: Cmd-Shift-M inserts a marker + appends a definition. */
static void test_margin_note_normal_mode_with_room(void) {
  stub_set_metrics(10, 20, 4000, 600);   /* wide → the note strip fits */
  tv_begin();
  load("Hello world");
  VS->typewriter_mode = 0;
  put_cursor(0, 11);
  key(KMOD_GUI | KMOD_SHIFT, SDLK_m);
  type("normal mode note");
  key(0, SDLK_RETURN);
  CHECK(strstr(ED->lines[0].text, "[^") != NULL);                 /* marker */
  CHECK(strstr(ED->lines[ED->line_count - 1].text, "]: normal mode note") != NULL);
  stub_set_metrics(10, 20, 800, 600);    /* restore the default window */
}

/* ---- X publish confirmation overlay (kern_publish_to_x -> preview -> confirm) ---- */

/* Clicking Publish while connected opens the confirmation overlay with the note
   snapshotted for preview — it does NOT post yet. */
static void test_publish_opens_overlay(void) {
  tv_begin();
  load("Hello X world");
  kern_test_set_x_connected(1);
  kern_publish_to_x();                         /* the title-bar button */
  CHECK_IEQ(tv_test_pub_state(), 1);           /* confirming */
  CHECK_SEQ(tv_test_pub_text(), "Hello X world");
  CHECK(kern_test_x_last_publish() == NULL);   /* nothing posted yet */
}

/* With no account linked, Publish just reports and never opens the overlay. */
static void test_publish_not_connected_no_overlay(void) {
  tv_begin();
  load("Hello");
  kern_test_set_x_connected(0);
  kern_publish_to_x();
  CHECK_IEQ(tv_test_pub_state(), 0);
  CHECK(kern_test_x_last_publish() == NULL);
}

/* With a region marked, the overlay previews (and later posts) only the region. */
static void test_publish_overlay_uses_region(void) {
  tv_begin();
  load("keep DROP");
  kern_test_set_x_connected(1);
  put_cursor(0, 0);
  ED->mark_active = 1; ED->mark_line = 0; ED->mark_col = 4;   /* select "keep" */
  ED->cursor_col = 0;
  kern_publish_to_x();
  CHECK_IEQ(tv_test_pub_state(), 1);
  CHECK_SEQ(tv_test_pub_text(), "keep");
}

/* Enter (or the Confirm button) posts the snapshotted text and enters "sending". */
static void test_overlay_confirm_publishes(void) {
  tv_begin();
  load("ship it");
  kern_test_set_x_connected(1);
  kern_publish_to_x();
  key(0, SDLK_RETURN);                         /* confirm */
  CHECK_IEQ(tv_test_pub_state(), 2);           /* sending */
  CHECK(kern_test_x_last_publish() != NULL);
  CHECK_SEQ(kern_test_x_last_publish(), "ship it");
}

/* Esc (or Cancel) closes the overlay without posting. */
static void test_overlay_cancel_closes(void) {
  tv_begin();
  load("nope");
  kern_test_set_x_connected(1);
  kern_publish_to_x();
  key(0, SDLK_ESCAPE);
  CHECK_IEQ(tv_test_pub_state(), 0);
  CHECK(kern_test_x_last_publish() == NULL);
}

/* A successful async result copies the tweet URL to the clipboard, reports
   success, and closes the overlay (consumed on the main-thread tick). */
static void test_publish_success_copies_url_and_closes(void) {
  tv_begin();
  load("done");
  kern_test_set_x_connected(1);
  kern_publish_to_x();
  key(0, SDLK_RETURN);
  kern_x_publish_done(1, "https://x.com/testuser/status/42");
  editor_tick();                               /* main thread consumes the result */
  CHECK_IEQ(tv_test_pub_state(), 0);
  char *clip = kern_clipboard_get();
  CHECK_SEQ(clip, "https://x.com/testuser/status/42");
  kern_clipboard_free(clip);
  CHECK(strstr(VS->status_msg, "Posted") != NULL);
}

/* A failed result reports the error and returns the overlay to the confirm
   state (so the user can retry or cancel) — clipboard untouched. */
static void test_publish_failure_keeps_overlay(void) {
  tv_begin();
  load("oops");
  kern_test_set_x_connected(1);
  kern_clipboard_set("PRESERVE");
  kern_publish_to_x();
  key(0, SDLK_RETURN);
  kern_x_publish_done(0, "rate limit exceeded");
  editor_tick();
  CHECK_IEQ(tv_test_pub_state(), 1);           /* back to confirming */
  char *clip = kern_clipboard_get();
  CHECK_SEQ(clip, "PRESERVE");                  /* not overwritten */
  kern_clipboard_free(clip);
  CHECK(strstr(VS->status_msg, "rate limit") != NULL);
}

/* ---- Error log: async X failures are written to a Kern-Errors.md note in
   the documents dir (newest entry on top, blockquoted detail) so a long API
   error outlives the 3-second status bar and can be read in the editor. ---- */

/* slurp <docs>/Kern-Errors.md; NULL if absent */
static char *read_error_note(const char *docs) {
  char path[1024]; snprintf(path, sizeof(path), "%s/Kern-Errors.md", docs);
  FILE *f = fopen(path, "rb");
  if (!f) return NULL;
  static char buf[8192];
  size_t n = fread(buf, 1, sizeof(buf) - 1, f);
  buf[n] = '\0'; fclose(f);
  return buf;
}

static void test_publish_failure_logs_error_note(void) {
  tv_begin(); load("oops");
  char docs[512]; fresh_docs_dir(docs, sizeof(docs));
  kern_test_set_x_connected(1);
  kern_publish_to_x();
  key(0, SDLK_RETURN);
  kern_x_publish_done(0, "403 Forbidden: your tier lacks access\nsecond detail line");
  editor_tick();
  char *note = read_error_note(docs);
  CHECK(note != NULL);
  if (note) {
    CHECK(strncmp(note, "# Kern Errors", 13) == 0);
    CHECK(strstr(note, "## X publish failed \xE2\x80\x94 ") != NULL);
    /* multi-line detail is blockquoted line by line */
    CHECK(strstr(note, "> 403 Forbidden: your tier lacks access\n> second detail line") != NULL);
  }
  buf_set_documents_dir("");
}

/* A second failure lands ABOVE the first (newest-first), under one title. */
static void test_error_log_newest_entry_first(void) {
  tv_begin(); load("x");
  char docs[512]; fresh_docs_dir(docs, sizeof(docs));
  kern_test_set_x_connected(1);
  kern_publish_to_x();
  key(0, SDLK_RETURN);
  kern_x_publish_done(0, "older error"); editor_tick();
  key(0, SDLK_RETURN);                       /* overlay is back at confirm; retry */
  kern_x_publish_done(0, "newer error"); editor_tick();
  char *note = read_error_note(docs);
  CHECK(note != NULL);
  if (note) {
    char *newer = strstr(note, "newer error"), *older = strstr(note, "older error");
    CHECK(newer != NULL && older != NULL && newer < older);
    CHECK(strstr(note + 1, "# Kern Errors") == NULL);   /* title not duplicated */
  }
  buf_set_documents_dir("");
}

/* Feed / bookmarks fetch failures log too, each under its own context. */
static void test_feed_failure_logs_error_note(void) {
  tv_begin(); load("keep");
  char docs[512]; fresh_docs_dir(docs, sizeof(docs));
  kern_test_set_x_connected(1);
  key(KMOD_CTRL, SDLK_x);
  key(0, SDLK_n);
  kern_x_feed_done(0, "X: rate limit exceeded");
  editor_tick();
  char *note = read_error_note(docs);
  CHECK(note != NULL);
  if (note) {
    CHECK(strstr(note, "## X feed download failed \xE2\x80\x94 ") != NULL);
    CHECK(strstr(note, "> X: rate limit exceeded") != NULL);
  }
  buf_set_documents_dir("");
}

/* Without a documents dir (bare CLI/test build) nothing is written. */
static void test_error_log_noop_without_documents_dir(void) {
  tv_begin(); load("x");
  buf_set_documents_dir("");
  unlink("Kern-Errors.md");
  kern_test_set_x_connected(1);
  kern_publish_to_x();
  key(0, SDLK_RETURN);
  kern_x_publish_done(0, "boom"); editor_tick();
  FILE *f = fopen("Kern-Errors.md", "rb");
  CHECK(f == NULL);
  if (f) fclose(f);
}

/* The overlay swallows ordinary keystrokes — typing while it's open must not
   reach the buffer. */
static void test_overlay_swallows_typing(void) {
  tv_begin();
  load("body");
  kern_test_set_x_connected(1);
  kern_publish_to_x();
  type("XYZ");
  EXPECT_LINE(0, "body");                        /* buffer untouched */
}

/* ---- Publish-as-reply: a note holding an X status URL followed by commentary
   posts the commentary as a REPLY to that tweet (POST /2/tweets with
   reply.in_reply_to_tweet_id). ---- */

/* kern_reply_scan finds the last x.com/<handle>/status/<id> URL and returns
   the trimmed commentary after it, extracting the tweet id + author handle —
   plus, from the feed-entry structure above the URL, the author display name,
   date, and the quoted text (for the original-post preview in the overlay). */
static void test_reply_scan_extracts_target_and_commentary(void) {
  KernReplyTarget t; int clen = 0;
  const char *c = kern_reply_scan(
      "## signull \xE2\x80\x94 2026-07-06 at 16:55\n\n"
      "> any numbers game you play makes you either the gambler or the house.\n"
      ">\n"
      "> at that point, which side are you on?\n\n"
      "https://x.com/signulll/status/2074145302907441169\n\n"
      "which is why the house always publishes\nthe odds.\n",
      &t, &clen);
  CHECK(c != NULL);
  CHECK_SEQ(t.id, "2074145302907441169");
  CHECK_SEQ(t.handle, "signulll");
  CHECK_SEQ(t.author, "signull");
  CHECK_SEQ(t.date, "Jul 6");                  /* 2026-07-06 prettified */
  CHECK_SEQ(t.quote, "any numbers game you play makes you either the gambler "
                     "or the house.\n\nat that point, which side are you on?");
  CHECK_IEQ(strncmp(c, "which is why", 12), 0);
  /* trimmed both ends: ends at "odds." (no trailing newline) */
  CHECK_IEQ(c[clen - 1], '.');
  CHECK_IEQ(strncmp(c + clen - 5, "odds.", 5), 0);
}

/* A hand-written URL with none of the feed structure above it still replies —
   just with no quoted-tweet preview (author/date/quote empty). */
static void test_reply_scan_bare_url_no_quote(void) {
  KernReplyTarget t; int clen = 0;
  const char *c = kern_reply_scan(
      "some intro text\nhttps://x.com/someone/status/555\nmy take",
      &t, &clen);
  CHECK(c != NULL);
  CHECK_SEQ(t.id, "555");
  CHECK_SEQ(t.author, "");
  CHECK_SEQ(t.quote, "");
}

/* twitter.com URLs work too, and with several entries the LAST URL is the
   reply target (text between entries belongs to the next entry, not us). */
static void test_reply_scan_last_url_wins(void) {
  KernReplyTarget t; int clen = 0;
  const char *c = kern_reply_scan(
      "https://twitter.com/first/status/111\n\nnot commentary, next entry\n\n"
      "https://twitter.com/second/status/222\n\nactual commentary",
      &t, &clen);
  CHECK(c != NULL);
  CHECK_SEQ(t.id, "222");
  CHECK_SEQ(t.handle, "second");
  CHECK_IEQ(strncmp(c, "actual commentary", clen), 0);
}

/* Nothing after the URL -> not a reply (nothing to say). */
static void test_reply_scan_no_commentary(void) {
  KernReplyTarget t; int clen = 0;
  CHECK(kern_reply_scan("look at this\nhttps://x.com/a/status/99\n\n",
                        &t, &clen) == NULL);
}

/* No status URL at all -> not a reply. */
static void test_reply_scan_no_url(void) {
  KernReplyTarget t; int clen = 0;
  CHECK(kern_reply_scan("just a note about x.com and twitter",
                        &t, &clen) == NULL);
}

/* Publishing a note that ends URL-then-commentary opens the overlay in reply
   mode: the preview is ONLY the commentary, and the reply target — id plus
   the quoted-tweet fields for the original-post preview — is stashed. */
static void test_publish_detects_reply(void) {
  tv_begin();
  load("## someone \xE2\x80\x94 today\n\n> quoted post\n\n"
       "https://x.com/someone/status/12345\n\nmy reply text");
  kern_test_set_x_connected(1);
  kern_publish_to_x();
  CHECK_IEQ(tv_test_pub_state(), 1);
  CHECK_SEQ(tv_test_pub_text(), "my reply text");
  CHECK_SEQ(tv_test_pub_reply_id(), "12345");
  CHECK_SEQ(tv_test_pub_quote_author(), "someone");
  CHECK_SEQ(tv_test_pub_quote_text(), "quoted post");
}

/* ---- Quote posts: commentary ABOVE the feed entry (heading + blockquote +
   URL, nothing after the URL) publishes as a quote of that tweet — the text
   posts with the tweet URL appended, which X renders as a quote card. ---- */

/* kern_quote_scan: plain text above the entry structure is the quote
   commentary; the entry itself fills the target (id/handle/author/quote). */
static void test_quote_scan_extracts_commentary_above(void) {
  KernReplyTarget t; int clen = 0;
  const char *c = kern_quote_scan(
      "This bears repeating,\nin case you haven't seen it:\n\n"
      "## karpathy \xE2\x80\x94 2026-03-07 at 09:00\n\n"
      "> I packaged up the autoresearch project.\n\n"
      "https://x.com/karpathy/status/888\n\n",
      &t, &clen);
  CHECK(c != NULL);
  CHECK_IEQ(strncmp(c, "This bears repeating,", 21), 0);
  CHECK_IEQ(c[clen - 1], ':');                 /* trimmed at the last line */
  CHECK_SEQ(t.id, "888");
  CHECK_SEQ(t.handle, "karpathy");
  CHECK_SEQ(t.author, "karpathy");
  CHECK_SEQ(t.quote, "I packaged up the autoresearch project.");
}

/* Commentary below the URL means REPLY — quote scan must not also match. */
static void test_quote_scan_rejects_text_below_url(void) {
  KernReplyTarget t; int clen = 0;
  CHECK(kern_quote_scan("my take\n\nhttps://x.com/a/status/1\n\nreply text",
                        &t, &clen) == NULL);
}

/* A multi-entry feed note (another status URL or blockquote above the last
   entry) is ambiguous — not a quote. */
static void test_quote_scan_rejects_multi_entry_notes(void) {
  KernReplyTarget t; int clen = 0;
  CHECK(kern_quote_scan(
      "https://x.com/first/status/1\n\n"
      "## second \xE2\x80\x94 today\n\n> post\n\nhttps://x.com/second/status/2\n",
      &t, &clen) == NULL);
  CHECK(kern_quote_scan(
      "> stray quoted line\n\nhttps://x.com/a/status/1\n",
      &t, &clen) == NULL);
}

/* Nothing above the entry -> nothing to say -> not a quote. */
static void test_quote_scan_nothing_above(void) {
  KernReplyTarget t; int clen = 0;
  CHECK(kern_quote_scan(
      "## a \xE2\x80\x94 today\n\n> post\n\nhttps://x.com/a/status/1\n",
      &t, &clen) == NULL);
}

/* Publishing a quote-shaped note opens the overlay in quote mode: preview is
   only the commentary, the target is stashed, and the tweet fetch fires. */
static void test_publish_detects_quote(void) {
  tv_begin();
  load("worth a read\n\n## someone \xE2\x80\x94 today\n\n> original post\n\n"
       "https://x.com/someone/status/321\n");
  kern_test_set_x_connected(1);
  kern_publish_to_x();
  CHECK_IEQ(tv_test_pub_state(), 1);
  CHECK_IEQ(tv_test_pub_kind(), 2);            /* quote */
  CHECK_SEQ(tv_test_pub_text(), "worth a read");
  CHECK_SEQ(tv_test_pub_reply_id(), "321");
  CHECK_SEQ(tv_test_pub_quote_text(), "original post");
  CHECK_SEQ(kern_test_x_tweet_fetch_id(), "321");
}

/* Confirming a quote posts the commentary with the tweet URL appended (X
   renders a trailing permalink as the quote card) — as a PLAIN post, no
   reply id (quote_tweet_id is Enterprise-gated on the API). */
static void test_quote_confirm_appends_url(void) {
  tv_begin();
  load("worth a read\n\n## someone \xE2\x80\x94 today\n\n> original post\n\n"
       "https://x.com/someone/status/321\n");
  kern_test_set_x_connected(1);
  kern_publish_to_x();
  key(0, SDLK_RETURN);
  CHECK_SEQ(kern_test_x_last_publish(),
            "worth a read\n\nhttps://x.com/someone/status/321");
  CHECK(kern_test_x_last_reply_id() == NULL);
}

/* Text both above AND below the URL: the reply wins (writing under the URL is
   the deliberate act; the text above stays out of the post). */
static void test_reply_wins_over_quote(void) {
  tv_begin();
  load("note to self above\n\n## someone \xE2\x80\x94 today\n\n> post\n\n"
       "https://x.com/someone/status/77\n\nthe actual reply");
  kern_test_set_x_connected(1);
  kern_publish_to_x();
  CHECK_IEQ(tv_test_pub_kind(), 1);            /* reply */
  CHECK_SEQ(tv_test_pub_text(), "the actual reply");
}

/* Opening a reply overlay also kicks off an async API fetch of the target
   tweet (GET /2/tweets/:id) — the note-parsed preview shows instantly, the
   authoritative content replaces it when the fetch lands. */
static void test_publish_reply_requests_tweet_fetch(void) {
  tv_begin();
  load("https://x.com/someone/status/424242\n\nmy take");
  kern_test_set_x_connected(1);
  kern_publish_to_x();
  CHECK(kern_test_x_tweet_fetch_id() != NULL);
  CHECK_SEQ(kern_test_x_tweet_fetch_id(), "424242");
}

/* A successful tweet fetch (applied on the main-thread tick) replaces the
   note-parsed preview fields with the API truth — author, handle, date, and
   the full text. */
static void test_tweet_result_updates_preview(void) {
  tv_begin();
  load("## someone \xE2\x80\x94 today\n\n> parsed quote\n\n"
       "https://x.com/someone/status/12345\n\nmy reply");
  kern_test_set_x_connected(1);
  kern_publish_to_x();
  CHECK_SEQ(tv_test_pub_quote_text(), "parsed quote");   /* instant fallback */
  kern_x_tweet_done(1, "12345", "Real Name", "realhandle", "Jul 6",
                    "the real full text from the API");
  editor_tick();
  CHECK_SEQ(tv_test_pub_quote_author(), "Real Name");
  CHECK_SEQ(tv_test_pub_quote_text(), "the real full text from the API");
  CHECK_SEQ(tv_test_pub_reply_handle(), "realhandle");
  CHECK_SEQ(tv_test_pub_reply_id(), "12345");             /* target unchanged */
}

/* A failed fetch keeps the note-parsed preview (it's still a fine preview —
   e.g. accounts on the write-only API tier can't read tweets). */
static void test_tweet_result_failure_keeps_parsed(void) {
  tv_begin();
  load("## someone \xE2\x80\x94 today\n\n> parsed quote\n\n"
       "https://x.com/someone/status/12345\n\nmy reply");
  kern_test_set_x_connected(1);
  kern_publish_to_x();
  kern_x_tweet_done(0, "12345", "", "", "", "no read access");
  editor_tick();
  CHECK_SEQ(tv_test_pub_quote_author(), "someone");
  CHECK_SEQ(tv_test_pub_quote_text(), "parsed quote");
}

/* A fetch result for a DIFFERENT id than the pending reply target is stale
   (a re-publish raced it) — ignored. */
static void test_tweet_result_stale_id_ignored(void) {
  tv_begin();
  load("## someone \xE2\x80\x94 today\n\n> parsed quote\n\n"
       "https://x.com/someone/status/12345\n\nmy reply");
  kern_test_set_x_connected(1);
  kern_publish_to_x();
  kern_x_tweet_done(1, "99999", "Wrong", "wrong", "Jan 1", "wrong tweet");
  editor_tick();
  CHECK_SEQ(tv_test_pub_quote_author(), "someone");
  CHECK_SEQ(tv_test_pub_quote_text(), "parsed quote");
}

/* A result arriving after the overlay was cancelled is dropped. */
static void test_tweet_result_after_cancel_ignored(void) {
  tv_begin();
  load("https://x.com/someone/status/12345\n\nmy reply");
  kern_test_set_x_connected(1);
  kern_publish_to_x();
  key(0, SDLK_ESCAPE);
  kern_x_tweet_done(1, "12345", "Late", "late", "Jul 6", "late text");
  editor_tick();
  CHECK_IEQ(tv_test_pub_state(), 0);
  CHECK_SEQ(tv_test_pub_quote_author(), "");   /* not applied */
}

/* Confirming a reply hands both the commentary and the tweet id to the Swift
   publisher. */
static void test_overlay_confirm_posts_reply(void) {
  tv_begin();
  load("https://x.com/someone/status/777\n\nwell said");
  kern_test_set_x_connected(1);
  kern_publish_to_x();
  key(0, SDLK_RETURN);
  CHECK_IEQ(tv_test_pub_state(), 2);
  CHECK_SEQ(kern_test_x_last_publish(), "well said");
  CHECK(kern_test_x_last_reply_id() != NULL);
  CHECK_SEQ(kern_test_x_last_reply_id(), "777");
}

/* A note with no status URL publishes exactly as before: whole note, no reply
   id anywhere. */
static void test_publish_without_url_is_normal_post(void) {
  tv_begin();
  load("plain note");
  kern_test_set_x_connected(1);
  kern_publish_to_x();
  CHECK_SEQ(tv_test_pub_text(), "plain note");
  CHECK_SEQ(tv_test_pub_reply_id(), "");
  key(0, SDLK_RETURN);
  CHECK_SEQ(kern_test_x_last_publish(), "plain note");
  CHECK(kern_test_x_last_reply_id() == NULL);   /* posted as a plain tweet */
  CHECK(kern_test_x_tweet_fetch_id() == NULL);  /* and no tweet fetch either */
}

/* ---- X news feed (C-x n -> async fetch -> time-stamped News note) ---- */

/* With no account linked, C-x n reports and never asks Swift for the feed. */
static void test_cx_n_not_connected(void) {
  tv_begin(); load("hi");
  kern_test_set_x_connected(0);
  key(KMOD_CTRL, SDLK_x);
  key(0, SDLK_n);
  CHECK_IEQ(kern_test_x_feed_requested(), 0);
  CHECK(strstr(VS->status_msg, "Connect") != NULL);
}

/* Connected, C-x n kicks off the async fetch and says so. */
static void test_cx_n_requests_feed(void) {
  tv_begin(); load("hi");
  kern_test_set_x_connected(1);
  key(KMOD_CTRL, SDLK_x);
  key(0, SDLK_n);
  CHECK_IEQ(VS->ctrl_x_prefix, 0);                /* prefix consumed */
  CHECK_IEQ(kern_test_x_feed_requested(), 1);
  CHECK(strstr(VS->status_msg, "Fetching") != NULL);
}

/* A successful fetch writes a time-stamped Twitter-Home-Newsfeed-… note to the
   documents dir and opens it: a title heading on top, the feed entries below. */
static void test_feed_result_opens_news_note(void) {
  tv_begin();
  char docs[256]; fresh_docs_dir(docs, sizeof docs);
  load("hi");
  kern_test_set_x_connected(1);
  key(KMOD_CTRL, SDLK_x);
  key(0, SDLK_n);
  kern_x_feed_done(1,
    "## Frank Smith \xE2\x80\x94 2026-07-06 \xE2\x80\x94 Hello world this is a post\n\n"
    "Hello world this is a post\n\n"
    "Frank Smith (@frank) \xE2\x80\x94 2026-07-06 14:32\n"
    "https://x.com/frank/status/42\n");
  editor_tick();                                   /* main thread lands the feed */
  CHECK(strstr(ED->filepath, "Twitter-Home-Newsfeed-") != NULL);
  CHECK(strncmp(ED->lines[0].text, "# Twitter Home Newsfeed", 23) == 0);
  int saw_entry = 0, saw_url = 0;
  for (int i = 0; i < ED->line_count; i++) {
    if (strstr(ED->lines[i].text, "(@frank)")) saw_entry = 1;
    if (strstr(ED->lines[i].text, "https://x.com/frank/status/42")) saw_url = 1;
  }
  CHECK(saw_entry);
  CHECK(saw_url);
  FILE *f = fopen(ED->filepath, "rb");             /* saved to disk, not scratch */
  CHECK(f != NULL);
  if (f) fclose(f);
  buf_set_documents_dir("");
}

/* The feed filter drops posts that are only link(s) — e.g. an image post whose
   whole text is its t.co media URL. */
static void test_feed_skip_link_only_posts(void) {
  tv_begin();
  CHECK_IEQ(kern_feed_skip_post("https://t.co/AbC123"), 1);
  CHECK_IEQ(kern_feed_skip_post("https://t.co/a https://t.co/b"), 1);   /* two images */
  CHECK_IEQ(kern_feed_skip_post("  https://t.co/AbC123  "), 1);        /* padded */
  CHECK_IEQ(kern_feed_skip_post("http://t.co/x\nhttps://t.co/y"), 1);  /* links on two lines */
  CHECK_IEQ(kern_feed_skip_post(""), 1);
  CHECK_IEQ(kern_feed_skip_post(NULL), 1);
}

/* …and posts that are just a single line (with or without a trailing media
   link), while keeping multi-line posts even when they contain links. */
static void test_feed_skip_single_line_posts(void) {
  tv_begin();
  CHECK_IEQ(kern_feed_skip_post("gm everybody"), 1);
  CHECK_IEQ(kern_feed_skip_post("look at this https://t.co/xyz"), 1);  /* one-liner + image */
  CHECK_IEQ(kern_feed_skip_post("just one line\n"), 1);                /* trailing \n is not a 2nd line */
  CHECK_IEQ(kern_feed_skip_post("line one\nline two"), 0);             /* real content: keep */
  CHECK_IEQ(kern_feed_skip_post("a thread:\nhttps://t.co/link\nmore text"), 0);
}

/* Post text is rendered as a markdown blockquote: every line gets a "> "
   prefix (bare ">" on empty lines so the quote reads as one block); a trailing
   newline doesn't grow a dangling ">". */
static void test_feed_quote_text(void) {
  tv_begin();
  char out[256];
  kern_feed_quote_text("one line", out, sizeof out);
  CHECK_SEQ(out, "> one line");
  kern_feed_quote_text("a\nb", out, sizeof out);
  CHECK_SEQ(out, "> a\n> b");
  kern_feed_quote_text("a\n\nb", out, sizeof out);          /* empty line stays quoted */
  CHECK_SEQ(out, "> a\n>\n> b");
  kern_feed_quote_text("a\n", out, sizeof out);             /* no dangling ">" */
  CHECK_SEQ(out, "> a\n");
  kern_feed_quote_text("", out, sizeof out);
  CHECK_SEQ(out, "");
  kern_feed_quote_text(NULL, out, sizeof out);
  CHECK_SEQ(out, "");
  char tiny[6];                                             /* truncation is NUL-safe */
  kern_feed_quote_text("abcdef", tiny, sizeof tiny);
  CHECK_SEQ(tiny, "> abc");
}

/* ---- menu-bar bridges (File/Edit/Format/View/Go/Notes -> kern_menu_*) ----
   Each bridge drives the exact same code path as its keyboard chord (synthetic
   SDL events / the same cmd_* functions), so menu and keyboard can't drift. */

static void test_menu_file_actions(void) {
  tv_begin(); load("hello");
  kern_menu_save();                            /* scratch buffer -> asks for a name */
  CHECK_IEQ(VS->minibuf_active, 1);
  key(0, SDLK_ESCAPE);                         /* dismiss */
  kern_menu_switch_buffer();                   /* opens the buffer-switch prompt */
  CHECK_IEQ(VS->minibuf_active, 1);
  key(0, SDLK_ESCAPE);
}

static void test_menu_edit_actions(void) {
  tv_begin(); load("hello world");
  put_cursor(0, 0);
  kern_menu_kill_line();                       /* C-k path */
  EXPECT_LINE(0, "");
  kern_menu_undo();                            /* C-/ path */
  EXPECT_LINE(0, "hello world");
  kern_menu_select_all();                      /* C-x h path */
  CHECK_IEQ(ED->mark_active, 1);
}

static void test_menu_format_wraps_region(void) {
  tv_begin(); load("pick me");
  put_cursor(0, 0);
  ED->mark_active = 1; ED->mark_line = 0; ED->mark_col = 4;   /* "pick" */
  kern_menu_bold();
  EXPECT_LINE(0, "**pick** me");
  tv_begin(); load("pick me");
  put_cursor(0, 0);
  ED->mark_active = 1; ED->mark_line = 0; ED->mark_col = 4;
  kern_menu_highlight();
  EXPECT_LINE(0, "==pick== me");
  /* no selection: report, don't touch the buffer */
  tv_begin(); load("plain");
  kern_menu_bold();
  EXPECT_LINE(0, "plain");
  CHECK(strstr(VS->status_msg, "Select") != NULL);
}

static void test_menu_view_toggles_and_queries(void) {
  tv_begin(); load("hi");
  CHECK_IEQ(kern_typewriter_enabled(), 0);
  kern_menu_typewriter();
  CHECK_IEQ(VS->typewriter_mode, 1);
  CHECK_IEQ(kern_typewriter_enabled(), 1);
  CHECK_IEQ(kern_page_borders_enabled(), 1);   /* borders shown by default */
  kern_menu_page_borders();
  CHECK_IEQ(VS->page_furniture_hidden, 1);
  CHECK_IEQ(kern_page_borders_enabled(), 0);
  float before = VS->font_size;
  kern_menu_font_bigger();
  CHECK(VS->font_size > before);
  /* Graph View drives the C-x g chord and reports the overlay state */
  CHECK_IEQ(kern_graph_enabled(), 0);
  kern_menu_graph_view();
  CHECK_IEQ(kern_graph_enabled(), 1);
  CHECK_IEQ(tv_test_graph_active(), 1);
  kern_menu_graph_view();
  CHECK_IEQ(kern_graph_enabled(), 0);
}

static void test_menu_go_actions(void) {
  tv_begin(); load("one\ntwo\nthree");
  kern_menu_bottom();
  CHECK_IEQ(ED->cursor_line, 2);
  kern_menu_goto_line();                       /* M-g path -> minibuffer */
  CHECK_IEQ(VS->minibuf_active, 1);
  key(0, SDLK_ESCAPE);
  kern_menu_search_fwd();                      /* C-s path -> isearch */
  CHECK_IEQ(VS->search_active, 1);
  key(0, SDLK_ESCAPE);
}

static void test_menu_notes_actions(void) {
  tv_begin(); load("hi");
  kern_menu_extract_note();                    /* no region -> report */
  CHECK(strstr(VS->status_msg, "region") != NULL);
  kern_test_set_x_connected(1);
  kern_menu_fetch_news();
  CHECK_IEQ(kern_test_x_feed_requested(), 1);
}

/* A menu-driven C-x chord must NOT arm suppress_next_text — there is no
   trailing SDL_TEXTINPUT after a mouse click, so a stale suppression would
   silently eat the user's next typed character. */
static void test_menu_chord_does_not_eat_next_char(void) {
  tv_begin(); load("");
  kern_menu_typewriter();                      /* routes through the C-x t chord */
  type("a");
  EXPECT_LINE(0, "a");                         /* the keystroke survived */
}

/* editor_tick re-syncs the View-menu checkmark delegate every pass — SwiftUI
   rebuilds the main menu on its own schedule, so a once-at-launch install can
   land on a menu that doesn't exist yet (or gets replaced), leaving the
   toggles without checkmarks until relaunch. */
static void test_tick_syncs_view_menu(void) {
  tv_begin(); load("hi");
  int before = kern_test_view_menu_syncs();
  editor_tick();
  CHECK_IEQ(kern_test_view_menu_syncs(), before + 1);
}

/* ---- X bookmarks (C-x m -> paginated fetch -> Twitter-Bookmarks note) ---- */

/* With no account linked, C-x m reports and never asks Swift for bookmarks. */
static void test_cx_m_not_connected(void) {
  tv_begin(); load("hi");
  kern_test_set_x_connected(0);
  key(KMOD_CTRL, SDLK_x);
  key(0, SDLK_m);
  CHECK_IEQ(kern_test_x_bookmarks_requested(), 0);
  CHECK(strstr(VS->status_msg, "Connect") != NULL);
}

/* Connected, C-x m kicks off the async bookmarks fetch. */
static void test_cx_m_requests_bookmarks(void) {
  tv_begin(); load("hi");
  kern_test_set_x_connected(1);
  key(KMOD_CTRL, SDLK_x);
  key(0, SDLK_m);
  CHECK_IEQ(VS->ctrl_x_prefix, 0);
  CHECK_IEQ(kern_test_x_bookmarks_requested(), 1);
  CHECK(strstr(VS->status_msg, "bookmarks") != NULL);
}

/* A successful bookmarks fetch lands in its own Twitter-Bookmarks-… note with
   a matching title heading — same result plumbing as the news feed, different
   name so the two kinds of notes don't collide. */
static void test_bookmarks_result_opens_note(void) {
  tv_begin();
  char docs[256]; fresh_docs_dir(docs, sizeof docs);
  load("hi");
  kern_test_set_x_connected(1);
  key(KMOD_CTRL, SDLK_x);
  key(0, SDLK_m);
  kern_x_bookmarks_done(1,
    "## Frank Smith \xE2\x80\x94 2026-07-06 at 14:32\n\n"
    "> a bookmarked gem\n> second line\n\n"
    "Frank Smith (@frank) \xE2\x80\x94 2026-07-06 14:32\n"
    "https://x.com/frank/status/42\n");
  editor_tick();
  CHECK(strstr(ED->filepath, "Twitter-Bookmarks-") != NULL);
  CHECK(strncmp(ED->lines[0].text, "# Twitter Bookmarks", 19) == 0);
  int saw = 0;
  for (int i = 0; i < ED->line_count; i++)
    if (strstr(ED->lines[i].text, "a bookmarked gem")) saw = 1;
  CHECK(saw);
  buf_set_documents_dir("");
}

/* A failed fetch reports the error and leaves the current buffer alone. */
static void test_feed_failure_reports_error(void) {
  tv_begin(); load("keep me");
  kern_test_set_x_connected(1);
  key(KMOD_CTRL, SDLK_x);
  key(0, SDLK_n);
  kern_x_feed_done(0, "X: rate limit exceeded");
  editor_tick();
  CHECK(strstr(VS->status_msg, "rate limit") != NULL);
  EXPECT_LINE(0, "keep me");
}

/* The preview renders the account's display name and @handle. */
static void test_overlay_renders_identity(void) {
  tv_begin();
  load("hi");
  kern_test_set_x_connected(1);
  kern_test_set_x_identity("Alexis Rondeau", "SpringStreetNYC");
  kern_publish_to_x();
  stub_reset();
  editor_tick();                                 /* draws the overlay */
  /* the stub captures only the first 7 bytes of each draw call, so match on
     truncation-safe prefixes ("Alexis ", "@Spring"). */
  int saw_name = 0, saw_handle = 0;
  for (int i = 0; i < stub_text_count; i++) {
    if (strstr(stub_texts[i].ch, "Alexis")) saw_name = 1;
    if (strstr(stub_texts[i].ch, "Spring")) saw_handle = 1;
  }
  CHECK(saw_name);
  CHECK(saw_handle);
}

/* Cmd-Shift-H routes to the sentence-highlight toggle and edits the buffer. */
static void test_cmd_shift_h_toggles_sentence_highlight(void) {
  tv_begin();
  load("The cat sat. The dog ran.");
  put_cursor(0, 4);                            /* inside the first sentence */
  key(KMOD_GUI | KMOD_SHIFT, SDLK_h);
  EXPECT_LINE(0, "==The cat sat.== The dog ran.");
  key(KMOD_GUI | KMOD_SHIFT, SDLK_h);          /* toggles back off */
  EXPECT_LINE(0, "The cat sat. The dog ran.");
}

/* Cmd-Shift-U routes to the sentence-underline toggle. */
static void test_cmd_shift_u_toggles_sentence_underline(void) {
  tv_begin();
  load("Just do it.");
  put_cursor(0, 2);
  key(KMOD_GUI | KMOD_SHIFT, SDLK_u);
  EXPECT_LINE(0, "++Just do it.++");
  key(KMOD_GUI | KMOD_SHIFT, SDLK_u);
  EXPECT_LINE(0, "Just do it.");
}

/* --------------------------------------------------------------------------- graph view (C-x g) */

/* A left mouse click (down + up) at window coordinates (x, y). */
static void click(int x, int y) {
  SDL_Event e; memset(&e, 0, sizeof e);
  e.type = SDL_MOUSEBUTTONDOWN;
  e.button.button = SDL_BUTTON_LEFT;
  e.button.x = x; e.button.y = y;
  editor_handle_event(&e);
  e.type = SDL_MOUSEBUTTONUP;
  editor_handle_event(&e);
}

/* C-x g opens the graph overlay over the current note: the open file and its
   [[wikilink]] targets become nodes joined by LINK edges; typing is swallowed
   while the overlay is up; Esc closes it. */
static void test_cx_g_opens_graph_view(void) {
  char dir[256]; fresh_docs_dir(dir, sizeof dir);
  char a[512]; snprintf(a, sizeof a, "%s/Alpha.md", dir);
  const char *body = "see [[Beta]]\n";
  buf_save_text(a, body, (int)strlen(body));
  tv_begin(); load("see [[Beta]]");
  snprintf(ED->filepath, sizeof ED->filepath, "%s", a);
  ED->filename = "Alpha.md";

  key(KMOD_CTRL, SDLK_x);
  key(0, SDLK_g);
  CHECK_IEQ(tv_test_graph_active(), 1);
  CHECK_IEQ(VS->ctrl_x_prefix, 0);

  int ia = graph_find("Alpha"), ib = graph_find("Beta");
  CHECK(ia >= 0);
  CHECK(ib >= 0);
  CHECK((graph_edge_kinds_between(ia, ib) & GRAPH_EDGE_LINK) != 0);

  /* the overlay is modal: typed text never reaches the buffer (clear the
     chord's own text suppression first so the swallow tested is the graph's) */
  VS->suppress_next_text = 0;
  type("x");
  EXPECT_LINE(0, "see [[Beta]]");

  key(0, SDLK_ESCAPE);
  CHECK_IEQ(tv_test_graph_active(), 0);
  buf_set_documents_dir("");
}

/* A second C-x g closes the overlay (toggle). */
static void test_cx_g_toggles_closed(void) {
  char dir[256]; fresh_docs_dir(dir, sizeof dir);
  tv_begin(); load("hi");
  key(KMOD_CTRL, SDLK_x); key(0, SDLK_g);
  CHECK_IEQ(tv_test_graph_active(), 1);
  key(KMOD_CTRL, SDLK_x); key(0, SDLK_g);
  CHECK_IEQ(tv_test_graph_active(), 0);
  buf_set_documents_dir("");
}

/* Clicking a node opens that note (the real on-disk filename when the node
   came from a file) and closes the overlay. */
static void test_graph_click_opens_note(void) {
  char dir[256]; fresh_docs_dir(dir, sizeof dir);
  char a[512]; snprintf(a, sizeof a, "%s/Alpha.md", dir);
  char b[512]; snprintf(b, sizeof b, "%s/Beta.md", dir);
  const char *body = "see [[Beta]]\n";
  buf_save_text(a, body, (int)strlen(body));
  buf_save_text(b, "beta\n", 5);
  tv_begin(); load("see [[Beta]]");
  snprintf(ED->filepath, sizeof ED->filepath, "%s", a);
  ED->filename = "Alpha.md";

  key(KMOD_CTRL, SDLK_x); key(0, SDLK_g);
  CHECK_IEQ(tv_test_graph_active(), 1);
  int x = 0, y = 0;
  CHECK(tv_test_graph_node_screen("Beta", &x, &y));
  click(x, y);
  CHECK_IEQ(tv_test_graph_active(), 0);
  CHECK(strstr(ED->filepath, "Beta.md") != NULL);
  buf_set_documents_dir("");
}

/* Dragging the background pans the whole graph — every node's screen position
   shifts by the mouse delta — without opening a note or closing the overlay,
   and the pan sticks (it takes over the view, so the auto-fit doesn't snap it
   back on the next tick). */
static void test_graph_pan_drags_view(void) {
  char dir[256]; fresh_docs_dir(dir, sizeof dir);
  char a[512]; snprintf(a, sizeof a, "%s/Alpha.md", dir);
  char b[512]; snprintf(b, sizeof b, "%s/Beta.md", dir);
  const char *body = "see [[Beta]]\n";
  buf_save_text(a, body, (int)strlen(body));
  buf_save_text(b, "beta\n", 5);
  tv_begin(); load("see [[Beta]]");
  snprintf(ED->filepath, sizeof ED->filepath, "%s", a);
  ED->filename = "Alpha.md";

  key(KMOD_CTRL, SDLK_x); key(0, SDLK_g);
  CHECK_IEQ(tv_test_graph_active(), 1);
  int bx = 0, by = 0;
  CHECK(tv_test_graph_node_screen("Beta", &bx, &by));

  /* press empty background (the fit keeps all nodes >=70px from the window
     edge, so the corner is guaranteed node-free), drag by (+50, +30) */
  SDL_Event e; memset(&e, 0, sizeof e);
  e.type = SDL_MOUSEBUTTONDOWN;
  e.button.button = SDL_BUTTON_LEFT;
  e.button.x = 10; e.button.y = 10;
  editor_handle_event(&e);
  memset(&e, 0, sizeof e);
  e.type = SDL_MOUSEMOTION; e.motion.x = 60; e.motion.y = 40;
  editor_handle_event(&e);
  memset(&e, 0, sizeof e);
  e.type = SDL_MOUSEBUTTONUP;
  e.button.button = SDL_BUTTON_LEFT;
  e.button.x = 60; e.button.y = 40;
  editor_handle_event(&e);

  int px = 0, py = 0;
  CHECK(tv_test_graph_node_screen("Beta", &px, &py));
  CHECK(px >= bx + 49 && px <= bx + 51);       /* shifted by the drag (±rounding) */
  CHECK(py >= by + 29 && py <= by + 31);
  CHECK_IEQ(tv_test_graph_active(), 1);        /* still open — a pan is not a click */
  CHECK(strstr(ED->filepath, "Alpha.md") != NULL);

  editor_tick();                               /* auto-fit must not undo the pan */
  CHECK(tv_test_graph_node_screen("Beta", &px, &py));
  CHECK(px >= bx + 49 && px <= bx + 51);
  CHECK(py >= by + 29 && py <= by + 31);

  key(0, SDLK_ESCAPE);
  buf_set_documents_dir("");
}

/* Notes created the same day get a DAY edge (both fixtures are written now,
   so they share a creation day by construction). */
static void test_graph_same_day_edge(void) {
  char dir[256]; fresh_docs_dir(dir, sizeof dir);
  char a[512]; snprintf(a, sizeof a, "%s/One.md", dir);
  char b[512]; snprintf(b, sizeof b, "%s/Two.md", dir);
  buf_save_text(a, "one\n", 4);
  buf_save_text(b, "two\n", 4);
  tv_begin(); load("one");
  snprintf(ED->filepath, sizeof ED->filepath, "%s", a);
  ED->filename = "One.md";

  key(KMOD_CTRL, SDLK_x); key(0, SDLK_g);
  int ia = graph_find("One"), ib = graph_find("Two");
  CHECK(ia >= 0);
  CHECK(ib >= 0);
  CHECK((graph_edge_kinds_between(ia, ib) & GRAPH_EDGE_DAY) != 0);
  key(0, SDLK_ESCAPE);
  buf_set_documents_dir("");
}

/* The opened-after history becomes OPENED edges. */
static void test_graph_opened_after_edge(void) {
  char dir[256]; fresh_docs_dir(dir, sizeof dir);
  char a[512]; snprintf(a, sizeof a, "%s/First.md", dir);
  buf_save_text(a, "first\n", 6);
  tv_begin(); load("first");
  snprintf(ED->filepath, sizeof ED->filepath, "%s", a);
  ED->filename = "First.md";
  /* open Second.md while First.md is up → Second remembers First */
  key(KMOD_CTRL, SDLK_x); key(KMOD_CTRL, SDLK_f);
  type("Second.md"); key(0, SDLK_RETURN);
  CHECK(strstr(ED->filepath, "Second.md") != NULL);

  key(KMOD_CTRL, SDLK_x); key(0, SDLK_g);
  int ia = graph_find("First"), ib = graph_find("Second");
  CHECK(ia >= 0);
  CHECK(ib >= 0);
  CHECK((graph_edge_kinds_between(ia, ib) & GRAPH_EDGE_OPENED) != 0);
  key(0, SDLK_ESCAPE);
  buf_set_documents_dir("");
}

/* Did any captured text draw contain `s` (page text, labels, chrome alike)? */
static int stub_has_text(const char *s) {
  for (int i = 0; i < stub_text_count; i++)
    if (strstr(stub_texts[i].ch, s)) return 1;
  return 0;
}

/* A wide star (one hub note wikilinking 29 ghost targets) settles larger than
   the 800×600 stub window at 1:1 — the overlay must open fitted so every node
   is on screen (Obsidian-style), not cropped. */
static void test_graph_opens_fitted(void) {
  char dir[256]; fresh_docs_dir(dir, sizeof dir);
  char hub[512]; snprintf(hub, sizeof hub, "%s/Hub.md", dir);
  char body[1024]; int blen = 0;
  for (int i = 1; i < 30; i++)
    blen += snprintf(body + blen, sizeof body - blen, "[[G%d]]\n", i);
  buf_save_text(hub, body, blen);
  char plain[512]; snprintf(plain, sizeof plain, "%s/Plain.md", dir);
  buf_save_text(plain, "hi\n", 3);
  tv_begin(); load("hi");
  snprintf(ED->filepath, sizeof ED->filepath, "%s", plain);
  ED->filename = "Plain.md";

  key(KMOD_CTRL, SDLK_x); key(0, SDLK_g);
  CHECK_IEQ(tv_test_graph_active(), 1);
  CHECK(graph_node_count() >= 30);
  for (int i = 0; i < graph_node_count(); i++) {
    int x = 0, y = 0;
    CHECK(tv_test_graph_node_screen(graph_node(i)->name, &x, &y));
    CHECK(x >= 0 && x <= 800);
    CHECK(y >= 0 && y <= 600);
  }
  key(0, SDLK_ESCAPE);
  buf_set_documents_dir("");
}

/* Node labels are hidden while zoomed out (below the fade threshold) and fade
   in past it — except the hovered node's label, which always shows. */
static void test_graph_labels_fade_with_zoom(void) {
  char dir[256]; fresh_docs_dir(dir, sizeof dir);
  char hub[512]; snprintf(hub, sizeof hub, "%s/Hub.md", dir);
  char body[1024]; int blen = 0;
  for (int i = 1; i < 30; i++)
    blen += snprintf(body + blen, sizeof body - blen, "[[G%d]]\n", i);
  buf_save_text(hub, body, blen);
  char plain[512]; snprintf(plain, sizeof plain, "%s/Plain.md", dir);
  buf_save_text(plain, "hi\n", 3);
  tv_begin(); load("hi");   /* page text has no "G7", so any hit is a label */
  snprintf(ED->filepath, sizeof ED->filepath, "%s", plain);
  ED->filename = "Plain.md";

  key(KMOD_CTRL, SDLK_x); key(0, SDLK_g);
  CHECK_IEQ(tv_test_graph_active(), 1);

  /* zoom well out, below the label threshold → no labels drawn */
  SDL_Event w; memset(&w, 0, sizeof w);
  w.type = SDL_MOUSEWHEEL; w.wheel.y = -1;
  for (int i = 0; i < 20; i++) editor_handle_event(&w);   /* scale floor 0.2 */
  stub_reset();
  editor_tick();
  CHECK_IEQ(stub_has_text("G7"), 0);

  /* hovering a node shows its label even while zoomed out */
  int hx = 0, hy = 0;
  CHECK(tv_test_graph_node_screen("G3", &hx, &hy));
  SDL_Event m; memset(&m, 0, sizeof m);
  m.type = SDL_MOUSEMOTION; m.motion.x = hx; m.motion.y = hy;
  editor_handle_event(&m);
  stub_reset();
  editor_tick();
  CHECK_IEQ(stub_has_text("G3"), 1);
  CHECK_IEQ(stub_has_text("G7"), 0);

  /* zoom in past the threshold → the other labels fade in */
  m.motion.x = 5; m.motion.y = 5;          /* un-hover first */
  editor_handle_event(&m);
  w.wheel.y = 1;
  for (int i = 0; i < 20; i++) editor_handle_event(&w);   /* 0.2 → ~1.35 */
  stub_reset();
  editor_tick();
  CHECK_IEQ(stub_has_text("G7"), 1);

  key(0, SDLK_ESCAPE);
  buf_set_documents_dir("");
}

/* Any recorded rect stroke with this rgb and an alpha inside [lo, hi]? */
static int stub_has_rect_rgba(int r, int g, int b, int lo, int hi) {
  for (int i = 0; i < stub_rect_count; i++) {
    Color c = stub_rects[i].color;
    if (c.r == r && c.g == g && c.b == b && c.a >= lo && c.a <= hi) return 1;
  }
  return 0;
}

/* Node labels draw ABOVE the node — below the disc they'd sit right under
   the mouse pointer whenever the node is hovered. */
static void test_graph_label_above_node(void) {
  char dir[256]; fresh_docs_dir(dir, sizeof dir);
  char a[512]; snprintf(a, sizeof a, "%s/Alpha.md", dir);
  char b[512]; snprintf(b, sizeof b, "%s/Beta.md", dir);
  buf_save_text(a, "see [[Beta]]\n", 13);
  buf_save_text(b, "beta\n", 5);
  tv_begin(); load("see [[Beta]]");
  snprintf(ED->filepath, sizeof ED->filepath, "%s", a);
  ED->filename = "Alpha.md";

  key(KMOD_CTRL, SDLK_x); key(0, SDLK_g);
  CHECK_IEQ(tv_test_graph_active(), 1);
  int nx = 0, ny = 0;
  CHECK(tv_test_graph_node_screen("Beta", &nx, &ny));
  SDL_Event m; memset(&m, 0, sizeof m);
  m.type = SDL_MOUSEMOTION; m.motion.x = nx; m.motion.y = ny;
  editor_handle_event(&m);                   /* hover → label always shows */
  stub_reset();
  editor_tick();
  int found = 0, label_y = 0;
  for (int i = 0; i < stub_text_count; i++)
    if (strncmp(stub_texts[i].ch, "Beta", 4) == 0) { found = 1; label_y = stub_texts[i].y; }
  CHECK_IEQ(found, 1);
  CHECK(label_y < ny);                       /* above the node center */
  key(0, SDLK_ESCAPE);
  buf_set_documents_dir("");
}

/* The overlay draws no usage-hint banner — the map speaks for itself. */
static void test_graph_no_hint_banner(void) {
  char dir[256]; fresh_docs_dir(dir, sizeof dir);
  tv_begin(); load("hi");
  key(KMOD_CTRL, SDLK_x); key(0, SDLK_g);
  CHECK_IEQ(tv_test_graph_active(), 1);
  stub_reset();
  editor_tick();
  CHECK_IEQ(stub_has_text("click to open"), 0);
  CHECK_IEQ(stub_has_text("drag to arrange"), 0);
  key(0, SDLK_ESCAPE);
  buf_set_documents_dir("");
}

/* Clicking a legend entry toggles that edge kind off and on (display filter;
   the entry itself stays clickable and the overlay stays open). */
static void test_graph_legend_toggles_edge_kind(void) {
  char dir[256]; fresh_docs_dir(dir, sizeof dir);
  char a[512]; snprintf(a, sizeof a, "%s/Alpha.md", dir);
  char b[512]; snprintf(b, sizeof b, "%s/Beta.md", dir);
  buf_save_text(a, "see [[Beta]]\n", 13);
  buf_save_text(b, "beta\n", 5);
  tv_begin(); load("see [[Beta]]");
  snprintf(ED->filepath, sizeof ED->filepath, "%s", a);
  ED->filename = "Alpha.md";

  key(KMOD_CTRL, SDLK_x); key(0, SDLK_g);
  CHECK_IEQ(tv_test_graph_active(), 1);
  SDL_Event w; memset(&w, 0, sizeof w);
  w.type = SDL_MOUSEWHEEL; w.wheel.y = 1;
  for (int i = 0; i < 10; i++) editor_handle_event(&w);   /* edges full alpha */
  stub_reset();
  editor_tick();
  CHECK_IEQ(stub_has_rect_rgba(130, 140, 152, 110, 110), 1);   /* LINK drawn */

  int lx = 0, ly = 0, lw = 0, lh = 0;
  CHECK(tv_test_graph_legend_rect(0, &lx, &ly, &lw, &lh));     /* "linked" */
  click(lx + lw / 2, ly + lh / 2);
  CHECK_IEQ(tv_test_graph_active(), 1);       /* a toggle, not a note-open */
  stub_reset();
  editor_tick();
  CHECK_IEQ(stub_has_rect_rgba(130, 140, 152, 110, 110), 0);   /* hidden */

  click(lx + lw / 2, ly + lh / 2);            /* back on */
  stub_reset();
  editor_tick();
  CHECK_IEQ(stub_has_rect_rgba(130, 140, 152, 110, 110), 1);
  key(0, SDLK_ESCAPE);
  buf_set_documents_dir("");
}

/* Zoomed way out, nodes draw as fine dots — the minimum drawn radius is
   small enough that a dense vault reads as a starfield with gaps, not a
   solid ball of 6px discs. */
static void test_graph_far_zoom_draws_small_dots(void) {
  char dir[256]; fresh_docs_dir(dir, sizeof dir);
  char a[512]; snprintf(a, sizeof a, "%s/Alpha.md", dir);
  char g[512]; snprintf(g, sizeof g, "%s/Gamma.md", dir);
  buf_save_text(a, "see [[Gamma]]\n", 14);
  buf_save_text(g, "gamma\n", 6);
  tv_begin(); load("see [[Gamma]]");
  snprintf(ED->filepath, sizeof ED->filepath, "%s", a);
  ED->filename = "Alpha.md";

  key(KMOD_CTRL, SDLK_x); key(0, SDLK_g);
  CHECK_IEQ(tv_test_graph_active(), 1);
  SDL_Event w; memset(&w, 0, sizeof w);
  w.type = SDL_MOUSEWHEEL; w.wheel.y = -1;
  for (int i = 0; i < 40; i++) editor_handle_event(&w);   /* deep zoom-out */
  stub_reset();
  editor_tick();
  /* every plain-note disc (168,174,184) is drawn tiny */
  int found = 0, oversized = 0;
  for (int i = 0; i < stub_rect_count; i++) {
    Color c = stub_rects[i].color;
    if (c.r == 168 && c.g == 174 && c.b == 184) {
      found = 1;
      if (stub_rects[i].rect.w > 4) oversized = 1;
    }
  }
  CHECK_IEQ(found, 1);
  CHECK_IEQ(oversized, 0);
  key(0, SDLK_ESCAPE);
  buf_set_documents_dir("");
}

/* A ghost node (a wikilink target with no file on disk yet) draws as a gray
   outline ring, not a filled disc — real notes keep the filled gray. */
static void test_graph_ghost_node_outlined(void) {
  char dir[256]; fresh_docs_dir(dir, sizeof dir);
  char a[512]; snprintf(a, sizeof a, "%s/Alpha.md", dir);
  char g[512]; snprintf(g, sizeof g, "%s/Gamma.md", dir);
  const char *body = "see [[Beta]] and [[Gamma]]\n";
  buf_save_text(a, body, (int)strlen(body));
  buf_save_text(g, "gamma\n", 6);          /* Gamma is real; Beta stays a ghost */
  tv_begin(); load("see [[Beta]] and [[Gamma]]");
  snprintf(ED->filepath, sizeof ED->filepath, "%s", a);
  ED->filename = "Alpha.md";

  key(KMOD_CTRL, SDLK_x); key(0, SDLK_g);
  CHECK_IEQ(tv_test_graph_active(), 1);
  CHECK(graph_find("Beta") >= 0);

  stub_reset();
  editor_tick();
  /* the ghost's ring strokes are drawn, the old filled ghost disc is not */
  CHECK_IEQ(stub_has_rect_rgba(128, 133, 143, 150, 255), 1);
  CHECK_IEQ(stub_has_rect_rgba(105, 110, 120, 1, 255), 0);
  /* a real (non-current) note still draws its filled disc */
  CHECK_IEQ(stub_has_rect_rgba(168, 174, 184, 200, 255), 1);

  key(0, SDLK_ESCAPE);
  buf_set_documents_dir("");
}

/* Edges fade with zoom but never disappear: the fitted overview draws them at
   a faint floor alpha, a hovered node's edges show at full highlight, and
   zooming past the threshold brings the rest to full strength. Asserted via
   the muted wikilink stroke color (130,140,152, base alpha 110) the stub
   records. */
static void test_graph_edges_fade_with_zoom(void) {
  char dir[256]; fresh_docs_dir(dir, sizeof dir);
  char hub[512]; snprintf(hub, sizeof hub, "%s/Hub.md", dir);
  char body[1024]; int blen = 0;
  for (int i = 1; i < 30; i++)
    blen += snprintf(body + blen, sizeof body - blen, "[[G%d]]\n", i);
  buf_save_text(hub, body, blen);
  char plain[512]; snprintf(plain, sizeof plain, "%s/Plain.md", dir);
  buf_save_text(plain, "hi\n", 3);
  tv_begin(); load("hi");
  snprintf(ED->filepath, sizeof ED->filepath, "%s", plain);
  ED->filename = "Plain.md";

  key(KMOD_CTRL, SDLK_x); key(0, SDLK_g);
  CHECK_IEQ(tv_test_graph_active(), 1);

  /* zoomed well out: edges present but faint (alpha well under the base 110,
     above zero — the floor), and none at full muted strength */
  SDL_Event zo; memset(&zo, 0, sizeof zo);
  zo.type = SDL_MOUSEWHEEL; zo.wheel.y = -1;
  for (int i = 0; i < 20; i++) editor_handle_event(&zo);  /* scale floor 0.2 */
  stub_reset();
  editor_tick();
  CHECK_IEQ(stub_has_rect_rgba(130, 140, 152, 1, 55), 1);
  CHECK_IEQ(stub_has_rect_rgba(130, 140, 152, 56, 255), 0);

  /* hovering a node lifts its edges to the full hot highlight */
  int hx = 0, hy = 0;
  CHECK(tv_test_graph_node_screen("G3", &hx, &hy));
  SDL_Event m; memset(&m, 0, sizeof m);
  m.type = SDL_MOUSEMOTION; m.motion.x = hx; m.motion.y = hy;
  editor_handle_event(&m);
  stub_reset();
  editor_tick();
  CHECK_IEQ(stub_has_rect_rgba(170, 190, 205, 235, 235), 1);

  /* zoom in past the threshold → the muted edges reach full base alpha */
  m.motion.x = 5; m.motion.y = 5;
  editor_handle_event(&m);
  SDL_Event w; memset(&w, 0, sizeof w);
  w.type = SDL_MOUSEWHEEL; w.wheel.y = 1;
  for (int i = 0; i < 30; i++) editor_handle_event(&w);   /* comfortably ≥ 1.0 */
  stub_reset();
  editor_tick();
  CHECK_IEQ(stub_has_rect_rgba(130, 140, 152, 110, 110), 1);

  key(0, SDLK_ESCAPE);
  buf_set_documents_dir("");
}

/* Node size mirrors the Context section: incoming wikilinks, same-day
   companions, and appearing in another note's opened-after list all count as
   backlinks, and the much-referenced note draws bigger. */
static void test_graph_node_size_reflects_backlinks(void) {
  char dir[256]; fresh_docs_dir(dir, sizeof dir);
  char p[512];
  snprintf(p, sizeof p, "%s/Hub.md", dir);  buf_save_text(p, "hub\n", 4);
  snprintf(p, sizeof p, "%s/A.md", dir);    buf_save_text(p, "see [[Hub]]\n", 12);
  snprintf(p, sizeof p, "%s/B.md", dir);    buf_save_text(p, "see [[Hub]]\n", 12);
  snprintf(p, sizeof p, "%s/C.md", dir);    buf_save_text(p, "see [[Hub]]\n", 12);
  tv_begin(); load("hub");
  char hubp[512]; snprintf(hubp, sizeof hubp, "%s/Hub.md", dir);
  snprintf(ED->filepath, sizeof ED->filepath, "%s", hubp);
  ED->filename = "Hub.md";
  /* open A.md while Hub.md is up → Hub lands in A's opened-after list */
  key(KMOD_CTRL, SDLK_x); key(KMOD_CTRL, SDLK_f);
  type("A.md"); key(0, SDLK_RETURN);

  key(KMOD_CTRL, SDLK_x); key(0, SDLK_g);
  int hub = graph_find("Hub"), b = graph_find("B");
  CHECK(hub >= 0);
  CHECK(b >= 0);
  /* Hub: 3 incoming wikilinks + 3 same-day companions + 1 opened-after
     appearance; B: 3 same-day companions only */
  CHECK_IEQ(graph_node(hub)->backlinks, 7);
  CHECK_IEQ(graph_node(b)->backlinks, 3);
  CHECK(graph_node_radius(hub) > graph_node_radius(b));
  key(0, SDLK_ESCAPE);
  buf_set_documents_dir("");
}

/* --------------------------------------------------------------------------- suite */

void suite_textview(void) {
  /* prefix chords */
  RUN(test_cx_prefix_sets_and_clears);
  RUN(test_cx_y_toggles_syntax);
  RUN(test_cx_s_toggles_style_check);
  RUN(test_cx_p_toggles_page_furniture);
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
  RUN(test_backspace_deletes_marked_region);
  RUN(test_isearch_forward_and_repeat);
  RUN(test_isearch_direction_flip_and_abort);
  /* minibuffer editing */
  RUN(test_find_file_creates_buffer);
  RUN(test_opened_after_records_predecessor);
  RUN(test_opened_after_accumulates_and_dedups);
  RUN(test_minibuffer_tab_completion);
  RUN(test_save_as_writes_file);
  /* wikilink nav */
  RUN(test_follow_wikilink_and_history);
  RUN(test_follow_wikilink_bare_name_resolves_md);
  RUN(test_follow_wikilink_creates_md_note);
  RUN(test_follow_wikilink_alias);
  RUN(test_follow_wikilink_none_at_cursor);
  RUN(test_follow_url_opens_browser);
  RUN(test_follow_url_absent_falls_through_to_wikilink);
  RUN(test_wikilink_dropdown_accept);
  RUN(test_wikilink_dropdown_escape_dismisses);
  /* autosave */
  RUN(test_autosave_fires_after_interval);
  RUN(test_autosave_skips_clean_and_scratch);
  /* margin notes */
  RUN(test_margin_note_creates_footnote);
  RUN(test_margin_note_escape_cancels);
  RUN(test_margin_note_no_room);
  RUN(test_margin_note_normal_mode_with_room);
  RUN(test_cmd_shift_h_toggles_sentence_highlight);
  RUN(test_cmd_shift_u_toggles_sentence_underline);
  /* X publish confirmation overlay */
  RUN(test_publish_opens_overlay);
  RUN(test_publish_not_connected_no_overlay);
  RUN(test_publish_overlay_uses_region);
  RUN(test_overlay_confirm_publishes);
  RUN(test_overlay_cancel_closes);
  RUN(test_publish_success_copies_url_and_closes);
  RUN(test_publish_failure_keeps_overlay);
  RUN(test_publish_failure_logs_error_note);
  RUN(test_error_log_newest_entry_first);
  RUN(test_feed_failure_logs_error_note);
  RUN(test_error_log_noop_without_documents_dir);
  RUN(test_overlay_swallows_typing);
  RUN(test_overlay_renders_identity);
  RUN(test_reply_scan_extracts_target_and_commentary);
  RUN(test_reply_scan_bare_url_no_quote);
  RUN(test_reply_scan_last_url_wins);
  RUN(test_reply_scan_no_commentary);
  RUN(test_reply_scan_no_url);
  RUN(test_publish_detects_reply);
  RUN(test_quote_scan_extracts_commentary_above);
  RUN(test_quote_scan_rejects_text_below_url);
  RUN(test_quote_scan_rejects_multi_entry_notes);
  RUN(test_quote_scan_nothing_above);
  RUN(test_publish_detects_quote);
  RUN(test_quote_confirm_appends_url);
  RUN(test_reply_wins_over_quote);
  RUN(test_publish_reply_requests_tweet_fetch);
  RUN(test_tweet_result_updates_preview);
  RUN(test_tweet_result_failure_keeps_parsed);
  RUN(test_tweet_result_stale_id_ignored);
  RUN(test_tweet_result_after_cancel_ignored);
  RUN(test_overlay_confirm_posts_reply);
  RUN(test_publish_without_url_is_normal_post);
  RUN(test_cx_n_not_connected);
  RUN(test_cx_n_requests_feed);
  RUN(test_feed_result_opens_news_note);
  RUN(test_feed_skip_link_only_posts);
  RUN(test_feed_skip_single_line_posts);
  RUN(test_feed_quote_text);
  RUN(test_menu_file_actions);
  RUN(test_menu_edit_actions);
  RUN(test_menu_format_wraps_region);
  RUN(test_menu_view_toggles_and_queries);
  RUN(test_menu_go_actions);
  RUN(test_menu_notes_actions);
  RUN(test_menu_chord_does_not_eat_next_char);
  RUN(test_tick_syncs_view_menu);
  RUN(test_cx_m_not_connected);
  RUN(test_cx_m_requests_bookmarks);
  RUN(test_bookmarks_result_opens_note);
  RUN(test_feed_failure_reports_error);
  /* graph view (C-x g) */
  RUN(test_cx_g_opens_graph_view);
  RUN(test_cx_g_toggles_closed);
  RUN(test_graph_click_opens_note);
  RUN(test_graph_pan_drags_view);
  RUN(test_graph_same_day_edge);
  RUN(test_graph_opened_after_edge);
  RUN(test_graph_opens_fitted);
  RUN(test_graph_labels_fade_with_zoom);
  RUN(test_graph_ghost_node_outlined);
  RUN(test_graph_far_zoom_draws_small_dots);
  RUN(test_graph_label_above_node);
  RUN(test_graph_no_hint_banner);
  RUN(test_graph_legend_toggles_edge_kind);
  RUN(test_graph_edges_fade_with_zoom);
  RUN(test_graph_node_size_reflects_backlinks);

  /* leave globals clean for any later suite */
  tv_test_reset();
  buf_set_documents_dir("");
}
