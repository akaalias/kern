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

/* ---- branch coverage ---- */

static void test_cursor_clamp_all_directions(void) {
  EditorState ed = {0}; ed_load(&ed, "ab");   /* one line, len 2 */
  ed.cursor_line = -1; nav_cursor_clamp(&ed); CHECK_IEQ(ed.cursor_line, 0);
  ed.cursor_line = 99; nav_cursor_clamp(&ed); CHECK_IEQ(ed.cursor_line, 0);
  ed.cursor_col = -1;  nav_cursor_clamp(&ed); CHECK_IEQ(ed.cursor_col, 0);
  ed.cursor_col = 99;  nav_cursor_clamp(&ed); CHECK_IEQ(ed.cursor_col, 2);
  ed_teardown(&ed);
}

/* With no content height set, ensure-visible falls back to the window height. */
static void test_ensure_visible_without_content_h(void) {
  stub_set_metrics(10, 20, 800, 600);
  EditorState ed = {0}; buf_init_empty(&ed);
  ViewState vs = {0};                          /* content_h == 0 */
  nav_ensure_cursor_visible(&ed, &vs);
  CHECK(vs.scroll_y >= 0);                      /* did not divide-by-bogus */
  ed_teardown(&ed);
}

/* Normal mode: after scrolling down then moving the caret back to the first
   line, scroll_y must rest at the top page margin (negative) so the margin
   reappears — not snap flush to 0. Regression for the vanishing top margin. */
static void test_ensure_visible_restores_top_margin(void) {
  stub_set_metrics(10, 20, 800, 600);            /* line height 30 */
  EditorState ed = {0}; ed_load(&ed, "line 0");
  for (int i = 1; i < 40; i++) buf_insert_line_at(&ed, i, "x", 1);
  ViewState vs = {0};
  vs.content_h = 600; vs.font_size = 24.0f;       /* top margin = 8 * 24 = 192 */
  vs.scroll_y = 300;                              /* scrolled well down */
  ed.cursor_line = 0; ed.cursor_col = 0;          /* caret back at the top */
  nav_ensure_cursor_visible(&ed, &vs);
  CHECK(vs.scroll_y < 0);                          /* margin shown, not flush at 0 */
  CHECK_IEQ((int)vs.scroll_y, -nav_top_margin(&vs));
  ed_teardown(&ed);
}

/* Click on the first row of a wrapped line takes the "more rows follow" branch. */
static void test_click_on_wrapped_line(void) {
  stub_set_metrics(10, 20, 800, 600);          /* 70 glyphs per row */
  char s[101]; memset(s, 'a', 100); s[100] = '\0';
  EditorState ed = {0}; ed_load(&ed, s);       /* wraps to 2 rows */
  ViewState vs = {0}; vs.content_y = 80; vs.content_h = 600;
  nav_click_to_cursor(&ed, &vs, 85, 85);       /* first visual row */
  CHECK_IEQ(ed.cursor_line, 0);
  CHECK(ed.cursor_col < 70);                    /* landed within the first row */
  ed_teardown(&ed);
}

/* Click on a list item must account for the hanging indent the renderer draws
   the row at (page margin + 4-space list indent). The pre-fix code measured
   from the bare page margin, so clicks landed offset by the indent on every
   list line. page margin 50 + list indent 40 → row origin x = 90; 10px glyphs. */
static void test_click_on_list_line_accounts_for_indent(void) {
  stub_set_metrics(10, 20, 800, 600);
  EditorState ed = {0}; ed_load(&ed, "- hello world");
  ViewState vs = {0}; vs.content_y = 0; vs.content_h = 600;

  nav_click_to_cursor(&ed, &vs, 90, 5);          /* at the row origin → col 0 */
  CHECK_IEQ(ed.cursor_col, 0);

  nav_click_to_cursor(&ed, &vs, 90 + 35, 5);     /* 3.5 glyphs into the text */
  CHECK_IEQ(ed.cursor_col, 4);
  ed_teardown(&ed);
}

/* Vertical movement moves by VISUAL row, not logical line: inside a wrapped
   paragraph, down/up stay within the same logical line until its rows are
   exhausted. This is the bug where C-n jumped a whole paragraph. */
static void test_visual_move_stays_in_wrapped_line(void) {
  stub_set_metrics(10, 20, 800, 600);           /* 70 glyphs per row */
  char s[141]; memset(s, 'a', 140); s[140] = '\0';   /* logical line 0 wraps to 2 rows */
  EditorState ed = {0}; ed_load(&ed, s);
  buf_insert_line_at(&ed, 1, "bbbb", 4);        /* a short logical line below */
  ViewState vs = {0}; vs.content_h = 600; vs.goal_line = -1;

  ed.cursor_line = 0; ed.cursor_col = 5; ed.cursor_target_col = 5;
  nav_visual_move(&ed, &vs, +1);                /* down one visual row */
  CHECK_IEQ(ed.cursor_line, 0);                 /* still the same logical line */
  CHECK(ed.cursor_col >= 70);                   /* now on its continuation row */
  CHECK_IEQ(ed.cursor_col, 75);                 /* same goal x (col 5 of the row) */
  ed_teardown(&ed);
}

/* The pixel goal column persists across a run of vertical moves, so up-then-down
   returns to the original column. */
static void test_visual_move_keeps_goal_x(void) {
  stub_set_metrics(10, 20, 800, 600);
  char s[141]; memset(s, 'a', 140); s[140] = '\0';   /* one logical line, 2 rows */
  EditorState ed = {0}; ed_load(&ed, s);
  ViewState vs = {0}; vs.content_h = 600; vs.goal_line = -1;

  ed.cursor_line = 0; ed.cursor_col = 75; ed.cursor_target_col = 75;  /* col 5 of row 1 */
  nav_visual_move(&ed, &vs, -1);                /* up to row 0 */
  CHECK_IEQ(ed.cursor_col, 5);
  nav_visual_move(&ed, &vs, +1);                /* down to row 1 again */
  CHECK_IEQ(ed.cursor_col, 75);                 /* goal x preserved */
  ed_teardown(&ed);
}

/* Up from the first visual row lands at the buffer start; down from the last
   lands at the buffer end. */
static void test_visual_move_clamps_to_buffer_ends(void) {
  stub_set_metrics(10, 20, 800, 600);
  EditorState ed = {0}; ed_load(&ed, "one");
  buf_insert_line_at(&ed, 1, "two", 3);
  ViewState vs = {0}; vs.content_h = 600; vs.goal_line = -1;

  ed.cursor_line = 0; ed.cursor_col = 2; ed.cursor_target_col = 2;
  nav_visual_move(&ed, &vs, -1);                /* above the top */
  CHECK_IEQ(ed.cursor_line, 0);
  CHECK_IEQ(ed.cursor_col, 0);

  ed.cursor_line = 1; ed.cursor_col = 1; ed.cursor_target_col = 1; vs.goal_line = -1;
  nav_visual_move(&ed, &vs, +1);                /* below the bottom */
  CHECK_IEQ(ed.cursor_line, 1);
  CHECK_IEQ(ed.cursor_col, 3);                  /* end of "two" */
  ed_teardown(&ed);
}

/* A list item's continuation rows hang under the item text, so they fit fewer
   characters than a flush paragraph — the wrap measurement must match the
   render (which the old full-page-width measurement did not). */
static void test_wrap_breaks_account_for_list_indent(void) {
  stub_set_metrics(10, 20, 800, 600);           /* page width 700 = 70 glyphs */
  /* "- " + 100 a's: a flush line wraps at 70; a list line's first row holds
     fewer, since indent (40) + marker (20) shrink the continuation width. */
  char s[120]; s[0] = '-'; s[1] = ' '; memset(s + 2, 'a', 100); s[102] = '\0';
  Line l = mkline(s);
  int starts[16];
  int rows = nav_get_wrap_breaks(&l, starts, 16);
  CHECK(rows >= 2);
  /* first row origin is margin+indent (40); continuation hangs at +marker (60),
     so neither row spans the full 70 glyphs a flush line would. */
  CHECK(starts[1] <= 66);                        /* row 0 holds ≤66 glyphs (700-40)/10 */
  free(l.text);
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
  RUN(test_cursor_clamp_all_directions);
  RUN(test_ensure_visible_without_content_h);
  RUN(test_ensure_visible_restores_top_margin);
  RUN(test_click_on_wrapped_line);
  RUN(test_click_on_list_line_accounts_for_indent);
  RUN(test_visual_move_stays_in_wrapped_line);
  RUN(test_visual_move_keeps_goal_x);
  RUN(test_visual_move_clamps_to_buffer_ends);
  RUN(test_wrap_breaks_account_for_list_indent);
}
