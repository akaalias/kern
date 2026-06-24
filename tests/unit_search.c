/* unit_search.c — unit tests for navigation.c incremental search. */
#include <stdio.h>
#include <string.h>
#include "test.h"
#include "ed_fixture.h"
#include "navigation.h"

static void load_lines(EditorState *ed, const char **lines, int n) {
  buf_init_empty(ed);
  buf_free_all_lines(ed);                 /* line_count -> 0, array kept */
  for (int i = 0; i < n; i++)
    buf_insert_line_at(ed, i, lines[i], (int)strlen(lines[i]));
}

static void set_query(ViewState *vs, const char *q, int dir) {
  vs->search_active = 1;
  vs->search_direction = dir;
  snprintf(vs->search_buf, sizeof vs->search_buf, "%s", q);
  vs->search_len = (int)strlen(q);
  vs->search_match_line = -1;
}

static const char *DOC[] = { "foo bar", "baz foo", "qux" };

static void test_search_forward_first_and_next(void) {
  EditorState ed = {0}; ViewState vs = {0}; vs.content_h = 600;
  load_lines(&ed, DOC, 3);
  set_query(&vs, "foo", 1);

  nav_search_find_first(&ed, &vs);
  CHECK_IEQ(vs.search_match_line, 0);
  CHECK_IEQ(vs.search_match_col, 0);
  CHECK_IEQ(ed.cursor_line, 0);

  nav_search_find_current_dir(&ed, &vs);   /* next match */
  CHECK_IEQ(vs.search_match_line, 1);
  CHECK_IEQ(vs.search_match_col, 4);

  nav_search_find_current_dir(&ed, &vs);   /* wraps back to the first */
  CHECK_IEQ(vs.search_match_line, 0);
  CHECK_IEQ(vs.search_match_col, 0);
  ed_teardown(&ed);
}

static void test_search_is_case_insensitive(void) {
  EditorState ed = {0}; ViewState vs = {0}; vs.content_h = 600;
  load_lines(&ed, DOC, 3);
  set_query(&vs, "FOO", 1);
  nav_search_find_first(&ed, &vs);
  CHECK_IEQ(vs.search_match_line, 0);
  CHECK_IEQ(vs.search_match_col, 0);
  ed_teardown(&ed);
}

static void test_search_not_found(void) {
  EditorState ed = {0}; ViewState vs = {0}; vs.content_h = 600;
  load_lines(&ed, DOC, 3);
  set_query(&vs, "zzz", 1);
  nav_search_find_first(&ed, &vs);
  CHECK_IEQ(vs.search_match_line, -1);
  ed_teardown(&ed);
}

static void test_search_backward(void) {
  EditorState ed = {0}; ViewState vs = {0}; vs.content_h = 600;
  load_lines(&ed, DOC, 3);
  set_query(&vs, "foo", -1);
  nav_search_find_first(&ed, &vs);          /* from the bottom up */
  CHECK_IEQ(vs.search_match_line, 1);
  CHECK_IEQ(vs.search_match_col, 4);

  nav_search_find_current_dir(&ed, &vs);    /* previous match */
  CHECK_IEQ(vs.search_match_line, 0);
  CHECK_IEQ(vs.search_match_col, 0);
  ed_teardown(&ed);
}

void suite_search(void) {
  RUN(test_search_forward_first_and_next);
  RUN(test_search_is_case_insensitive);
  RUN(test_search_not_found);
  RUN(test_search_backward);
}
