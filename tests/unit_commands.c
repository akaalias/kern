/* unit_commands.c — feature tests for the de-globalized command dispatch.
 * Feeds key chords to kern_dispatch_key and asserts on EditorState. */
#include <stdlib.h>
#include <SDL2/SDL.h>
#include "test.h"
#include "ed_fixture.h"
#include "commands.h"
#include "clipboard.h"

/* A ViewState with a realistic viewport so nav_ensure_cursor_visible behaves. */
static ViewState vs_make(void) {
  ViewState vs = {0};
  vs.content_y = 80;
  vs.content_h = 600;
  return vs;
}

static void test_dispatch_unbound_returns_zero(void) {
  EditorState ed = {0}; ed_load(&ed, "x");
  ViewState vs = vs_make();
  CHECK_IEQ(kern_dispatch_key(&ed, &vs, KMOD_CTRL, SDLK_z), 0);  /* C-z not bound */
  ed_teardown(&ed);
}

static void test_ctrl_a_beginning_of_line(void) {
  EditorState ed = {0}; ed_load(&ed, "hello");   /* caret at end */
  ViewState vs = vs_make();
  CHECK_IEQ(kern_dispatch_key(&ed, &vs, KMOD_CTRL, SDLK_a), 1);
  CHECK_IEQ(ed.cursor_col, 0);
  ed_teardown(&ed);
}

static void test_ctrl_e_end_of_line(void) {
  EditorState ed = {0}; ed_load(&ed, "hello");
  ed.cursor_col = 0;
  ViewState vs = vs_make();
  CHECK_IEQ(kern_dispatch_key(&ed, &vs, KMOD_CTRL, SDLK_e), 1);
  CHECK_IEQ(ed.cursor_col, 5);
  ed_teardown(&ed);
}

static void test_ctrl_f_and_b_char(void) {
  EditorState ed = {0}; ed_load(&ed, "ab");
  ed.cursor_col = 0;
  ViewState vs = vs_make();
  kern_dispatch_key(&ed, &vs, KMOD_CTRL, SDLK_f);
  CHECK_IEQ(ed.cursor_col, 1);
  kern_dispatch_key(&ed, &vs, KMOD_CTRL, SDLK_b);
  CHECK_IEQ(ed.cursor_col, 0);
  ed_teardown(&ed);
}

static void test_ctrl_f_wraps_to_next_line(void) {
  EditorState ed = {0}; ed_load(&ed, "ab");
  buf_insert_line_at(&ed, 1, "cd", 2);
  ed.cursor_line = 0; ed.cursor_col = 2;          /* end of line 0 */
  ViewState vs = vs_make();
  kern_dispatch_key(&ed, &vs, KMOD_CTRL, SDLK_f);
  CHECK_IEQ(ed.cursor_line, 1);
  CHECK_IEQ(ed.cursor_col, 0);
  ed_teardown(&ed);
}

static void test_ctrl_n_and_p_line(void) {
  EditorState ed = {0}; ed_load(&ed, "one");
  buf_insert_line_at(&ed, 1, "two", 3);
  ed.cursor_line = 0; ed.cursor_col = 1;
  ed.cursor_target_col = 1;
  ViewState vs = vs_make();
  kern_dispatch_key(&ed, &vs, KMOD_CTRL, SDLK_n);
  CHECK_IEQ(ed.cursor_line, 1);
  CHECK_IEQ(ed.cursor_col, 1);                     /* keeps target column */
  kern_dispatch_key(&ed, &vs, KMOD_CTRL, SDLK_p);
  CHECK_IEQ(ed.cursor_line, 0);
  ed_teardown(&ed);
}

static void test_meta_f_and_b_word(void) {
  EditorState ed = {0}; ed_load(&ed, "alpha beta");
  ed.cursor_col = 0;
  ViewState vs = vs_make();
  kern_dispatch_key(&ed, &vs, KMOD_ALT, SDLK_f);
  CHECK(ed.cursor_col > 0);                         /* advanced past a word */
  int after_fwd = ed.cursor_col;
  kern_dispatch_key(&ed, &vs, KMOD_ALT, SDLK_b);
  CHECK(ed.cursor_col < after_fwd);                 /* moved back */
  ed_teardown(&ed);
}

/* ---- editing commands (batch 2a) ---- */

static void test_ctrl_d_deletes_forward(void) {
  EditorState ed = {0}; ed_load(&ed, "abc"); ed.cursor_col = 0;
  ViewState vs = vs_make();
  kern_dispatch_key(&ed, &vs, KMOD_CTRL, SDLK_d);
  CHECK_SEQ(LINE(ed, 0), "bc");
  ed_teardown(&ed);
}

static void test_backspace_deletes_back(void) {
  EditorState ed = {0}; ed_load(&ed, "abc");     /* caret at end */
  ViewState vs = vs_make();
  kern_dispatch_key(&ed, &vs, 0, SDLK_BACKSPACE);
  CHECK_SEQ(LINE(ed, 0), "ab");
  ed_teardown(&ed);
}

static void test_return_splits_line(void) {
  EditorState ed = {0}; ed_load(&ed, "ab"); ed.cursor_col = 1;
  ViewState vs = vs_make();
  kern_dispatch_key(&ed, &vs, 0, SDLK_RETURN);
  CHECK_IEQ(ed.line_count, 2);
  CHECK_SEQ(LINE(ed, 0), "a");
  CHECK_SEQ(LINE(ed, 1), "b");
  ed_teardown(&ed);
}

static void test_set_mark_then_quit_clears(void) {
  EditorState ed = {0}; ed_load(&ed, "x");
  ViewState vs = vs_make();
  kern_dispatch_key(&ed, &vs, KMOD_CTRL, SDLK_SPACE);
  CHECK_IEQ(ed.mark_active, 1);
  kern_dispatch_key(&ed, &vs, KMOD_CTRL, SDLK_g);   /* keyboard-quit */
  CHECK_IEQ(ed.mark_active, 0);
  ed_teardown(&ed);
}

static void test_undo_via_dispatch(void) {
  EditorState ed = {0}; ed_load(&ed, "ab");
  ViewState vs = vs_make();
  kern_dispatch_key(&ed, &vs, 0, SDLK_BACKSPACE);   /* "a" */
  CHECK_SEQ(LINE(ed, 0), "a");
  kern_dispatch_key(&ed, &vs, KMOD_CTRL, SDLK_SLASH); /* C-/ undo */
  CHECK_SEQ(LINE(ed, 0), "ab");
  ed_teardown(&ed);
}

static void test_open_line_keeps_point(void) {
  EditorState ed = {0}; ed_load(&ed, "abc"); ed.cursor_col = 1;
  ViewState vs = vs_make();
  kern_dispatch_key(&ed, &vs, KMOD_CTRL, SDLK_o);
  CHECK_IEQ(ed.line_count, 2);
  CHECK_SEQ(LINE(ed, 0), "a");
  CHECK_SEQ(LINE(ed, 1), "bc");
  CHECK_IEQ(ed.cursor_line, 0);     /* point stays put */
  CHECK_IEQ(ed.cursor_col, 1);
  ed_teardown(&ed);
}

static void test_transpose_chars(void) {
  EditorState ed = {0}; ed_load(&ed, "ab");        /* caret at end */
  ViewState vs = vs_make();
  kern_dispatch_key(&ed, &vs, KMOD_CTRL, SDLK_t);
  CHECK_SEQ(LINE(ed, 0), "ba");
  ed_teardown(&ed);
}

/* ---- kill / yank / copy / case (batch 2b) ---- */

static void test_ctrl_k_kill_line_to_clipboard(void) {
  EditorState ed = {0}; ed_load(&ed, "hello world"); ed.cursor_col = 5;
  ViewState vs = vs_make();
  kern_dispatch_key(&ed, &vs, KMOD_CTRL, SDLK_k);
  CHECK_SEQ(LINE(ed, 0), "hello");
  char *clip = kern_clipboard_get();
  CHECK_SEQ(clip, " world");          /* mirrored to the clipboard */
  free(clip);
  ed_teardown(&ed);
}

static void test_ctrl_w_kill_region(void) {
  EditorState ed = {0}; ed_load(&ed, "hello world");
  ed.cursor_col = 0; buf_mark_set(&ed); ed.cursor_col = 6;   /* "hello " */
  ViewState vs = vs_make();
  kern_dispatch_key(&ed, &vs, KMOD_CTRL, SDLK_w);
  CHECK_SEQ(LINE(ed, 0), "world");
  ed_teardown(&ed);
}

static void test_meta_w_copy_region_keeps_text(void) {
  EditorState ed = {0}; ed_load(&ed, "hello world");
  ed.cursor_col = 0; buf_mark_set(&ed); ed.cursor_col = 5;   /* "hello" */
  ViewState vs = vs_make();
  kern_dispatch_key(&ed, &vs, KMOD_ALT, SDLK_w);
  CHECK_SEQ(LINE(ed, 0), "hello world");   /* buffer unchanged */
  CHECK_IEQ(ed.mark_active, 0);            /* mark cleared */
  char *clip = kern_clipboard_get();
  CHECK_SEQ(clip, "hello");
  free(clip);
  ed_teardown(&ed);
}

static void test_ctrl_y_yank_from_clipboard(void) {
  EditorState ed = {0}; buf_init_empty(&ed);
  ViewState vs = vs_make();
  kern_clipboard_set("XY");
  kern_dispatch_key(&ed, &vs, KMOD_CTRL, SDLK_y);
  CHECK_SEQ(LINE(ed, 0), "XY");
  ed_teardown(&ed);
}

static void test_copy_then_yank_via_dispatch(void) {
  EditorState ed = {0}; ed_load(&ed, "hello world");
  ed.cursor_col = 0; buf_mark_set(&ed); ed.cursor_col = 5;
  ViewState vs = vs_make();
  kern_dispatch_key(&ed, &vs, KMOD_ALT, SDLK_w);   /* copy "hello" */
  ed.cursor_col = ed.lines[0].len;
  kern_dispatch_key(&ed, &vs, KMOD_CTRL, SDLK_y);  /* yank at end */
  CHECK_SEQ(LINE(ed, 0), "hello worldhello");
  ed_teardown(&ed);
}

static void test_meta_d_kill_word_forward(void) {
  EditorState ed = {0}; ed_load(&ed, "alpha beta"); ed.cursor_col = 0;
  ViewState vs = vs_make();
  kern_dispatch_key(&ed, &vs, KMOD_ALT, SDLK_d);
  CHECK_SEQ(LINE(ed, 0), "beta");   /* kills "alpha " through the trailing space */
  ed_teardown(&ed);
}

static void test_meta_case_words(void) {
  ViewState vs = vs_make();
  EditorState ed = {0}; ed_load(&ed, "abc"); ed.cursor_col = 0;
  kern_dispatch_key(&ed, &vs, KMOD_ALT, SDLK_u);
  CHECK_SEQ(LINE(ed, 0), "ABC");
  ed_teardown(&ed);

  EditorState ed2 = {0}; ed_load(&ed2, "ABC"); ed2.cursor_col = 0;
  kern_dispatch_key(&ed2, &vs, KMOD_ALT, SDLK_l);
  CHECK_SEQ(LINE(ed2, 0), "abc");
  ed_teardown(&ed2);

  EditorState ed3 = {0}; ed_load(&ed3, "hello"); ed3.cursor_col = 0;
  kern_dispatch_key(&ed3, &vs, KMOD_ALT, SDLK_c);
  CHECK_SEQ(LINE(ed3, 0), "Hello");
  ed_teardown(&ed3);
}

/* ---- scrolling / font / buffer-ends / mark (batch 3) ---- */

static void mkbuf(EditorState *ed, int nlines) {
  buf_init_empty(ed);
  for (int i = 1; i < nlines; i++) buf_insert_line_at(ed, i, "line", 4);
}

static void test_buffer_ends(void) {
  EditorState ed = {0}; mkbuf(&ed, 5);
  ViewState vs = vs_make();
  ed.cursor_line = 2; ed.cursor_col = 1;
  /* buffer-ends are invoked from textview's section-5 shift handler, not the
     dispatch table, so call the exposed commands directly. */
  cmd_end_of_buffer_alt(&ed, &vs);
  CHECK_IEQ(ed.cursor_line, 4);
  cmd_beginning_of_buffer_alt(&ed, &vs);
  CHECK_IEQ(ed.cursor_line, 0);
  CHECK_IEQ(ed.cursor_col, 0);
  ed_teardown(&ed);
}

static void test_mark_whole_buffer(void) {
  EditorState ed = {0}; mkbuf(&ed, 3);
  ViewState vs = vs_make();
  cmd_mark_whole_buffer(&ed, &vs);          /* C-x h */
  CHECK_IEQ(ed.mark_active, 1);
  CHECK_IEQ(ed.mark_line, 2);               /* mark at end */
  CHECK_IEQ(ed.cursor_line, 0);             /* point at start */
  ed_teardown(&ed);
}

static void test_exchange_point_and_mark(void) {
  EditorState ed = {0}; mkbuf(&ed, 3);
  ViewState vs = vs_make();
  ed.cursor_line = 0; ed.cursor_col = 0;
  buf_mark_set(&ed);                        /* mark (0,0) */
  ed.cursor_line = 2; ed.cursor_col = 1;
  cmd_exchange_point_mark(&ed, &vs);        /* C-x C-x */
  CHECK_IEQ(ed.cursor_line, 0);             /* point ↔ mark */
  CHECK_IEQ(ed.mark_line, 2);
  ed_teardown(&ed);
}

static void test_page_down_moves_cursor(void) {
  EditorState ed = {0}; mkbuf(&ed, 60);
  ViewState vs = vs_make();
  ed.cursor_line = 0; ed.cursor_col = 0;
  kern_dispatch_key(&ed, &vs, KMOD_CTRL, SDLK_v);   /* C-v page down */
  CHECK(ed.cursor_line > 0);                        /* advanced down a page */
  ed_teardown(&ed);
}

static void test_font_size_changes(void) {
  EditorState ed = {0}; buf_init_empty(&ed);
  ViewState vs = vs_make(); vs.font_size = 20.0f;
  kern_dispatch_key(&ed, &vs, KMOD_GUI, SDLK_EQUALS);   /* Cmd-= */
  CHECK(vs.font_size > 20.0f);
  float bigger = vs.font_size;
  kern_dispatch_key(&ed, &vs, KMOD_GUI, SDLK_MINUS);    /* Cmd-- */
  CHECK(vs.font_size < bigger);
  ed_teardown(&ed);
}

void suite_commands(void) {
  kern_clipboard_set("");   /* start from a known clipboard state */
  RUN(test_dispatch_unbound_returns_zero);
  RUN(test_ctrl_a_beginning_of_line);
  RUN(test_ctrl_e_end_of_line);
  RUN(test_ctrl_f_and_b_char);
  RUN(test_ctrl_f_wraps_to_next_line);
  RUN(test_ctrl_n_and_p_line);
  RUN(test_meta_f_and_b_word);
  RUN(test_ctrl_d_deletes_forward);
  RUN(test_backspace_deletes_back);
  RUN(test_return_splits_line);
  RUN(test_set_mark_then_quit_clears);
  RUN(test_undo_via_dispatch);
  RUN(test_open_line_keeps_point);
  RUN(test_transpose_chars);
  RUN(test_ctrl_k_kill_line_to_clipboard);
  RUN(test_ctrl_w_kill_region);
  RUN(test_meta_w_copy_region_keeps_text);
  RUN(test_ctrl_y_yank_from_clipboard);
  RUN(test_copy_then_yank_via_dispatch);
  RUN(test_meta_d_kill_word_forward);
  RUN(test_meta_case_words);
  RUN(test_buffer_ends);
  RUN(test_mark_whole_buffer);
  RUN(test_exchange_point_and_mark);
  RUN(test_page_down_moves_cursor);
  RUN(test_font_size_changes);
}
