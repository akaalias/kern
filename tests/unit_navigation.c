/* unit_navigation.c — unit tests for Editor/navigation.c word wrapping,
 * against the deterministic stub renderer (10px glyphs, 800px window →
 * page width 700px = 70 glyphs per row). */
#include <stdlib.h>
#include <string.h>
#include "test.h"
#include "buffer.h"
#include "navigation.h"
#include "stub_renderer.h"

static Line mkline(const char *s) {
  Line l;
  line_init(&l, s, (int)strlen(s));
  return l;
}

static void test_wrap_empty_line(void) {
  Line l = mkline("");
  int starts[8];
  int n = nav_get_wrap_breaks(&l, starts, 8);
  CHECK_IEQ(n, 1);
  CHECK_IEQ(starts[0], 0);
  free(l.text);
}

static void test_wrap_short_line_one_row(void) {
  Line l = mkline("hello world");
  int starts[8];
  CHECK_IEQ(nav_get_wrap_breaks(&l, starts, 8), 1);
  free(l.text);
}

static void test_wrap_exactly_page_width_one_row(void) {
  char s[71];
  memset(s, 'a', 70);
  s[70] = '\0';                      /* 70 glyphs == 700px == page width */
  Line l = mkline(s);
  int starts[8];
  CHECK_IEQ(nav_get_wrap_breaks(&l, starts, 8), 1);
  free(l.text);
}

static void test_wrap_breaks_midword_without_space(void) {
  char s[72];
  memset(s, 'a', 71);
  s[71] = '\0';                      /* 71 glyphs → spills to a 2nd row */
  Line l = mkline(s);
  int starts[8];
  int n = nav_get_wrap_breaks(&l, starts, 8);
  CHECK_IEQ(n, 2);
  CHECK_IEQ(starts[1], 70);          /* no space → break at the glyph */
  free(l.text);
}

static void test_wrap_breaks_at_last_space(void) {
  char s[82];
  memset(s, 'a', 40);
  s[40] = ' ';
  memset(s + 41, 'b', 40);
  s[81] = '\0';                      /* "aaa…(40) bbb…(40)" */
  Line l = mkline(s);
  int starts[8];
  int n = nav_get_wrap_breaks(&l, starts, 8);
  CHECK_IEQ(n, 2);
  CHECK_IEQ(starts[1], 41);          /* break just after the space */
  free(l.text);
}

static void test_count_wraps_caches(void) {
  char s[72];
  memset(s, 'a', 71);
  s[71] = '\0';
  Line l = mkline(s);
  CHECK_IEQ(l.wrap_count, -1);       /* fresh line: dirty */
  CHECK_IEQ(nav_count_wraps(&l), 2);
  CHECK_IEQ(l.wrap_count, 2);        /* now cached */
  CHECK_IEQ(nav_count_wraps(&l), 2); /* second call hits the cache */
  free(l.text);
}

void suite_navigation(void) {
  RUN(test_wrap_empty_line);
  RUN(test_wrap_short_line_one_row);
  RUN(test_wrap_exactly_page_width_one_row);
  RUN(test_wrap_breaks_midword_without_space);
  RUN(test_wrap_breaks_at_last_space);
  RUN(test_count_wraps_caches);
}
