/* pos_render.c — per-line POS span cache + class colors (see pos_render.h). */

#include <stdlib.h>
#include "pos_render.h"

/* Class palette, tuned to read like iA Writer's syntax mode on a dark page:
   nouns warm orange, verbs blue, adjectives amber, adverbs violet,
   conjunctions green. Indexed by PosClass; POS_OTHER has no entry. */
static const Color k_class_color[POS_CLASS_COUNT] = {
  [POS_NOUN]        = { 206, 124,  92, 255 },
  [POS_VERB]        = { 110, 150, 205, 255 },
  [POS_ADJECTIVE]   = { 206, 170,  92, 255 },
  [POS_ADVERB]      = { 176, 132, 206, 255 },
  [POS_CONJUNCTION] = { 132, 182, 120, 255 },
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
