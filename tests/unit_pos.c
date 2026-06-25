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

/* The fake tags a known sentence into the expected sparse span map. */
static void test_tag_basic(void) {
  /*            0         1         2
                012345678901234567890 */
  Line l = mkline("The cat and dog run");
  const PosSpan *sp;
  int n = pos_line_spans(&l, &sp);
  CHECK_IEQ(n, 4);                 /* "The" is POS_OTHER, no span */
  /* cat=noun, and=conj, dog=noun, run=verb */
  CHECK_IEQ(sp[0].start, 4);  CHECK_IEQ(sp[0].end, 7);  CHECK_IEQ(sp[0].cls, POS_NOUN);
  CHECK_IEQ(sp[1].start, 8);  CHECK_IEQ(sp[1].cls, POS_CONJUNCTION);
  CHECK_IEQ(sp[2].cls, POS_NOUN);
  CHECK_IEQ(sp[3].cls, POS_VERB);
  CHECK_IEQ(class_at(&l, 5), POS_NOUN);    /* inside "cat" */
  CHECK_IEQ(class_at(&l, 0), POS_OTHER);   /* inside "The" */
  CHECK_IEQ(class_at(&l, 3), POS_OTHER);   /* the space */
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
  Line l = mkline("the cat runs");   /* cat=noun, runs=verb */
  Color c;

  /* mask 0 = highlighting off */
  CHECK_IEQ(pos_color_at(&l, 0, 5, &c), 0);

  /* nouns only: the noun colors, the verb does not */
  unsigned int nouns = POS_BIT(POS_NOUN);
  CHECK_IEQ(pos_color_at(&l, nouns, 5, &c), 1);    /* inside "cat" */
  CHECK_IEQ(pos_color_at(&l, nouns, 9, &c), 0);    /* inside "runs" (verb, masked off) */

  /* all classes: both color */
  CHECK_IEQ(pos_color_at(&l, SYNTAX_MASK_ALL, 5, &c), 1);
  CHECK_IEQ(pos_color_at(&l, SYNTAX_MASK_ALL, 9, &c), 1);

  /* a column in no span never colors */
  CHECK_IEQ(pos_color_at(&l, SYNTAX_MASK_ALL, 0, &c), 0);
  freeline(&l);
}

/* The class palette is defined for the colored classes and distinct. */
static void test_class_colors(void) {
  Color noun, verb, adj, adv, conj, other;
  CHECK_IEQ(pos_class_color(POS_NOUN, &noun), 1);
  CHECK_IEQ(pos_class_color(POS_VERB, &verb), 1);
  CHECK_IEQ(pos_class_color(POS_ADJECTIVE, &adj), 1);
  CHECK_IEQ(pos_class_color(POS_ADVERB, &adv), 1);
  CHECK_IEQ(pos_class_color(POS_CONJUNCTION, &conj), 1);
  CHECK_IEQ(pos_class_color(POS_OTHER, &other), 0);   /* no color for OTHER */
  /* distinct hues: noun vs verb at least differ */
  CHECK(noun.r != verb.r || noun.g != verb.g || noun.b != verb.b);
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
  RUN(test_class_colors);
  RUN(test_empty_line);
}
