/* unit_editing.c — unit tests for Editor/editing.c.
 * Pure EditorState operations: no window, no GL, no SDL runtime. */
#include <string.h>
#include <stdlib.h>
#include "test.h"
#include "ed_fixture.h"
#include "editing.h"
#include "buffer.h"
#include "undo.h"

/* ---- ed_insert_char ---- */

static void test_insert_into_empty(void) {
  EditorState ed = {0};
  buf_init_empty(&ed);
  ed_insert_char(&ed, "h");
  ed_insert_char(&ed, "i");
  CHECK_SEQ(LINE(ed, 0), "hi");
  CHECK_IEQ(ed.cursor_col, 2);
  CHECK_IEQ(ed.line_count, 1);
  ed_teardown(&ed);
}

static void test_insert_midline(void) {
  EditorState ed = {0};
  ed_load(&ed, "ac");
  ed.cursor_col = 1;                  /* between 'a' and 'c' */
  ed_insert_char(&ed, "b");
  CHECK_SEQ(LINE(ed, 0), "abc");
  CHECK_IEQ(ed.cursor_col, 2);
  ed_teardown(&ed);
}

static void test_insert_utf8_is_byte_indexed(void) {
  EditorState ed = {0};
  buf_init_empty(&ed);
  ed_insert_char(&ed, "\xe2\x86\x92");  /* "→", 3 UTF-8 bytes, one call */
  CHECK_SEQ(LINE(ed, 0), "\xe2\x86\x92");
  CHECK_IEQ(ed.cursor_col, 3);          /* cursor is byte-indexed */
  ed_teardown(&ed);
}

/* ---- ed_backspace ---- */

static void test_backspace_in_line(void) {
  EditorState ed = {0};
  ed_load(&ed, "abc");
  ed_backspace(&ed);
  CHECK_SEQ(LINE(ed, 0), "ab");
  CHECK_IEQ(ed.cursor_col, 2);
  ed_teardown(&ed);
}

static void test_backspace_joins_lines(void) {
  EditorState ed = {0};
  ed_load(&ed, "foo");
  buf_insert_line_at(&ed, 1, "bar", 3);
  ed.cursor_line = 1;
  ed.cursor_col = 0;                  /* start of "bar" */
  ed_backspace(&ed);
  CHECK_IEQ(ed.line_count, 1);
  CHECK_SEQ(LINE(ed, 0), "foobar");
  CHECK_IEQ(ed.cursor_line, 0);
  CHECK_IEQ(ed.cursor_col, 3);        /* at the join point */
  ed_teardown(&ed);
}

/* ---- ed_enter ---- */

static void test_enter_splits_line(void) {
  EditorState ed = {0};
  ed_load(&ed, "abcd");
  ed.cursor_col = 2;
  ed_enter(&ed);
  CHECK_IEQ(ed.line_count, 2);
  CHECK_SEQ(LINE(ed, 0), "ab");
  CHECK_SEQ(LINE(ed, 1), "cd");
  CHECK_IEQ(ed.cursor_line, 1);
  CHECK_IEQ(ed.cursor_col, 0);
  ed_teardown(&ed);
}

static void test_enter_continues_bullet(void) {
  EditorState ed = {0};
  ed_load(&ed, "- foo");             /* caret at end */
  ed_enter(&ed);
  CHECK_IEQ(ed.line_count, 2);
  CHECK_SEQ(LINE(ed, 0), "- foo");
  CHECK_SEQ(LINE(ed, 1), "- ");      /* bullet carried to the new line */
  CHECK_IEQ(ed.cursor_col, 2);
  ed_teardown(&ed);
}

static void test_enter_continues_numbered(void) {
  EditorState ed = {0};
  ed_load(&ed, "3. item");
  ed_enter(&ed);
  CHECK_SEQ(LINE(ed, 1), "4. ");     /* number incremented */
  CHECK_IEQ(ed.cursor_col, 3);
  ed_teardown(&ed);
}

/* ---- ed_indent_line / ed_dedent_line ---- */

static void test_indent_inserts_two_spaces(void) {
  EditorState ed = {0};
  ed_load(&ed, "- foo");
  int col = ed.cursor_col;
  ed_indent_line(&ed);
  CHECK_SEQ(LINE(ed, 0), "  - foo");
  CHECK_IEQ(ed.cursor_col, col + 2); /* caret stays on the same char */
  ed_teardown(&ed);
}

static void test_dedent_removes_indent(void) {
  EditorState ed = {0};
  ed_load(&ed, "  - foo");
  ed_dedent_line(&ed);
  CHECK_SEQ(LINE(ed, 0), "- foo");
  ed_teardown(&ed);
}

static void test_dedent_noop_at_margin(void) {
  EditorState ed = {0};
  ed_load(&ed, "- foo");
  int col = ed.cursor_col;
  ed_dedent_line(&ed);
  CHECK_SEQ(LINE(ed, 0), "- foo");   /* nothing to remove */
  CHECK_IEQ(ed.cursor_col, col);
  ed_teardown(&ed);
}

static void test_indent_then_dedent_roundtrip(void) {
  EditorState ed = {0};
  ed_load(&ed, "- foo");
  ed_indent_line(&ed);
  ed_dedent_line(&ed);
  CHECK_SEQ(LINE(ed, 0), "- foo");
  ed_teardown(&ed);
}

/* ---- kill ring: copy / yank / kill-line ---- */

static void test_kill_line_to_eol(void) {
  EditorState ed = {0};
  ed_load(&ed, "hello world");
  ed.cursor_col = 5;                 /* before the space */
  ed_emacs_kill_line(&ed);
  CHECK_SEQ(LINE(ed, 0), "hello");
  CHECK_SEQ(ed.kill_buf, " world");
  CHECK_IEQ(ed.kill_len, 6);
  ed_teardown(&ed);
}

static void test_copy_region_single_line(void) {
  EditorState ed = {0};
  ed_load(&ed, "hello world");
  ed.cursor_col = 0;
  buf_mark_set(&ed);                 /* mark at (0,0) */
  ed.cursor_col = 5;
  ed_emacs_copy_region(&ed);
  CHECK_SEQ(ed.kill_buf, "hello");   /* region copied... */
  CHECK_SEQ(LINE(ed, 0), "hello world");  /* ...buffer unchanged */
  ed_teardown(&ed);
}

static void test_copy_region_multiline(void) {
  EditorState ed = {0};
  ed_load(&ed, "abc");
  buf_insert_line_at(&ed, 1, "def", 3);
  ed.cursor_line = 0; ed.cursor_col = 1;
  buf_mark_set(&ed);                 /* mark at (0,1) */
  ed.cursor_line = 1; ed.cursor_col = 2;
  ed_emacs_copy_region(&ed);
  CHECK_SEQ(ed.kill_buf, "bc\nde");  /* newline joins the rows */
  CHECK_IEQ(ed.kill_len, 5);
  ed_teardown(&ed);
}

static void test_yank_inserts_kill_buffer(void) {
  EditorState ed = {0};
  buf_init_empty(&ed);
  buf_kill_set(&ed, "XY", 2);
  ed_emacs_yank(&ed);
  CHECK_SEQ(LINE(ed, 0), "XY");
  CHECK_IEQ(ed.cursor_col, 2);
  ed_teardown(&ed);
}

static void test_yank_multiline_splits(void) {
  EditorState ed = {0};
  buf_init_empty(&ed);
  buf_kill_set(&ed, "a\nb", 3);
  ed_emacs_yank(&ed);
  CHECK_IEQ(ed.line_count, 2);
  CHECK_SEQ(LINE(ed, 0), "a");
  CHECK_SEQ(LINE(ed, 1), "b");
  CHECK_IEQ(ed.cursor_line, 1);
  CHECK_IEQ(ed.cursor_col, 1);
  ed_teardown(&ed);
}

static void test_copy_then_yank_roundtrip(void) {
  EditorState ed = {0};
  ed_load(&ed, "hello world");
  ed.cursor_col = 0;
  buf_mark_set(&ed);
  ed.cursor_col = 5;
  ed_emacs_copy_region(&ed);         /* kill = "hello" */
  ed.cursor_col = ed.lines[0].len;   /* to end of line */
  ed_emacs_yank(&ed);
  CHECK_SEQ(LINE(ed, 0), "hello worldhello");
  ed_teardown(&ed);
}

static void test_region_dup_returns_marked_text(void) {
  EditorState ed = {0};
  ed_load(&ed, "abc");
  buf_insert_line_at(&ed, 1, "def", 3);
  ed.cursor_line = 0; ed.cursor_col = 1;
  buf_mark_set(&ed);                 /* mark at (0,1) */
  ed.cursor_line = 1; ed.cursor_col = 2;
  int len = -1;
  char *r = ed_region_dup(&ed, &len);
  CHECK_SEQ(r, "bc\nde");
  CHECK_IEQ(len, 5);
  CHECK_SEQ(LINE(ed, 0), "abc");     /* buffer untouched */
  free(r);
  /* no region -> NULL, len 0 */
  buf_mark_clear(&ed);
  len = 99;
  CHECK(ed_region_dup(&ed, &len) == NULL);
  CHECK_IEQ(len, 0);
  ed_teardown(&ed);
}

static void test_replace_region_swaps_in_text(void) {
  EditorState ed = {0};
  ed_load(&ed, "before MARK after");
  ed.cursor_col = 7;
  buf_mark_set(&ed);                 /* mark before "MARK" */
  ed.cursor_col = 11;                /* point after "MARK" */
  ed_replace_region(&ed, "[[Note.md]]");
  CHECK_SEQ(LINE(ed, 0), "before [[Note.md]] after");
  CHECK_IEQ(ed.mark_active, 0);      /* mark cleared */
  ed_teardown(&ed);
}

static void test_replace_region_multiline_is_one_undo(void) {
  EditorState ed = {0};
  ed_load(&ed, "abc");
  buf_insert_line_at(&ed, 1, "def", 3);
  ed.cursor_line = 0; ed.cursor_col = 1;
  buf_mark_set(&ed);
  ed.cursor_line = 1; ed.cursor_col = 2;   /* region "bc\nde" */
  ed_replace_region(&ed, "[[X.md]]");
  CHECK_IEQ(ed.line_count, 1);
  CHECK_SEQ(LINE(ed, 0), "a[[X.md]]f");
  /* a single undo step restores both the cut and the link insertion */
  undo_perform(&ed);
  CHECK_IEQ(ed.line_count, 2);
  CHECK_SEQ(LINE(ed, 0), "abc");
  CHECK_SEQ(LINE(ed, 1), "def");
  ed_teardown(&ed);
}

void suite_editing(void) {
  RUN(test_insert_into_empty);
  RUN(test_insert_midline);
  RUN(test_insert_utf8_is_byte_indexed);
  RUN(test_backspace_in_line);
  RUN(test_backspace_joins_lines);
  RUN(test_enter_splits_line);
  RUN(test_enter_continues_bullet);
  RUN(test_enter_continues_numbered);
  RUN(test_indent_inserts_two_spaces);
  RUN(test_dedent_removes_indent);
  RUN(test_dedent_noop_at_margin);
  RUN(test_indent_then_dedent_roundtrip);
  RUN(test_kill_line_to_eol);
  RUN(test_copy_region_single_line);
  RUN(test_copy_region_multiline);
  RUN(test_yank_inserts_kill_buffer);
  RUN(test_yank_multiline_splits);
  RUN(test_copy_then_yank_roundtrip);
  RUN(test_region_dup_returns_marked_text);
  RUN(test_replace_region_swaps_in_text);
  RUN(test_replace_region_multiline_is_one_undo);
}
