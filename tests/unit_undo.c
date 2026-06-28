/* unit_undo.c — unit tests for Editor/undo.c.
 * Drives undo through the real editing operations, then undo_perform(). */
#include <string.h>
#include "test.h"
#include "ed_fixture.h"
#include "editing.h"
#include "undo.h"

static void test_undo_insert(void) {
  EditorState ed = {0};
  ed_load(&ed, "abc");
  ed_insert_char(&ed, "d");
  CHECK_SEQ(LINE(ed, 0), "abcd");
  undo_perform(&ed);
  CHECK_SEQ(LINE(ed, 0), "abc");
  ed_teardown(&ed);
}

static void test_undo_backspace_restores_char(void) {
  EditorState ed = {0};
  ed_load(&ed, "abc");
  ed_backspace(&ed);
  CHECK_SEQ(LINE(ed, 0), "ab");
  undo_perform(&ed);
  CHECK_SEQ(LINE(ed, 0), "abc");     /* deleted char re-inserted */
  ed_teardown(&ed);
}

static void test_undo_enter_rejoins(void) {
  EditorState ed = {0};
  ed_load(&ed, "abcd");
  ed.cursor_col = 2;
  ed_enter(&ed);
  CHECK_IEQ(ed.line_count, 2);
  undo_perform(&ed);
  CHECK_IEQ(ed.line_count, 1);
  CHECK_SEQ(LINE(ed, 0), "abcd");
  ed_teardown(&ed);
}

/* A list-continuation Enter records split + prefix-insert as one undo group,
   so a single undo reverts both. */
static void test_undo_list_enter_is_one_group(void) {
  EditorState ed = {0};
  ed_load(&ed, "- foo");
  ed_enter(&ed);
  CHECK_IEQ(ed.line_count, 2);
  CHECK_SEQ(LINE(ed, 1), "- ");
  undo_perform(&ed);
  CHECK_IEQ(ed.line_count, 1);
  CHECK_SEQ(LINE(ed, 0), "- foo");
  ed_teardown(&ed);
}

static void test_undo_indent(void) {
  EditorState ed = {0};
  ed_load(&ed, "- foo");
  ed_indent_line(&ed);
  CHECK_SEQ(LINE(ed, 0), "  - foo");
  undo_perform(&ed);
  CHECK_SEQ(LINE(ed, 0), "- foo");
  ed_teardown(&ed);
}

/* Consecutive single-char inserts at adjacent columns coalesce into one undo
   op, so one undo_perform removes the whole typed run. */
static void test_undo_coalesces_typed_run(void) {
  EditorState ed = {0};
  buf_init_empty(&ed);
  ed_insert_char(&ed, "a");
  ed_insert_char(&ed, "b");
  ed_insert_char(&ed, "c");
  CHECK_SEQ(LINE(ed, 0), "abc");
  undo_perform(&ed);
  CHECK_SEQ(LINE(ed, 0), "");        /* one undo clears the coalesced run */
  ed_teardown(&ed);
}

/* Undoing a multi-line kill re-inserts text containing newlines (the split
   re-insert path in undo_apply_one). */
static void test_undo_kill_multiline_region(void) {
  EditorState ed = {0};
  ed_load(&ed, "ab");
  buf_insert_line_at(&ed, 1, "cd", 2);
  ed.cursor_line = 0; ed.cursor_col = 1;
  buf_mark_set(&ed);                 /* mark (0,1) */
  ed.cursor_line = 1; ed.cursor_col = 1;   /* point (1,1) — region "b\nc" */
  ed_emacs_kill_region(&ed);
  CHECK_IEQ(ed.line_count, 1);
  CHECK_SEQ(LINE(ed, 0), "ad");
  undo_perform(&ed);
  CHECK_IEQ(ed.line_count, 2);
  CHECK_SEQ(LINE(ed, 0), "ab");
  CHECK_SEQ(LINE(ed, 1), "cd");
  ed_teardown(&ed);
}

/* Backspace at column 0 joins the line into the previous one (UNDO_JOIN_LINE);
   undoing must split it back apart. */
static void test_undo_join_line_resplits(void) {
  EditorState ed = {0};
  ed_load(&ed, "foo");
  buf_insert_line_at(&ed, 1, "bar", 3);
  ed.cursor_line = 1; ed.cursor_col = 0;
  ed_backspace(&ed);                 /* join → "foobar" */
  CHECK_IEQ(ed.line_count, 1);
  CHECK_SEQ(LINE(ed, 0), "foobar");
  undo_perform(&ed);
  CHECK_IEQ(ed.line_count, 2);
  CHECK_SEQ(LINE(ed, 0), "foo");
  CHECK_SEQ(LINE(ed, 1), "bar");
  ed_teardown(&ed);
}

/* The undo stack is bounded: past MAX_UNDO ops, undo_top saturates rather than
   overflowing the ring buffer. */
static void test_undo_stack_caps_at_max(void) {
  EditorState ed = {0};
  buf_init_empty(&ed);
  for (int i = 0; i < MAX_UNDO + 10; i++) ed_enter(&ed);  /* one SPLIT op each */
  CHECK_IEQ(ed.undo_count, MAX_UNDO);
  ed_teardown(&ed);
}

static void test_undo_empty_is_noop(void) {
  EditorState ed = {0};
  buf_init_empty(&ed);
  undo_perform(&ed);                 /* nothing on the stack — must not crash */
  CHECK_SEQ(LINE(ed, 0), "");
  CHECK_IEQ(ed.line_count, 1);
  ed_teardown(&ed);
}

/* Loading a new buffer (buf_init_empty / buf_load_file) must drop the old
   file's undo history — otherwise undoing applies ops that reference a buffer
   that no longer exists. Regression for the C-/ -after-buffer-switch crash. */
static void test_undo_cleared_on_buffer_reset(void) {
  EditorState ed = {0};
  ed_load(&ed, "abcdef");
  ed_insert_char(&ed, "g");          /* push an insert op at col 6 */
  CHECK(ed.undo_count > 0);
  buf_init_empty(&ed);               /* simulates opening another file */
  CHECK_IEQ(ed.undo_count, 0);
  CHECK_IEQ(ed.undo_top, 0);
  undo_perform(&ed);                 /* must be a no-op, not a stale-op replay */
  CHECK_SEQ(LINE(ed, 0), "");
  ed_teardown(&ed);
}

/* Defense in depth: an op whose (line,col,text_len) overran the live buffer
   used to drive an out-of-bounds memmove (negative length -> giant size_t).
   undo_apply_one now rejects it instead of corrupting memory. */
static void test_undo_stale_op_is_safe(void) {
  EditorState ed = {0};
  ed_load(&ed, "abcdef");
  ed_insert_char(&ed, "g");          /* insert op: line 0, col 6, len 1 */
  /* Shrink the line behind undo's back, as a smaller file in the same slot
     would — col+text_len (7) now exceeds len (2). */
  ed.lines[0].len = 2;
  ed.lines[0].text[2] = '\0';
  ed.cursor_col = 2;
  undo_perform(&ed);                 /* must not crash; op rejected as OOB */
  CHECK_SEQ(LINE(ed, 0), "ab");      /* buffer left intact */
  ed_teardown(&ed);
}

void suite_undo(void) {
  RUN(test_undo_cleared_on_buffer_reset);
  RUN(test_undo_stale_op_is_safe);
  RUN(test_undo_insert);
  RUN(test_undo_backspace_restores_char);
  RUN(test_undo_enter_rejoins);
  RUN(test_undo_list_enter_is_one_group);
  RUN(test_undo_indent);
  RUN(test_undo_coalesces_typed_run);
  RUN(test_undo_kill_multiline_region);
  RUN(test_undo_join_line_resplits);
  RUN(test_undo_stack_caps_at_max);
  RUN(test_undo_empty_is_noop);
}
