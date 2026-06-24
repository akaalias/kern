/* unit_commands.c — feature tests for the de-globalized command dispatch.
 * Feeds key chords to kern_dispatch_key and asserts on EditorState. */
#include <SDL2/SDL.h>
#include "test.h"
#include "ed_fixture.h"
#include "commands.h"

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

void suite_commands(void) {
  RUN(test_dispatch_unbound_returns_zero);
  RUN(test_ctrl_a_beginning_of_line);
  RUN(test_ctrl_e_end_of_line);
  RUN(test_ctrl_f_and_b_char);
  RUN(test_ctrl_f_wraps_to_next_line);
  RUN(test_ctrl_n_and_p_line);
  RUN(test_meta_f_and_b_word);
}
