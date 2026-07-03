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

/* The pasted-smart-quote bug: one backspace must remove the whole 3-byte
   codepoint, not a single byte (which used to take three presses). */
static void test_backspace_removes_whole_codepoint(void) {
  EditorState ed = {0};
  buf_init_empty(&ed);
  ed_insert_char(&ed, "x");
  ed_insert_char(&ed, "\xe2\x80\x99");   /* U+2019 ’ smart apostrophe */
  CHECK_IEQ(ed.cursor_col, 4);
  ed_backspace(&ed);                      /* one press */
  CHECK_SEQ(LINE(ed, 0), "x");
  CHECK_IEQ(ed.cursor_col, 1);
  ed_teardown(&ed);
}

/* Forward-delete likewise removes the whole codepoint at point. */
static void test_delete_removes_whole_codepoint(void) {
  EditorState ed = {0};
  buf_init_empty(&ed);
  ed_insert_char(&ed, "\xe2\x80\x99");   /* ’ */
  ed_insert_char(&ed, "y");
  ed.cursor_col = 0;
  ed_delete(&ed);
  CHECK_SEQ(LINE(ed, 0), "y");
  CHECK_IEQ(ed.cursor_col, 0);
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

static void test_enter_continues_indented_bullet(void) {
  EditorState ed = {0};
  ed_load(&ed, "  - sub");            /* indented sub-list item, caret at end */
  ed_enter(&ed);
  CHECK_IEQ(ed.line_count, 2);
  CHECK_SEQ(LINE(ed, 0), "  - sub");
  CHECK_SEQ(LINE(ed, 1), "  - ");     /* indentation + bullet both carried */
  CHECK_IEQ(ed.cursor_col, 4);
  ed_teardown(&ed);
}

static void test_enter_continues_indented_numbered(void) {
  EditorState ed = {0};
  ed_load(&ed, "    2. item");        /* indented numbered item */
  ed_enter(&ed);
  CHECK_SEQ(LINE(ed, 1), "    3. ");   /* indentation kept, number incremented */
  CHECK_IEQ(ed.cursor_col, 7);
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

/* Backspace/Delete over an active selection removes the whole region without
   disturbing the kill ring (it's a delete, not a cut). */
static void test_delete_region_removes_selection(void) {
  EditorState ed = {0};
  ed_load(&ed, "hello world");
  buf_kill_set(&ed, "PRIOR", 5);     /* something already on the kill ring */
  ed.cursor_col = 0;
  buf_mark_set(&ed);                 /* mark at (0,0) */
  ed.cursor_col = 6;                 /* select "hello " */
  CHECK_IEQ(ed_delete_region(&ed), 1);
  CHECK_SEQ(LINE(ed, 0), "world");
  CHECK_IEQ(ed.mark_active, 0);      /* mark cleared */
  CHECK_SEQ(ed.kill_buf, "PRIOR");   /* kill ring untouched */
  CHECK_IEQ(ed.cursor_col, 0);
  ed_teardown(&ed);
}

static void test_delete_region_noop_without_mark(void) {
  EditorState ed = {0};
  ed_load(&ed, "abc");
  CHECK_IEQ(ed_delete_region(&ed), 0);   /* nothing selected → caller falls back */
  CHECK_SEQ(LINE(ed, 0), "abc");
  ed_teardown(&ed);
}

static void test_delete_region_multiline(void) {
  EditorState ed = {0};
  ed_load(&ed, "abc");
  buf_insert_line_at(&ed, 1, "def", 3);
  ed.cursor_line = 0; ed.cursor_col = 1;
  buf_mark_set(&ed);
  ed.cursor_line = 1; ed.cursor_col = 2;   /* select "bc\nde" */
  CHECK_IEQ(ed_delete_region(&ed), 1);
  CHECK_IEQ(ed.line_count, 1);
  CHECK_SEQ(LINE(ed, 0), "af");
  ed_teardown(&ed);
}

/* ---- read-only Context section (EditorState.readonly_from) ---- */

/* Editing primitives refuse to mutate a line inside the static section. */
static void test_readonly_blocks_edits(void) {
  EditorState ed = {0};
  ed_load(&ed, "body");
  buf_insert_line_at(&ed, 1, "## Context", 10);   /* a "section" line below */
  ed.readonly_from = 1;                            /* lines >= 1 are static */

  ed.cursor_line = 1; ed.cursor_col = 2;
  ed_insert_char(&ed, "X");                        /* blocked */
  CHECK_SEQ(LINE(ed, 1), "## Context");
  ed_delete(&ed);                                  /* blocked */
  CHECK_SEQ(LINE(ed, 1), "## Context");
  ed_backspace(&ed);                               /* blocked */
  CHECK_SEQ(LINE(ed, 1), "## Context");
  ed_enter(&ed);                                   /* blocked — no new line */
  CHECK_IEQ(ed.line_count, 2);

  /* the editable line above is still mutable */
  ed.cursor_line = 0; ed.cursor_col = 4;
  ed_insert_char(&ed, "!");
  CHECK_SEQ(LINE(ed, 0), "body!");
  ed_teardown(&ed);
}

/* A forward-delete at the end of the last editable line must not pull the
   section up into it. */
static void test_readonly_blocks_section_join(void) {
  EditorState ed = {0};
  ed_load(&ed, "body");
  buf_insert_line_at(&ed, 1, "## Context", 10);
  ed.readonly_from = 1;
  ed.cursor_line = 0; ed.cursor_col = 4;            /* end of "body" */
  ed_delete(&ed);                                   /* would join the section — blocked */
  CHECK_IEQ(ed.line_count, 2);
  CHECK_SEQ(LINE(ed, 0), "body");
  ed_teardown(&ed);
}

/* The boundary tracks the section as content above it is inserted/removed. */
static void test_readonly_boundary_tracks_edits(void) {
  EditorState ed = {0};
  ed_load(&ed, "body");
  buf_insert_line_at(&ed, 1, "## Context", 10);
  ed.readonly_from = 1;

  ed.cursor_line = 0; ed.cursor_col = 4;
  ed_enter(&ed);                       /* adds a content line above the section */
  CHECK_IEQ(ed.line_count, 3);
  CHECK_IEQ(ed.readonly_from, 2);      /* section shifted down with the insert */
  CHECK_SEQ(LINE(ed, 2), "## Context");
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

static void test_wrap_region_single_line(void) {
  EditorState ed = {0};
  ed_load(&ed, "make me bold here");
  ed.cursor_col = 8;
  buf_mark_set(&ed);                 /* mark before "bold" */
  ed.cursor_col = 12;                /* point after "bold" */
  CHECK_IEQ(ed_wrap_region(&ed, "*", "*"), 1);
  CHECK_SEQ(LINE(ed, 0), "make me *bold* here");
  CHECK_IEQ(ed.mark_active, 1);      /* region stays active... */
  /* ...over the inner text, so a second wrap stacks → **bold** */
  CHECK_IEQ(ed_wrap_region(&ed, "*", "*"), 1);
  CHECK_SEQ(LINE(ed, 0), "make me **bold** here");
  ed_teardown(&ed);
}

static void test_wrap_region_preserves_inner_selection(void) {
  EditorState ed = {0};
  ed_load(&ed, "ab cd ef");
  ed.cursor_col = 3;
  buf_mark_set(&ed);
  ed.cursor_col = 5;                 /* region = "cd", caret at end */
  ed_wrap_region(&ed, "*", "*");
  /* the selection now covers just the inner "cd", caret still at its end */
  int sl, sc, el, ec;
  buf_region_ordered(&ed, &sl, &sc, &el, &ec);
  CHECK_IEQ(sc, 4); CHECK_IEQ(ec, 6);
  CHECK_SEQ(LINE(ed, 0), "ab *cd* ef");
  ed_teardown(&ed);
}

static void test_wrap_region_cursor_at_start_orientation(void) {
  EditorState ed = {0};
  ed_load(&ed, "ab cd ef");
  ed.cursor_col = 5;
  buf_mark_set(&ed);                 /* mark at end of "cd" */
  ed.cursor_col = 3;                 /* caret at start of "cd" */
  ed_wrap_region(&ed, "*", "*");
  CHECK_SEQ(LINE(ed, 0), "ab *cd* ef");
  CHECK_IEQ(ed.cursor_col, 4);       /* caret stays on the start endpoint */
  CHECK_IEQ(ed.mark_col, 6);
  ed_teardown(&ed);
}

static void test_wrap_region_is_one_undo(void) {
  EditorState ed = {0};
  ed_load(&ed, "x bold y");
  ed.cursor_col = 2;
  buf_mark_set(&ed);
  ed.cursor_col = 6;                 /* region = "bold" */
  ed_wrap_region(&ed, "**", "**");
  CHECK_SEQ(LINE(ed, 0), "x **bold** y");
  undo_perform(&ed);                 /* one step removes both markers */
  CHECK_SEQ(LINE(ed, 0), "x bold y");
  ed_teardown(&ed);
}

static void test_wrap_region_multiline(void) {
  EditorState ed = {0};
  ed_load(&ed, "abc");
  buf_insert_line_at(&ed, 1, "def", 3);
  ed.cursor_line = 0; ed.cursor_col = 1;
  buf_mark_set(&ed);
  ed.cursor_line = 1; ed.cursor_col = 2;   /* region "bc\nde" */
  ed_wrap_region(&ed, "*", "*");
  CHECK_SEQ(LINE(ed, 0), "a*bc");
  CHECK_SEQ(LINE(ed, 1), "de*f");
  ed_teardown(&ed);
}

static void test_wrap_region_without_mark_is_noop(void) {
  EditorState ed = {0};
  ed_load(&ed, "hello");
  ed.cursor_col = 5;
  CHECK_IEQ(ed_wrap_region(&ed, "*", "*"), 0);
  CHECK_SEQ(LINE(ed, 0), "hello");
  ed_teardown(&ed);
}

static void test_wrap_region_empty_is_noop(void) {
  EditorState ed = {0};
  ed_load(&ed, "hello");
  ed.cursor_col = 3;
  buf_mark_set(&ed);                 /* mark == point: empty region */
  CHECK_IEQ(ed_wrap_region(&ed, "*", "*"), 0);
  CHECK_SEQ(LINE(ed, 0), "hello");
  ed_teardown(&ed);
}

/* ---- delete joins next line ---- */

static void test_delete_at_eol_joins_next(void) {
  EditorState ed = {0};
  ed_load(&ed, "foo");
  buf_insert_line_at(&ed, 1, "bar", 3);
  ed.cursor_line = 0; ed.cursor_col = 3;     /* end of "foo" */
  ed_delete(&ed);
  CHECK_IEQ(ed.line_count, 1);
  CHECK_SEQ(LINE(ed, 0), "foobar");
  ed_teardown(&ed);
}

/* ---- kill-line append / end-of-line join ---- */

/* A consecutive C-k (last_kill_was_k) appends to the kill buffer rather than
   replacing it. */
static void test_kill_line_appends_when_consecutive(void) {
  EditorState ed = {0};
  ed_load(&ed, "world"); ed.cursor_col = 0;
  ed.last_kill_was_k = 1;                     /* pretend a previous C-k */
  ed_emacs_kill_line(&ed);
  CHECK_SEQ(LINE(ed, 0), "");
  CHECK_SEQ(ed.kill_buf, "world");            /* appended onto an empty kill buf */
  ed_teardown(&ed);
}

/* C-k at end-of-line joins the next line and records a newline in the kill buf. */
static void test_kill_line_at_eol_sets_newline(void) {
  EditorState ed = {0};
  ed_load(&ed, "foo");
  buf_insert_line_at(&ed, 1, "bar", 3);
  ed.cursor_line = 0; ed.cursor_col = 3;
  ed.last_kill_was_k = 0;
  ed_emacs_kill_line(&ed);
  CHECK_IEQ(ed.line_count, 1);
  CHECK_SEQ(LINE(ed, 0), "foobar");
  CHECK_SEQ(ed.kill_buf, "\n");
  ed_teardown(&ed);
}

static void test_kill_line_at_eol_appends_newline(void) {
  EditorState ed = {0};
  ed_load(&ed, "foo");
  buf_insert_line_at(&ed, 1, "bar", 3);
  ed.cursor_line = 0; ed.cursor_col = 3;
  ed.last_kill_was_k = 1;                     /* consecutive → append */
  ed_emacs_kill_line(&ed);
  CHECK_SEQ(ed.kill_buf, "\n");
  ed_teardown(&ed);
}

/* ---- word motion across line boundaries ---- */

static void test_forward_word_crosses_line(void) {
  EditorState ed = {0};
  ed_load(&ed, "ab");
  buf_insert_line_at(&ed, 1, "cd", 2);
  ed.cursor_line = 0; ed.cursor_col = 2;      /* end of line 0 */
  ed_emacs_forward_word(&ed);
  CHECK_IEQ(ed.cursor_line, 1);
  CHECK_IEQ(ed.cursor_col, 0);
  ed_teardown(&ed);
}

static void test_backward_word_crosses_line(void) {
  EditorState ed = {0};
  ed_load(&ed, "ab");
  buf_insert_line_at(&ed, 1, "cd", 2);
  ed.cursor_line = 1; ed.cursor_col = 0;      /* start of line 1 */
  ed_emacs_backward_word(&ed);
  CHECK_IEQ(ed.cursor_line, 0);
  CHECK_IEQ(ed.cursor_col, 0);
  ed_teardown(&ed);
}

static void test_kill_word_backward(void) {
  EditorState ed = {0};
  ed_load(&ed, "alpha beta");                 /* caret at end */
  ed_emacs_kill_word_backward(&ed);
  CHECK_SEQ(LINE(ed, 0), "alpha ");           /* "beta" killed */
  ed_teardown(&ed);
}

/* Case change on a word already in the target case moves point past it without
   recording an undo step. */
static void test_case_word_noop_moves_point(void) {
  EditorState ed = {0};
  ed_load(&ed, "ABC"); ed.cursor_col = 0;
  ed_emacs_case_word(&ed, 0);                 /* upcase already-upper */
  CHECK_SEQ(LINE(ed, 0), "ABC");
  CHECK_IEQ(ed.cursor_col, 3);               /* advanced to end of word */
  ed_teardown(&ed);
}

/* ---- transpose at the two non-end positions ---- */

static void test_transpose_at_line_start(void) {
  EditorState ed = {0};
  ed_load(&ed, "abc"); ed.cursor_col = 0;     /* point at column 0 */
  ed_emacs_transpose_chars(&ed);
  CHECK_SEQ(LINE(ed, 0), "bac");
  ed_teardown(&ed);
}

static void test_transpose_midline(void) {
  EditorState ed = {0};
  ed_load(&ed, "abcd"); ed.cursor_col = 2;    /* between 'b' and 'c' */
  ed_emacs_transpose_chars(&ed);
  CHECK_SEQ(LINE(ed, 0), "acbd");
  ed_teardown(&ed);
}

/* Killing an empty region (mark == point) is a no-op that just clears the mark. */
static void test_kill_region_empty_is_noop(void) {
  EditorState ed = {0};
  ed_load(&ed, "abc"); ed.cursor_col = 1;
  buf_mark_set(&ed);                          /* mark == point at (0,1) */
  ed_emacs_kill_region(&ed);
  CHECK_SEQ(LINE(ed, 0), "abc");
  CHECK_IEQ(ed.mark_active, 0);
  ed_teardown(&ed);
}

/* ---- branch coverage: early-returns, no-ops, tab/space alternatives ---- */

static void test_dedent_removes_tab_indent(void) {
  EditorState ed = {0};
  ed_load(&ed, "\t- foo");          /* tab as the leading indent char */
  ed_dedent_line(&ed);
  CHECK_SEQ(LINE(ed, 0), "- foo");  /* the tab is removed */
  ed_teardown(&ed);
}

static void test_yank_empty_kill_is_noop(void) {
  EditorState ed = {0};
  buf_init_empty(&ed);
  ed_emacs_yank(&ed);               /* nothing in the kill buffer */
  CHECK_SEQ(LINE(ed, 0), "");
  ed_teardown(&ed);
}

static void test_replace_region_without_mark_is_noop(void) {
  EditorState ed = {0};
  ed_load(&ed, "abc");              /* no mark active */
  ed_replace_region(&ed, "X");
  CHECK_SEQ(LINE(ed, 0), "abc");
  ed_teardown(&ed);
}

static void test_kill_region_without_mark_is_noop(void) {
  EditorState ed = {0};
  ed_load(&ed, "abc");
  ed_emacs_kill_region(&ed);
  CHECK_SEQ(LINE(ed, 0), "abc");
  ed_teardown(&ed);
}

static void test_kill_word_forward_at_eof_is_noop(void) {
  EditorState ed = {0};
  ed_load(&ed, "abc");             /* single line, caret at end */
  ed_emacs_kill_word_forward(&ed);
  CHECK_SEQ(LINE(ed, 0), "abc");
  ed_teardown(&ed);
}

static void test_kill_word_backward_at_bof_is_noop(void) {
  EditorState ed = {0};
  ed_load(&ed, "abc"); ed.cursor_col = 0;
  ed_emacs_kill_word_backward(&ed);
  CHECK_SEQ(LINE(ed, 0), "abc");
  ed_teardown(&ed);
}

static void test_case_word_skips_leading_spaces(void) {
  EditorState ed = {0};
  ed_load(&ed, "  ab"); ed.cursor_col = 0;   /* point on the spaces */
  ed_emacs_case_word(&ed, 0);                 /* upcase the next word */
  CHECK_SEQ(LINE(ed, 0), "  AB");
  ed_teardown(&ed);
}

static void test_case_word_no_word_is_noop(void) {
  EditorState ed = {0};
  ed_load(&ed, "ab");              /* caret at end → no word ahead */
  ed_emacs_case_word(&ed, 0);
  CHECK_SEQ(LINE(ed, 0), "ab");
  ed_teardown(&ed);
}

static void test_transpose_too_short_is_noop(void) {
  EditorState ed = {0};
  ed_load(&ed, "a");              /* < 2 chars */
  ed_emacs_transpose_chars(&ed);
  CHECK_SEQ(LINE(ed, 0), "a");
  ed_teardown(&ed);
}

static void test_transpose_equal_chars_is_noop(void) {
  EditorState ed = {0};
  ed_load(&ed, "aa");            /* the two chars are identical */
  ed_emacs_transpose_chars(&ed);
  CHECK_SEQ(LINE(ed, 0), "aa");
  ed_teardown(&ed);
}

/* Cmd-Shift-H: wrap the caret's sentence in ==…==, and toggle it back off. */
static void test_toggle_sentence_highlight_wraps(void) {
  EditorState ed = {0};
  ed_load(&ed, "The cat sat. The dog ran.");
  ed.cursor_col = 4;                          /* inside the first sentence */
  CHECK_IEQ(ed_toggle_sentence_highlight(&ed), 1);
  CHECK_SEQ(LINE(ed, 0), "==The cat sat.== The dog ran.");
  /* toggling again (caret still in the first sentence) removes the markers */
  CHECK_IEQ(ed_toggle_sentence_highlight(&ed), 1);
  CHECK_SEQ(LINE(ed, 0), "The cat sat. The dog ran.");
  ed_teardown(&ed);
}

/* Only the caret's sentence is affected, and the wrap is one undo group. */
static void test_toggle_sentence_highlight_second_sentence_and_undo(void) {
  EditorState ed = {0};
  ed_load(&ed, "One. Two.");
  ed.cursor_col = 6;                          /* inside "Two." */
  ed_toggle_sentence_highlight(&ed);
  CHECK_SEQ(LINE(ed, 0), "One. ==Two.==");
  undo_perform(&ed);                          /* a single undo reverts the whole wrap */
  CHECK_SEQ(LINE(ed, 0), "One. Two.");
  ed_teardown(&ed);
}

/* A wrapped sentence adjacent to plain ones toggles off only its own markers. */
static void test_toggle_sentence_highlight_unwrap_amid_neighbors(void) {
  EditorState ed = {0};
  ed_load(&ed, "==One.== Two.");
  ed.cursor_col = 3;                          /* inside the highlighted "One." */
  CHECK_IEQ(ed_toggle_sentence_highlight(&ed), 1);
  CHECK_SEQ(LINE(ed, 0), "One. Two.");
  ed_teardown(&ed);
}

/* A sentence with no terminator (ends at the line end) wraps, and toggles back
   off cleanly — the trailing == is trimmed from the sentence bounds so the second
   toggle removes the markers instead of stacking another pair. */
static void test_toggle_sentence_highlight_no_terminator(void) {
  EditorState ed = {0};
  ed_load(&ed, "Hello world");           /* no . ! ? */
  ed.cursor_col = 3;
  CHECK_IEQ(ed_toggle_sentence_highlight(&ed), 1);
  CHECK_SEQ(LINE(ed, 0), "==Hello world==");
  CHECK_IEQ(ed_toggle_sentence_highlight(&ed), 1);
  CHECK_SEQ(LINE(ed, 0), "Hello world");   /* removed, not doubled */
  ed_teardown(&ed);
}

/* Cmd-Shift-U: the same toggle with ++underline++ markers, and it nests with an
   existing highlight (each marker toggles independently). */
static void test_toggle_sentence_underline_wraps_and_nests(void) {
  EditorState ed = {0};
  ed_load(&ed, "Just do it.");
  ed.cursor_col = 2;
  CHECK_IEQ(ed_toggle_sentence_underline(&ed), 1);
  CHECK_SEQ(LINE(ed, 0), "++Just do it.++");
  CHECK_IEQ(ed_toggle_sentence_underline(&ed), 1);      /* toggles back off */
  CHECK_SEQ(LINE(ed, 0), "Just do it.");
  /* nesting: underline a sentence that is already highlighted */
  ed_load(&ed, "==Wow.==");
  ed.cursor_col = 3;
  ed_toggle_sentence_underline(&ed);
  CHECK_SEQ(LINE(ed, 0), "==++Wow.++==");
  ed_toggle_sentence_underline(&ed);                    /* removes only the ++ */
  CHECK_SEQ(LINE(ed, 0), "==Wow.==");
  ed_teardown(&ed);
}

/* No sentence text under the caret (empty line) → no-op. */
static void test_toggle_sentence_highlight_empty_is_noop(void) {
  EditorState ed = {0};
  ed_load(&ed, "");
  CHECK_IEQ(ed_toggle_sentence_highlight(&ed), 0);
  CHECK_SEQ(LINE(ed, 0), "");
  ed_teardown(&ed);
}

void suite_editing(void) {
  RUN(test_insert_into_empty);
  RUN(test_insert_midline);
  RUN(test_insert_utf8_is_byte_indexed);
  RUN(test_backspace_removes_whole_codepoint);
  RUN(test_delete_removes_whole_codepoint);
  RUN(test_backspace_in_line);
  RUN(test_backspace_joins_lines);
  RUN(test_enter_splits_line);
  RUN(test_enter_continues_bullet);
  RUN(test_enter_continues_numbered);
  RUN(test_enter_continues_indented_bullet);
  RUN(test_enter_continues_indented_numbered);
  RUN(test_indent_inserts_two_spaces);
  RUN(test_dedent_removes_indent);
  RUN(test_dedent_noop_at_margin);
  RUN(test_indent_then_dedent_roundtrip);
  RUN(test_kill_line_to_eol);
  RUN(test_copy_region_single_line);
  RUN(test_copy_region_multiline);
  RUN(test_delete_region_removes_selection);
  RUN(test_delete_region_noop_without_mark);
  RUN(test_delete_region_multiline);
  RUN(test_readonly_blocks_edits);
  RUN(test_readonly_blocks_section_join);
  RUN(test_readonly_boundary_tracks_edits);
  RUN(test_yank_inserts_kill_buffer);
  RUN(test_yank_multiline_splits);
  RUN(test_copy_then_yank_roundtrip);
  RUN(test_region_dup_returns_marked_text);
  RUN(test_replace_region_swaps_in_text);
  RUN(test_replace_region_multiline_is_one_undo);
  RUN(test_wrap_region_single_line);
  RUN(test_wrap_region_preserves_inner_selection);
  RUN(test_wrap_region_cursor_at_start_orientation);
  RUN(test_wrap_region_is_one_undo);
  RUN(test_wrap_region_multiline);
  RUN(test_wrap_region_without_mark_is_noop);
  RUN(test_wrap_region_empty_is_noop);
  RUN(test_toggle_sentence_highlight_wraps);
  RUN(test_toggle_sentence_highlight_second_sentence_and_undo);
  RUN(test_toggle_sentence_highlight_unwrap_amid_neighbors);
  RUN(test_toggle_sentence_highlight_no_terminator);
  RUN(test_toggle_sentence_underline_wraps_and_nests);
  RUN(test_toggle_sentence_highlight_empty_is_noop);
  RUN(test_delete_at_eol_joins_next);
  RUN(test_kill_line_appends_when_consecutive);
  RUN(test_kill_line_at_eol_sets_newline);
  RUN(test_kill_line_at_eol_appends_newline);
  RUN(test_forward_word_crosses_line);
  RUN(test_backward_word_crosses_line);
  RUN(test_kill_word_backward);
  RUN(test_case_word_noop_moves_point);
  RUN(test_transpose_at_line_start);
  RUN(test_transpose_midline);
  RUN(test_kill_region_empty_is_noop);
  RUN(test_dedent_removes_tab_indent);
  RUN(test_yank_empty_kill_is_noop);
  RUN(test_replace_region_without_mark_is_noop);
  RUN(test_kill_region_without_mark_is_noop);
  RUN(test_kill_word_forward_at_eof_is_noop);
  RUN(test_kill_word_backward_at_bof_is_noop);
  RUN(test_case_word_skips_leading_spaces);
  RUN(test_case_word_no_word_is_noop);
  RUN(test_transpose_too_short_is_noop);
  RUN(test_transpose_equal_chars_is_noop);
}
