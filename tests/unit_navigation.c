/* unit_navigation.c — unit tests for Editor/navigation.c word wrapping,
 * against the deterministic stub renderer (10px glyphs, 800px window →
 * page width 700px = 70 glyphs per row). */
#include <stdlib.h>
#include <string.h>
#include "test.h"
#include "ed_fixture.h"
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

/* nav_maybe_reflow invalidates wraps only when the page width changes. */
static void test_reflow_only_on_width_change(void) {
  stub_set_metrics(10, 20, 800, 600);          /* page width = 700 */
  EditorState ed = {0}; buf_init_empty(&ed);
  ViewState vs = {0};                           /* wrap_page_w = 0 */

  /* first call: width differs from the (zero) cache → reflows */
  CHECK_IEQ(nav_maybe_reflow(&ed, &vs), 1);
  CHECK_IEQ(vs.wrap_page_w, 700);

  /* warm the wrap cache, then a same-width call must be a no-op that leaves
     the cache intact */
  (void)nav_total_visual_lines(&ed);
  CHECK(ed.lines[0].wrap_count >= 0);
  CHECK_IEQ(nav_maybe_reflow(&ed, &vs), 0);
  CHECK(ed.lines[0].wrap_count >= 0);           /* not invalidated */

  /* a width change reflows and invalidates */
  stub_set_metrics(10, 20, 400, 600);           /* page width = 360 */
  CHECK_IEQ(nav_maybe_reflow(&ed, &vs), 1);
  CHECK_IEQ(vs.wrap_page_w, 360);
  CHECK_IEQ(ed.lines[0].wrap_count, -1);        /* invalidated */

  stub_set_metrics(10, 20, 800, 600);           /* restore default for other tests */
  ed_teardown(&ed);
}

/* Regression: wrap measurement must ignore the renderer's ambient font style.
   The frame ends with the status bar's FONT_MONO active; an event-time wrap
   recompute (ensure-cursor-visible after an edit) used to cache mono-measured
   wrap counts that disagreed with the FONT_REGULAR render pass, producing a
   phantom unwrapped row and mis-placing click-to-cursor. nav_get_wrap_breaks
   and nav_count_wraps must both measure in the body font regardless. */
static void test_wrap_count_ignores_ambient_font(void) {
  stub_set_style_extra(FONT_MONO, 5);          /* mono is 15px/glyph vs 10px body */

  char s[141];
  memset(s, 'a', 140);
  s[140] = '\0';                               /* 140 body glyphs → exactly 2 rows */
  Line l = mkline(s);

  /* populate the cache while MONO is active (simulating post-status-bar state) */
  r_set_font_style(FONT_MONO);
  int cached = nav_count_wraps(&l);

  /* the render pass measures fresh in REGULAR */
  r_set_font_style(FONT_REGULAR);
  int starts[16];
  int rendered = nav_get_wrap_breaks(&l, starts, 16);

  /* both must agree on the body-font wrap (2 rows). Measured in mono this line
     would span 3 rows — the stale-cache bug that produced phantom wrap rows. */
  CHECK_IEQ(rendered, 2);
  CHECK_IEQ(cached, rendered);

  free(l.text);
  stub_reset();                                /* clear the style-extra knob */
}

static void test_win_metrics(void) {
  stub_set_metrics(10, 20, 800, 600);
  CHECK_IEQ(nav_win_w(), 800);
  CHECK_IEQ(nav_win_h(), 600);
}

/* A visual index past the end clamps to the last logical line, offset 0. */
static void test_visual_to_logical_past_end(void) {
  EditorState ed = {0}; buf_init_empty(&ed);
  int off = -1;
  int ln = nav_visual_to_logical(&ed, 999, &off);
  CHECK_IEQ(ln, ed.line_count - 1);
  CHECK_IEQ(off, 0);
  ed_teardown(&ed);
}

/* nav_click_to_cursor: page margin 50, glyph 10px, line height 30 at 800x600. */
static void test_click_to_cursor(void) {
  stub_set_metrics(10, 20, 800, 600);
  EditorState ed = {0}; ed_load(&ed, "hello world");
  ViewState vs = {0}; vs.content_y = 80; vs.content_h = 600; vs.scroll_y = 0;

  /* click inside the line: x=85 lands between columns (margin 50 + ~3.5 glyphs) */
  nav_click_to_cursor(&ed, &vs, 85, 85);
  CHECK_IEQ(ed.cursor_line, 0);
  CHECK_IEQ(ed.cursor_col, 4);

  /* click left of the page margin → column 0 */
  nav_click_to_cursor(&ed, &vs, 10, 85);
  CHECK_IEQ(ed.cursor_col, 0);

  /* click far below the last row clamps to the last visual line */
  nav_click_to_cursor(&ed, &vs, 85, 99999);
  CHECK_IEQ(ed.cursor_line, 0);

  /* click above the content area clamps the visual line up to 0 */
  nav_click_to_cursor(&ed, &vs, 85, 0);
  CHECK_IEQ(ed.cursor_line, 0);

  ed_teardown(&ed);
}

void suite_navigation(void) {
  RUN(test_wrap_empty_line);
  RUN(test_reflow_only_on_width_change);
  RUN(test_wrap_short_line_one_row);
  RUN(test_wrap_exactly_page_width_one_row);
  RUN(test_wrap_breaks_midword_without_space);
  RUN(test_wrap_breaks_at_last_space);
  RUN(test_count_wraps_caches);
  RUN(test_wrap_count_ignores_ambient_font);
  RUN(test_win_metrics);
  RUN(test_visual_to_logical_past_end);
  RUN(test_click_to_cursor);
}
