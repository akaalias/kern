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

static void test_undo_empty_is_noop(void) {
  EditorState ed = {0};
  buf_init_empty(&ed);
  undo_perform(&ed);                 /* nothing on the stack — must not crash */
  CHECK_SEQ(LINE(ed, 0), "");
  CHECK_IEQ(ed.line_count, 1);
  ed_teardown(&ed);
}

void suite_undo(void) {
  RUN(test_undo_insert);
  RUN(test_undo_backspace_restores_char);
  RUN(test_undo_enter_rejoins);
  RUN(test_undo_list_enter_is_one_group);
  RUN(test_undo_indent);
  RUN(test_undo_coalesces_typed_run);
  RUN(test_undo_empty_is_noop);
}
