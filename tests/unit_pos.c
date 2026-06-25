/* unit_pos.c — tests for the POS span cache + color mapping (pos_render.c),
 * driven through the deterministic fake tagger (pos_tagger_fake.c). */
#include <stdlib.h>
#include <string.h>
#include "test.h"
#include "buffer.h"
#include "pos_render.h"

static Line mkline(const char *s) {
  Line l;
  line_init(&l, s, (int)strlen(s));
  return l;
}
static void freeline(Line *l) { free(l->text); free(l->md_spans); free(l->pos_spans); }

/* class covering byte `col`, or POS_OTHER if none. */
static PosClass class_at(Line *l, int col) {
  const PosSpan *sp;
  int n = pos_line_spans(l, &sp);
  for (int i = 0; i < n; i++)
    if (col >= sp[i].start && col < sp[i].end) return (PosClass)sp[i].cls;
  return POS_OTHER;
}

/* The fake tags a known sentence into the expected sparse span map — including
   the function words (determiner "The"), which the value palette will dim. */
static void test_tag_basic(void) {
  /*            0         1
                0123456789012345678 */
  Line l = mkline("The cat and dog run");
  const PosSpan *sp;
  int n = pos_line_spans(&l, &sp);
  CHECK_IEQ(n, 5);
  /* The=det, cat=noun, and=conj, dog=noun, run=verb */
  CHECK_IEQ(sp[0].start, 0);  CHECK_IEQ(sp[0].end, 3);  CHECK_IEQ(sp[0].cls, POS_DETERMINER);
  CHECK_IEQ(sp[1].start, 4);  CHECK_IEQ(sp[1].end, 7);  CHECK_IEQ(sp[1].cls, POS_NOUN);
  CHECK_IEQ(sp[2].start, 8);  CHECK_IEQ(sp[2].cls, POS_CONJUNCTION);
  CHECK_IEQ(sp[3].cls, POS_NOUN);
  CHECK_IEQ(sp[4].cls, POS_VERB);
  CHECK_IEQ(class_at(&l, 5), POS_NOUN);        /* inside "cat" */
  CHECK_IEQ(class_at(&l, 0), POS_DETERMINER);  /* inside "The" */
  CHECK_IEQ(class_at(&l, 3), POS_OTHER);       /* the space */
  /* "The" is a function word; "cat" is not */
  CHECK(POS_IS_FUNCTION(POS_DETERMINER));
  CHECK(!POS_IS_FUNCTION(POS_NOUN));
  freeline(&l);
}

/* The span map is cached and reused until the line is dirtied. */
static void test_cache_and_invalidate(void) {
  Line l = mkline("quick brown fox");
  const PosSpan *a, *b;
  pos_line_spans(&l, &a);
  pos_line_spans(&l, &b);
  CHECK(a == b);                   /* second call returns the cached array */

  line_dirty(&l);
  CHECK_IEQ(l.pos_span_count, -1); /* dirty invalidates the cache */
  const PosSpan *c;
  int n = pos_line_spans(&l, &c);
  CHECK_IEQ(n, 3);                 /* quick=adj, brown=adj, fox=noun */
  freeline(&l);
}

/* pos_color_at honors the mask: a disabled class yields no color. */
static void test_color_masking(void) {
  /*            0123456789012 */
  Line l = mkline("the cat runs");   /* the=det, cat=noun, runs=verb */
  Color c;

  /* mask 0 = highlighting off */
  CHECK_IEQ(pos_color_at(&l, 0, 5, &c), 0);

  /* nouns only: the noun colors, the verb and determiner do not */
  unsigned int nouns = POS_BIT(POS_NOUN);
  CHECK_IEQ(pos_color_at(&l, nouns, 5, &c), 1);    /* inside "cat" */
  CHECK_IEQ(pos_color_at(&l, nouns, 9, &c), 0);    /* inside "runs" (verb, masked off) */
  CHECK_IEQ(pos_color_at(&l, nouns, 0, &c), 0);    /* inside "the" (determiner, masked off) */

  /* all classes: content and function alike resolve a color */
  CHECK_IEQ(pos_color_at(&l, SYNTAX_MASK_ALL, 5, &c), 1);   /* cat */
  CHECK_IEQ(pos_color_at(&l, SYNTAX_MASK_ALL, 9, &c), 1);   /* runs */
  CHECK_IEQ(pos_color_at(&l, SYNTAX_MASK_ALL, 0, &c), 1);   /* the (function word now tagged) */

  /* a column in no span (a space) never colors */
  CHECK_IEQ(pos_color_at(&l, SYNTAX_MASK_ALL, 3, &c), 0);
  freeline(&l);
}

/* The palette is a value ramp, not hues: a strict descending order by
   importance — verb > noun > adjective > adverb >> function words. */
static void test_value_ordering(void) {
  Color verb, noun, adj, adv, conj, det, other;
  CHECK_IEQ(pos_class_color(POS_VERB, &verb), 1);
  CHECK_IEQ(pos_class_color(POS_NOUN, &noun), 1);
  CHECK_IEQ(pos_class_color(POS_ADJECTIVE, &adj), 1);
  CHECK_IEQ(pos_class_color(POS_ADVERB, &adv), 1);
  CHECK_IEQ(pos_class_color(POS_CONJUNCTION, &conj), 1);
  CHECK_IEQ(pos_class_color(POS_DETERMINER, &det), 1);
  CHECK_IEQ(pos_class_color(POS_OTHER, &other), 0);   /* no entry for OTHER */

  /* strict descending ramp */
  CHECK(verb.r > noun.r);
  CHECK(noun.r > adj.r);
  CHECK(adj.r  > adv.r);
  CHECK(adv.r  > conj.r);              /* all content above function */
  CHECK(verb.r > 204);                 /* top of ramp forward of base text */
  CHECK(conj.r < 204);                 /* function recedes below base text */
  /* every function word shares the dimmest value */
  CHECK(conj.r == det.r && conj.g == det.g && conj.b == det.b);
}

/* A Verb right after a determiner is retagged a Noun (the "a heading" fix). The
   fake tags "built" as a verb; the determiner rule should override it. */
static void test_determiner_fixes_gerund(void) {
  /*            0123456789 */
  Line l = mkline("the built");        /* the=det, built=verb -> noun */
  CHECK_IEQ(class_at(&l, 4), POS_NOUN);
  freeline(&l);

  /* an adjective between the determiner and the verb is skipped */
  /*             0123456789012345 */
  Line l2 = mkline("the simple built"); /* det adj verb -> noun */
  CHECK_IEQ(class_at(&l2, 11), POS_NOUN);
  freeline(&l2);

  /* without a preceding determiner, the verb stays a verb */
  Line l3 = mkline("cat built");        /* noun verb -> unchanged */
  CHECK_IEQ(class_at(&l3, 4), POS_VERB);
  freeline(&l3);
}

/* An empty line tags to nothing without misbehaving. */
static void test_empty_line(void) {
  Line l = mkline("");
  const PosSpan *sp;
  CHECK_IEQ(pos_line_spans(&l, &sp), 0);
  Color c;
  CHECK_IEQ(pos_color_at(&l, SYNTAX_MASK_ALL, 0, &c), 0);
  freeline(&l);
}

void suite_pos(void) {
  RUN(test_tag_basic);
  RUN(test_cache_and_invalidate);
  RUN(test_color_masking);
  RUN(test_value_ordering);
  RUN(test_determiner_fixes_gerund);
  RUN(test_empty_line);
}
