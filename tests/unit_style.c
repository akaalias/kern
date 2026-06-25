/* unit_style.c — tests for style_check.c: curated filler/redundancy detection,
 * word-boundary safety, multi-word spans, masking, and the lazy cache. */
#include <stdlib.h>
#include <string.h>
#include "test.h"
#include "buffer.h"
#include "style_check.h"

static Line mkline(const char *s) {
  Line l;
  line_init(&l, s, (int)strlen(s));
  return l;
}
static void freeline(Line *l) { free(l->text); free(l->md_spans); free(l->style_spans); }

/* category covering byte `col`, or STYLE_NONE if none. */
static StyleCategory cat_at(Line *l, int col) {
  const StyleSpan *sp;
  int n = style_line_spans(l, &sp);
  for (int i = 0; i < n; i++)
    if (col >= sp[i].start && col < sp[i].end) return (StyleCategory)sp[i].category;
  return STYLE_NONE;
}

/* A single filler word is flagged over its exact byte range. */
static void test_filler(void) {
  /*            0         1         2
                0123456789012345678901234 */
  Line l = mkline("I felt really productive");
  const StyleSpan *sp;
  int n = style_line_spans(&l, &sp);
  CHECK_IEQ(n, 1);
  CHECK_IEQ(sp[0].start, 7);  CHECK_IEQ(sp[0].end, 13);   /* whole word "really" */
  CHECK_IEQ(sp[0].category, STYLE_FILLER);
  CHECK_IEQ(sp[0].decor, STYLE_DECOR_STRIKE);
  CHECK_IEQ(cat_at(&l, 9), STYLE_FILLER);
  CHECK_IEQ(cat_at(&l, 2), STYLE_NONE);   /* "felt" is fine */
  freeline(&l);
}

/* A redundancy strikes only its cuttable word; the kept word stays unmarked. */
static void test_redundancy_phrase(void) {
  /*            0         1
                0123456789012345678 */
  Line l = mkline("an added bonus here");
  const StyleSpan *sp;
  int n = style_line_spans(&l, &sp);
  CHECK_IEQ(n, 1);
  CHECK_IEQ(sp[0].start, 3);   CHECK_IEQ(sp[0].end, 8);   /* just "added" */
  CHECK_IEQ(sp[0].category, STYLE_REDUNDANCY);
  CHECK_IEQ(sp[0].decor, STYLE_DECOR_STRIKE);
  CHECK_IEQ(cat_at(&l, 5), STYLE_REDUNDANCY);    /* inside "added" (struck) */
  CHECK_IEQ(cat_at(&l, 11), STYLE_NONE);         /* "bonus" is kept */
  CHECK_IEQ(cat_at(&l, 16), STYLE_NONE);         /* "here" */

  /* an "X and Y" doublet cuts its redundant side: "each and" struck, "every" kept */
  /*             0         1
                 012345678901234 */
  Line d = mkline("each and every");
  CHECK_IEQ(cat_at(&d, 0), STYLE_REDUNDANCY);    /* "each" struck */
  CHECK_IEQ(cat_at(&d, 9), STYLE_NONE);          /* "every" kept */
  freeline(&d);
  freeline(&l);
}

/* Matching is whole-word: "just" inside "adjust" must not flag. */
static void test_word_boundary(void) {
  /*            0123456789012 */
  Line l = mkline("adjust just");
  CHECK_IEQ(cat_at(&l, 0), STYLE_NONE);    /* inside "adjust" */
  CHECK_IEQ(cat_at(&l, 8), STYLE_FILLER);  /* the standalone "just" */
  freeline(&l);
}

/* Clichés (a long phrase) and generalizations (a single word) both flag. */
static void test_cliche(void) {
  /*            0         1         2
                012345678901234567890123 */
  Line l = mkline("Obviously, at the end of the day we win");
  const StyleSpan *sp;
  int n = style_line_spans(&l, &sp);
  CHECK_IEQ(n, 2);
  CHECK_IEQ(sp[0].start, 0);   CHECK_IEQ(sp[0].end, 9);   /* "Obviously" */
  CHECK_IEQ(sp[0].category, STYLE_CLICHE);
  CHECK_IEQ(sp[0].decor, STYLE_DECOR_UNDERLINE);          /* clichés underline */
  CHECK_IEQ(sp[1].category, STYLE_CLICHE);                /* whole "at the end of the day" */
  CHECK_IEQ(sp[1].decor, STYLE_DECOR_UNDERLINE);
  CHECK_IEQ(cat_at(&l, 14), STYLE_CLICHE);                /* inside the phrase */
  freeline(&l);

  /* hyphens are token separators, so "low-hanging fruit" matches the pattern */
  Line h = mkline("grab the low-hanging fruit");
  CHECK_IEQ(cat_at(&h, 11), STYLE_CLICHE);   /* inside "low-hanging fruit" */
  freeline(&h);
}

/* style_decor_at honors the mask: a disabled category isn't decorated. */
static void test_masking(void) {
  /*            0         1
                012345678901234567 */
  Line l = mkline("really added bonus");  /* filler + redundancy ("added") */

  CHECK_IEQ(style_decor_at(&l, 0, 2), STYLE_DECOR_NONE);                 /* off */

  unsigned int fillers = STYLE_BIT(STYLE_FILLER);
  CHECK_IEQ(style_decor_at(&l, fillers, 2), STYLE_DECOR_STRIKE);         /* "really" */
  CHECK_IEQ(style_decor_at(&l, fillers, 9), STYLE_DECOR_NONE);           /* redundancy off */

  CHECK_IEQ(style_decor_at(&l, STYLE_MASK_ALL, 2), STYLE_DECOR_STRIKE);  /* "really" */
  CHECK_IEQ(style_decor_at(&l, STYLE_MASK_ALL, 9), STYLE_DECOR_STRIKE);  /* "added" cut word */
  CHECK_IEQ(style_decor_at(&l, STYLE_MASK_ALL, 15), STYLE_DECOR_NONE);   /* "bonus" kept */
  CHECK_IEQ(style_decor_at(&l, STYLE_MASK_ALL, 6), STYLE_DECOR_NONE);    /* the space */
  freeline(&l);
}

/* The span map is cached and reused until the line is dirtied. */
static void test_cache_and_invalidate(void) {
  Line l = mkline("really very basic");
  const StyleSpan *a, *b;
  style_line_spans(&l, &a);
  style_line_spans(&l, &b);
  CHECK(a == b);
  line_dirty(&l);
  CHECK_IEQ(l.style_span_count, -1);
  const StyleSpan *c;
  int n = style_line_spans(&l, &c);
  CHECK_IEQ(n, 2);                 /* "really" + "very" ("basic" alone is fine) */
  freeline(&l);
}

/* A clean line and an empty line produce no spans. */
static void test_no_false_positives(void) {
  Line l = mkline("The quick brown fox jumps.");
  const StyleSpan *sp;
  CHECK_IEQ(style_line_spans(&l, &sp), 0);
  freeline(&l);

  Line e = mkline("");
  CHECK_IEQ(style_line_spans(&e, &sp), 0);
  CHECK_IEQ(style_decor_at(&e, STYLE_MASK_ALL, 0), STYLE_DECOR_NONE);
  freeline(&e);
}

void suite_style(void) {
  RUN(test_filler);
  RUN(test_redundancy_phrase);
  RUN(test_cliche);
  RUN(test_word_boundary);
  RUN(test_masking);
  RUN(test_cache_and_invalidate);
  RUN(test_no_false_positives);
}
