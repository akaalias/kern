/* unit_sub.c — tests for sub_render.c: text→symbol substitution detection
 * (arrows, relations, dashes, smart quotes, symbols), longest-match, category
 * masking, the lazy cache, and — most importantly — that the three width walks
 * (md_draw_text, md_x_to_col, nav_get_wrap_breaks) agree on a substituted
 * token's drawn width, so caret-x, click-to-cursor, and wrapping stay in step. */
#include <stdlib.h>
#include <string.h>
#include "test.h"
#include "buffer.h"
#include "renderer.h"
#include "gfx.h"
#include "md_render.h"
#include "navigation.h"
#include "sub_render.h"
#include "stub_renderer.h"

static Line mkline(const char *s) {
  Line l;
  line_init(&l, s, (int)strlen(s));
  return l;
}
static void freeline(Line *l) { free(l->text); free(l->md_spans); free(l->sub_spans); }

/* the substitution span starting exactly at byte `col`, or NULL (mask = all). */
static const SubSpan *span_at(Line *l, int col) {
  const SubSpan *sp;
  int n = sub_line_spans(l, &sp);
  for (int i = 0; i < n; i++) if (sp[i].start == col) return &sp[i];
  return NULL;
}
static int glyph_is(const SubSpan *s, const char *g) {
  return s && strcmp(s->glyph, g) == 0;
}

/* ---- detection ---- */

static void test_arrow(void) {
  /*            0123456 */
  Line l = mkline("a -> b");
  const SubSpan *sp;
  int n = sub_line_spans(&l, &sp);
  CHECK_IEQ(n, 1);
  CHECK_IEQ(sp[0].start, 2);
  CHECK_IEQ(sp[0].len, 2);
  CHECK_IEQ(sp[0].category, SUB_ARROW);
  CHECK(glyph_is(&sp[0], "→"));
  freeline(&l);
}

static void test_relations(void) {
  Line a = mkline("x != y");  CHECK(glyph_is(span_at(&a, 2), "≠")); freeline(&a);
  Line b = mkline("x <= y");  CHECK(glyph_is(span_at(&b, 2), "≤")); freeline(&b);
  Line c = mkline("x >= y");  CHECK(glyph_is(span_at(&c, 2), "≥")); freeline(&c);
  Line d = mkline("x ~= y");  CHECK(glyph_is(span_at(&d, 2), "≈")); freeline(&d);
  Line e = mkline("a <- b");  CHECK(glyph_is(span_at(&e, 2), "←")); freeline(&e);
  Line f = mkline("a => b");  CHECK(glyph_is(span_at(&f, 2), "⇒")); freeline(&f);
}

/* "<=>" must win over the "<=" prefix (longest match). */
static void test_longest_match(void) {
  Line l = mkline("p <=> q");
  const SubSpan *s = span_at(&l, 2);
  CHECK(glyph_is(s, "⇔"));
  CHECK_IEQ(s->len, 3);
  /* and only one span — the "<=" prefix did not also fire */
  const SubSpan *sp; CHECK_IEQ(sub_line_spans(&l, &sp), 1);
  freeline(&l);
}

/* a doubled hyphen is an em dash; a lone hyphen between digits is an en dash;
   a hyphen between letters is left alone. */
static void test_dashes(void) {
  Line em = mkline("yes--no");
  CHECK(glyph_is(span_at(&em, 3), "—"));
  CHECK_IEQ(span_at(&em, 3)->len, 2);
  freeline(&em);

  /*            01234567 */
  Line en = mkline("1995-2026");
  const SubSpan *s = span_at(&en, 4);
  CHECK(glyph_is(s, "–"));
  CHECK_IEQ(s->len, 1);
  freeline(&en);

  Line word = mkline("set-up");     /* between letters: no substitution */
  const SubSpan *sp; CHECK_IEQ(sub_line_spans(&word, &sp), 0);
  freeline(&word);
}

/* a quote curls left at a boundary and right otherwise — the latter also gives
   the correct apostrophe in contractions. */
static void test_smart_quotes(void) {
  /*            0123456 */
  Line q = mkline("\"hi\" x");        /* "hi" */
  CHECK(glyph_is(span_at(&q, 0), "“"));   /* opening double */
  CHECK(glyph_is(span_at(&q, 3), "”"));   /* closing double */
  freeline(&q);

  Line apo = mkline("don't");
  CHECK(glyph_is(span_at(&apo, 3), "’")); /* apostrophe = right single */
  freeline(&apo);

  Line sq = mkline("a 'b' c");
  CHECK(glyph_is(span_at(&sq, 2), "‘"));  /* opening single after space */
  CHECK(glyph_is(span_at(&sq, 4), "’"));  /* closing single */
  freeline(&sq);
}

static void test_symbols(void) {
  Line a = mkline("(c) 2026");  CHECK(glyph_is(span_at(&a, 0), "©")); freeline(&a);
  Line b = mkline("Kern(tm)");  CHECK(glyph_is(span_at(&b, 4), "™")); freeline(&b);
  Line c = mkline("(r) mark");  CHECK(glyph_is(span_at(&c, 0), "®")); freeline(&c);
  Line d = mkline("wait...");   CHECK(glyph_is(span_at(&d, 4), "…")); freeline(&d);
  Line e = mkline("3 +- 1");    CHECK(glyph_is(span_at(&e, 2), "±")); freeline(&e);
  /* "(cat)" must NOT match "(c)" — literal token only */
  Line f = mkline("(cat)");     const SubSpan *sp; CHECK_IEQ(sub_line_spans(&f, &sp), 0); freeline(&f);
}

/* ---- masking ---- */

static void test_mask(void) {
  Line l = mkline("a -> b == c");   /* "->" is ARROW */
  CHECK(sub_at(&l, 0, 2) == NULL);                       /* mask off → nothing */
  CHECK(sub_at(&l, SUB_BIT(SUB_ARROW), 2) != NULL);      /* arrow bit → match */
  CHECK(sub_at(&l, SUB_BIT(SUB_PUNCT), 2) == NULL);      /* wrong category → no */
  freeline(&l);
}

/* ---- cache lifecycle ---- */

static void test_cache(void) {
  Line l = mkline("a -> b");
  CHECK_IEQ(l.sub_span_count, -1);          /* not yet computed */
  const SubSpan *sp; sub_line_spans(&l, &sp);
  CHECK_IEQ(l.sub_span_count, 1);           /* cached */
  line_dirty(&l);
  CHECK_IEQ(l.sub_span_count, -1);          /* invalidated by edit */
  freeline(&l);
}

/* ---- geometry: the three walks must agree ---- */

/* md_col_x (caret-x) and md_x_to_col (click) round-trip at token boundaries; a
   caret inside the collapsed token resolves to its left edge. Stub glyph widths
   are by byte: 'a'/' '/'b' = 10, "→" (3 bytes) = 30. */
static void test_caret_click_parity(void) {
  stub_reset();
  sub_set_mask(SUB_MASK_ALL);
  sub_set_caret(NULL, -1);     /* caret elsewhere: token stays collapsed */
  Line l = mkline("a -> b");   /* cols: a=0 ' '=1 -=2 >=3 ' '=4 b=5 */

  /* caret-x: col 2 and the interior col 3 both sit at the glyph's left edge */
  CHECK_IEQ(md_col_x(&l, 0, 6, 0, 0, 0), 0);
  CHECK_IEQ(md_col_x(&l, 0, 6, 0, 0, 2), 20);   /* left of "→" */
  CHECK_IEQ(md_col_x(&l, 0, 6, 0, 0, 3), 20);   /* interior → left edge */
  CHECK_IEQ(md_col_x(&l, 0, 6, 0, 0, 4), 50);   /* right of "→" (20 + 30) */
  CHECK_IEQ(md_col_x(&l, 0, 6, 0, 0, 6), 70);

  /* click: x maps back to a token boundary, never the interior col 3 */
  CHECK_IEQ(md_x_to_col(&l, 0, 6, 0, 0, 0),  0);
  CHECK_IEQ(md_x_to_col(&l, 0, 6, 0, 0, 20), 2);
  CHECK_IEQ(md_x_to_col(&l, 0, 6, 0, 0, 50), 4);
  CHECK_IEQ(md_x_to_col(&l, 0, 6, 0, 0, 70), 6);

  sub_set_mask(0);
  freeline(&l);
}

/* reveal-on-contact: with the caret on the token, the walks measure and draw the
   literal source — so the token is fully editable and the caret can sit between
   its bytes (col 3 becomes reachable at x=30, not collapsed to the glyph edge). */
static void test_reveal_on_contact(void) {
  stub_reset();
  sub_set_mask(SUB_MASK_ALL);
  Line l = mkline("a -> b");           /* token "->" at [2,4) */
  sub_set_caret(&l, 3);                /* caret inside the token → reveal */

  /* literal widths now: every byte is its own 10px cell */
  CHECK_IEQ(md_col_x(&l, 0, 6, 0, 0, 2), 20);
  CHECK_IEQ(md_col_x(&l, 0, 6, 0, 0, 3), 30);   /* between '-' and '>' (was 20) */
  CHECK_IEQ(md_col_x(&l, 0, 6, 0, 0, 4), 40);
  CHECK_IEQ(md_x_to_col(&l, 0, 6, 0, 0, 30), 3); /* click lands mid-token now */

  /* and the draw emits the literal bytes, not the glyph */
  int out = -1;
  md_draw_text(&l, 0, 6, 0, 0, color(200, 200, 200, 255), 0, -1, &out, 1);
  int saw_arrow = 0, saw_hyphen = 0;
  for (int i = 0; i < stub_text_count; i++) {
    if (strcmp(stub_texts[i].ch, "→") == 0) saw_arrow = 1;
    if (strcmp(stub_texts[i].ch, "-") == 0)  saw_hyphen = 1;
  }
  CHECK(!saw_arrow);
  CHECK(saw_hyphen);

  /* reveal is scoped to the caret's line: a different line stays collapsed */
  CHECK_IEQ(sub_reveal_col(&l), 3);
  Line other = mkline("x -> y");
  CHECK_IEQ(sub_reveal_col(&other), -1);
  CHECK_IEQ(md_col_x(&other, 0, 6, 0, 0, 4), 50);   /* collapsed: 20 + glyph 30 */
  freeline(&other);

  sub_set_caret(NULL, -1);
  sub_set_mask(0);
  freeline(&l);
}

/* the draw pass emits the replacement glyph, not the source bytes. */
static void test_draw_emits_glyph(void) {
  stub_reset();
  sub_set_mask(SUB_MASK_ALL);
  sub_set_caret(NULL, -1);
  Line l = mkline("a -> b");
  int out = -1;
  md_draw_text(&l, 0, 6, 0, 0, color(200, 200, 200, 255), 0, -1, &out, 1);
  int saw_arrow = 0, saw_hyphen = 0;
  for (int i = 0; i < stub_text_count; i++) {
    if (strcmp(stub_texts[i].ch, "→") == 0) saw_arrow = 1;
    if (strcmp(stub_texts[i].ch, "-") == 0)  saw_hyphen = 1;
  }
  CHECK(saw_arrow);
  CHECK(!saw_hyphen);
  sub_set_mask(0);
  freeline(&l);
}

/* wrapping measures the literal text, so it is identical whether symbols are on
   or off — a collapsed glyph never reflows the line, which is what lets
   reveal-on-contact redraw the literal without shifting wrap points. */
static void test_wrap_is_substitution_independent(void) {
  stub_reset();
  char buf[512]; buf[0] = '\0';
  for (int i = 0; i < 40; i++) strcat(buf, "ab -> cd ");
  Line l = mkline(buf);

  sub_set_mask(0);
  int off = nav_count_wraps(&l);
  line_dirty(&l);
  sub_set_mask(SUB_MASK_ALL);
  int starts[256];
  int on = nav_get_wrap_breaks(&l, starts, 256);

  CHECK(off > 1);            /* it really does wrap */
  CHECK_IEQ(on, off);        /* symbols on/off → same wrap (literal-measured) */

  /* word-wrap breaks at spaces, so none lands inside an arrow token */
  const SubSpan *sp; int ns = sub_line_spans(&l, &sp);
  for (int r = 1; r < on; r++) {
    for (int s = 0; s < ns; s++)
      CHECK(!(starts[r] > sp[s].start && starts[r] < sp[s].start + sp[s].len));
  }
  sub_set_mask(0);
  freeline(&l);
}

void suite_sub(void) {
  RUN(test_arrow);
  RUN(test_relations);
  RUN(test_longest_match);
  RUN(test_dashes);
  RUN(test_smart_quotes);
  RUN(test_symbols);
  RUN(test_mask);
  RUN(test_cache);
  RUN(test_caret_click_parity);
  RUN(test_reveal_on_contact);
  RUN(test_draw_emits_glyph);
  RUN(test_wrap_is_substitution_independent);
}
