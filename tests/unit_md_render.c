/* unit_md_render.c — unit tests for Editor/md_render.c inline formatting,
 * via the capture stub renderer. Focus: styles carry across a visual-row
 * boundary (the multi-row span bug) and the ==highlight== span. */
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include "test.h"
#include "buffer.h"
#include "md_render.h"
#include "pos_render.h"
#include "stub_renderer.h"

static Color GREY;

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

/* md_is_list_item recognizes "- " and "N. ", including under leading indent. */
static void test_is_list_item(void) {
  Line a = mkline("- top");        CHECK_IEQ(md_is_list_item(&a), 1); freeline(&a);
  Line b = mkline("  - nested");   CHECK_IEQ(md_is_list_item(&b), 1); freeline(&b);
  Line c = mkline("3. numbered");  CHECK_IEQ(md_is_list_item(&c), 1); freeline(&c);
  Line d = mkline("plain text");   CHECK_IEQ(md_is_list_item(&d), 0); freeline(&d);
}

/* md_heading_prefix_len counts the hashes plus the single trailing space. */
static void test_heading_prefix_len(void) {
  Line h2 = mkline("## Title");    CHECK_IEQ(md_heading_prefix_len(&h2), 3); freeline(&h2);
  Line h1 = mkline("# Title");     CHECK_IEQ(md_heading_prefix_len(&h1), 2); freeline(&h1);
  Line nn = mkline("not heading"); CHECK_IEQ(md_heading_prefix_len(&nn), 0); freeline(&nn);
}

/* List metrics recognize a tab as the leading indent char. */
static void test_list_metrics_with_tab_indent(void) {
  Line a = mkline("\t- x");
  CHECK(md_list_indent(&a) > 0);
  CHECK(md_list_marker_width(&a) > 0);
  CHECK_IEQ(md_is_list_item(&a), 1);
  freeline(&a);
}

/* md_col_x for a column at/after the window end returns the trailing pen x. */
static void test_col_x_at_end(void) {
  Line l = mkline("ab");
  CHECK_IEQ(md_col_x(&l, 0, 2, 0, 0, 2), 20);   /* two 10px glyphs */
  freeline(&l);
}

/* Drawing a window that begins past the first span exercises the span-skip
   loop (si advanced before the draw). */
static void test_draw_window_after_first_span(void) {
  stub_reset();
  draw_window("**a** **b**", 6, 11);            /* second "**b**" only */
  CHECK_IEQ(stub_text_count, 5);                /* '*','*','b','*','*' */
}

/* Inside a heading, an italic span renders bold (headings are all-bold). */
static void test_italic_in_heading_is_bold(void) {
  stub_reset();
  Line l = mkline("_x_");
  int out = -1;
  md_draw_text(&l, 0, 3, 0, 0, GREY, 1 /* heading */, -1, &out, 1);
  CHECK_IEQ(stub_texts[1].style, FONT_BOLD);    /* the 'x' content glyph */
  freeline(&l);
}

/* ---- typewriter focus dim (md_set_text_opacity / md_fade) ---- */

/* Opacity < 1 scales the alpha of drawn glyphs so non-focused lines fade toward
   the background. Base color GREY (a=255) at 0.4 → ~102. */
static void test_dim_scales_text_alpha(void) {
  stub_reset();
  md_set_text_opacity(0.4f);
  draw_window("abc", 0, 3);
  md_set_text_opacity(1.0f);                  /* reset — global, would leak otherwise */
  CHECK_IEQ(stub_text_count, 3);
  for (int i = 0; i < stub_text_count; i++)
    CHECK_IEQ(stub_texts[i].color.a, 102);
}

/* Full opacity (the default) leaves glyph alpha untouched. */
static void test_full_opacity_keeps_alpha(void) {
  stub_reset();
  md_set_text_opacity(1.0f);
  draw_window("abc", 0, 3);
  CHECK_IEQ(stub_texts[0].color.a, 255);
}

/* The dim also fades background rects — here the ==highlight== bg (a=70 → ~28). */
static void test_dim_scales_bg_rect_alpha(void) {
  stub_reset();
  md_set_text_opacity(0.4f);
  draw_window("==hi==", 0, 6);                 /* 2 highlight bg rects */
  md_set_text_opacity(1.0f);
  CHECK_IEQ(stub_rect_count, 2);
  for (int i = 0; i < stub_rect_count; i++)
    CHECK_IEQ(stub_rects[i].color.a, 28);
}

static int approx(float a, float b) { return fabsf(a - b) < 1e-4f; }

/* Focus crossfade: at t=1 only the focused line is full, others (incl. the line
   just left) are dim; at t=0 the just-left line is still full and the new focus
   is still dim; the two cross symmetrically at t=0.5. */
static void test_focus_opacity_settled(void) {
  CHECK(approx(md_focus_opacity(5, 5, 4, 1.0f), 1.0f));              /* focused, full */
  CHECK(approx(md_focus_opacity(4, 5, 4, 1.0f), FOCUS_DIM_OPACITY)); /* left line, dim */
  CHECK(approx(md_focus_opacity(9, 5, 4, 1.0f), FOCUS_DIM_OPACITY)); /* unrelated, dim */
}
static void test_focus_opacity_start_of_transition(void) {
  CHECK(approx(md_focus_opacity(5, 5, 4, 0.0f), FOCUS_DIM_OPACITY)); /* new focus starts dim */
  CHECK(approx(md_focus_opacity(4, 5, 4, 0.0f), 1.0f));             /* left line starts full */
}
static void test_focus_opacity_crosses_midway(void) {
  float a = md_focus_opacity(5, 5, 4, 0.5f);   /* rising */
  float b = md_focus_opacity(4, 5, 4, 0.5f);   /* falling */
  CHECK(a > FOCUS_DIM_OPACITY && a < 1.0f);
  CHECK(b > FOCUS_DIM_OPACITY && b < 1.0f);
  CHECK(approx(a, b));                          /* symmetric crossfade */
}

/* Syntax highlighting isolates: a shown class keeps its bright ramp value while
   everything else (here the noun "cat", off) drops to the muted ground, so the
   shown class (verb "runs") pops. */
static Color glyph_color(const char *ch) {
  for (int i = 0; i < stub_text_count; i++)
    if (strcmp(stub_texts[i].ch, ch) == 0) return stub_texts[i].color;
  Color none = { 0, 0, 0, 0 };
  return none;
}
static void test_syntax_isolate_mutes_ground(void) {
  stub_reset();
  Line l = mkline("the cat runs");           /* det / noun / verb (fake tagger) */
  md_set_syntax_mask(POS_BIT(POS_VERB));      /* isolate verbs */
  int out = -1;
  md_draw_text(&l, 0, l.len, 0, 0, color(204, 200, 195, 255), 0, -1, &out, 1);
  md_set_syntax_mask(0);

  Color verb = glyph_color("u");              /* 'u' only in "runs"  */
  Color noun = glyph_color("c");              /* 'c' only in "cat"   */
  Color mute = pos_mute_color();
  CHECK_IEQ(verb.r, 254);                     /* shown class keeps its ramp value */
  CHECK_IEQ(noun.r, mute.r);                  /* off class drops to the ground    */
  CHECK(verb.r > noun.r);                     /* so the verb pops                 */
  CHECK(mute.r < 112);                        /* ground sits below function words */
  free(l.pos_spans);
  freeline(&l);
}

void suite_md_render(void) {
  GREY = color(200, 200, 200, 255);
  RUN(test_syntax_isolate_mutes_ground);
  RUN(test_focus_opacity_settled);
  RUN(test_focus_opacity_start_of_transition);
  RUN(test_focus_opacity_crosses_midway);
  RUN(test_dim_scales_text_alpha);
  RUN(test_full_opacity_keeps_alpha);
  RUN(test_dim_scales_bg_rect_alpha);
  RUN(test_bold_carries_into_tail_window);
  RUN(test_markers_use_base_style);
  RUN(test_bold_full_line);
  RUN(test_highlight_draws_bg_per_content_glyph);
  RUN(test_wikilink_draws_bg_for_token);
  RUN(test_col_x_is_linear);
  RUN(test_is_list_item);
  RUN(test_heading_prefix_len);
  RUN(test_list_metrics_with_tab_indent);
  RUN(test_col_x_at_end);
  RUN(test_draw_window_after_first_span);
  RUN(test_italic_in_heading_is_bold);
}
