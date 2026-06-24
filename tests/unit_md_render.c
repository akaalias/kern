/* unit_md_render.c — unit tests for Editor/md_render.c inline formatting,
 * via the capture stub renderer. Focus: styles carry across a visual-row
 * boundary (the multi-row span bug) and the ==highlight== span. */
#include <stdlib.h>
#include <string.h>
#include "test.h"
#include "buffer.h"
#include "md_render.h"
#include "stub_renderer.h"

static mu_Color GREY;

static Line mkline(const char *s) {
  Line l;
  line_init(&l, s, (int)strlen(s));
  return l;
}
static void freeline(Line *l) { free(l->text); free(l->md_spans); }

static void draw_window(const char *t, int start, int end) {
  Line l = mkline(t);
  int out = -1;
  md_draw_text(&l, start, end, 0, 0, GREY, 0, -1, &out, 1);
  freeline(&l);
}

/* The bug: a span that opened on a previous visual row lost its style. Drawing
 * only the tail window [5,8) of "**abcdef**" must still render those glyphs
 * bold, because parsing starts from the logical-line start. */
static void test_bold_carries_into_tail_window(void) {
  stub_reset();
  draw_window("**abcdef**", 5, 8);       /* glyphs d,e,f — inside the bold span */
  CHECK_IEQ(stub_text_count, 3);
  for (int i = 0; i < stub_text_count; i++)
    CHECK_IEQ(stub_texts[i].style, FONT_BOLD);
}

/* Delimiter glyphs themselves render in the base (non-bold) style. */
static void test_markers_use_base_style(void) {
  stub_reset();
  draw_window("**abcdef**", 0, 2);       /* the leading "**" */
  CHECK_IEQ(stub_text_count, 2);
  for (int i = 0; i < stub_text_count; i++)
    CHECK_IEQ(stub_texts[i].style, FONT_REGULAR);
}

/* Whole bold span drawn in one window: 4 dim markers + 6 bold content glyphs. */
static void test_bold_full_line(void) {
  stub_reset();
  draw_window("**abcdef**", 0, 10);
  CHECK_IEQ(stub_text_count, 10);
  CHECK_IEQ(stub_texts[0].style, FONT_REGULAR);  /* '*' */
  CHECK_IEQ(stub_texts[2].style, FONT_BOLD);     /* 'a' */
  CHECK_IEQ(stub_texts[7].style, FONT_BOLD);     /* 'f' */
  CHECK_IEQ(stub_texts[8].style, FONT_REGULAR);  /* '*' */
}

/* ==highlight== paints a background rect behind each content glyph only. */
static void test_highlight_draws_bg_per_content_glyph(void) {
  stub_reset();
  draw_window("==hi==", 0, 6);            /* content = h,i */
  CHECK_IEQ(stub_rect_count, 2);
  CHECK_IEQ(stub_text_count, 6);
}

/* A wikilink renders its whole token with a background rect. */
static void test_wikilink_draws_bg_for_token(void) {
  stub_reset();
  draw_window("[[Note]]", 0, 8);
  CHECK_IEQ(stub_rect_count, 8);
}

/* Measurement is linear in the stub (every glyph 10px), independent of style. */
static void test_col_x_is_linear(void) {
  Line l = mkline("**abcdef**");
  CHECK_IEQ(md_col_x(&l, 0, 10, 0, 0, 5), 50);
  freeline(&l);
}

void suite_md_render(void) {
  GREY = mu_color(200, 200, 200, 255);
  RUN(test_bold_carries_into_tail_window);
  RUN(test_markers_use_base_style);
  RUN(test_bold_full_line);
  RUN(test_highlight_draws_bg_per_content_glyph);
  RUN(test_wikilink_draws_bg_for_token);
  RUN(test_col_x_is_linear);
}
