/* pos_render.c — per-line POS span cache + class colors (see pos_render.h). */

#include <stdlib.h>
#include "pos_render.h"

/* Value, not hue. The Tufte alternative to iA's five-hue rainbow: no color at
   all, only luminance — a strict descending ramp by grammatical importance:
       verb > noun > adjective > adverb  >>  function words.
   Verbs (predication) sit brightest, then nouns, then the two modifier classes
   stepping down, and the function-word glue (and/the/of/to/it…) drops well below
   the base text grey so it recedes. Each level keeps the base's warm-grey tint
   (g = r-4, b = r-9, as in 204,200,195). What you read is the prose's content
   rhythm — figure vs ground — not a label per color. Indexed by PosClass;
   POS_OTHER (numbers, etc.) has no entry and stays at the base color. */
static const Color k_class_color[POS_CLASS_COUNT] = {
  /* content — a descending ramp forward of / around the text grey. Verbs get a
     strong lift clear of nouns, and nouns a strong lift clear of the modifiers,
     so the two load-bearing classes each stand out on their own. */
  [POS_VERB]        = { 254, 250, 245, 255 },   /* brightest: the action (near max) */
  [POS_NOUN]        = { 200, 196, 191, 255 },   /* the substance            */
  [POS_ADJECTIVE]   = { 160, 156, 151, 255 },   /* modifies a noun          */
  [POS_ADVERB]      = { 140, 136, 131, 255 },   /* modifies a verb          */
  /* function — the glue, well below the text so it recedes */
  [POS_CONJUNCTION] = { 112, 108, 103, 255 },
  [POS_DETERMINER]  = { 112, 108, 103, 255 },
  [POS_PREPOSITION] = { 112, 108, 103, 255 },
  [POS_PRONOUN]     = { 112, 108, 103, 255 },
  [POS_PARTICLE]    = { 112, 108, 103, 255 },
};

int pos_class_color(PosClass cls, Color *out) {
  if (cls <= POS_OTHER || cls >= POS_CLASS_COUNT) return 0;
  *out = k_class_color[cls];
  return 1;
}

int pos_class_enabled(unsigned int syntax_mask, PosClass cls) {
  return (syntax_mask & POS_BIT(cls)) != 0;
}

int pos_line_spans(Line *l, const PosSpan **out) {
  if (l->pos_span_count < 0) {
    free(l->pos_spans);
    l->pos_spans = NULL;
    PosSpan scratch[POS_MAX_SPANS];
    int n = pos_tag_line(l->text, l->len, scratch, POS_MAX_SPANS);
    /* Post-correction: the tagger reads a gerund right after a determiner as a
       verb ("a heading"), but English puts a noun there, not a finite verb.
       Retag any Verb whose nearest preceding non-adjective span is a Determiner
       (so "a heading", "the meeting", "an interesting opening" all become nouns). */
    for (int i = 1; i < n; i++) {
      if (scratch[i].cls != POS_VERB) continue;
      int j = i - 1;
      while (j >= 0 && scratch[j].cls == POS_ADJECTIVE) j--;
      if (j >= 0 && scratch[j].cls == POS_DETERMINER) scratch[i].cls = POS_NOUN;
    }
    if (n > 0) {
      l->pos_spans = malloc((size_t)n * sizeof(PosSpan));
      if (l->pos_spans) {
        for (int i = 0; i < n; i++) l->pos_spans[i] = scratch[i];
      } else {
        n = 0;   /* allocation failed: cache an empty map, render uncolored */
      }
    }
    l->pos_span_count = n;
  }
  *out = l->pos_spans;
  return l->pos_span_count;
}

int pos_color_at(Line *l, unsigned int syntax_mask, int col, Color *out) {
  if (!syntax_mask) return 0;
  const PosSpan *spans;
  int n = pos_line_spans(l, &spans);
  /* spans are ordered and non-overlapping; a small linear scan is plenty for a
     single line's worth of words. */
  for (int i = 0; i < n; i++) {
    if (col < spans[i].start) break;          /* past the column, none cover it */
    if (col < spans[i].end) {
      PosClass cls = (PosClass)spans[i].cls;
      if (!pos_class_enabled(syntax_mask, cls)) return 0;
      return pos_class_color(cls, out);
    }
  }
  return 0;
}
