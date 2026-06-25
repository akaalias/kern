/* style_check.h — flags cuttable words/phrases for strike-through, iA-Writer's
 * "Style Check". A third per-line span layer alongside md_spans (markdown) and
 * pos_spans (part of speech): style_spans mark text the writer could cut, drawn
 * dimmed with a strike-through bar in md_draw_text.
 *
 * Pure C — string/phrase matching against curated lists, no tagger or renderer
 * dependency — so the whole thing is headless-testable. Per-line spans are
 * lazily cached on the Line and invalidated by line_dirty, like the other two. */
#ifndef STYLE_CHECK_H
#define STYLE_CHECK_H

#include "editor_types.h"

/* Cuttable categories. The enum values double as bit indices for
   ViewState.style_mask. (Clichés and a user Custom list are planned additions.) */
typedef enum {
  STYLE_NONE = 0,
  STYLE_FILLER,        /* hedges/intensifiers that weaken prose: very, just, … */
  STYLE_REDUNDANCY,    /* phrases that say one thing twice: added bonus, … */
  STYLE_CLICHE,        /* tired phrases & sweeping generalizations: at the end
                          of the day, think outside the box, always, everyone … */
  STYLE_CATEGORY_COUNT
} StyleCategory;

/* How a span is drawn, chosen by the action the category implies:
   - STRIKE: greyed + struck — "delete this" (fillers; the cuttable word of a
     redundancy, so the kept word stays unmarked);
   - UNDERLINE: a wavy underline, text kept readable — "rewrite this" (clichés). */
typedef enum {
  STYLE_DECOR_NONE = 0,
  STYLE_DECOR_STRIKE,
  STYLE_DECOR_UNDERLINE
} StyleDecor;

/* A flagged run: the byte range [start,end) to *decorate* (for a redundancy this
   is just the cuttable word, not the whole phrase), its category, and how to draw
   it. Tagged so editor_types.h can forward-declare it for the per-line cache. */
typedef struct StyleSpan {
  int start, end;
  unsigned char category;   /* StyleCategory, for masking */
  unsigned char decor;      /* StyleDecor */
} StyleSpan;

#define STYLE_MAX_SPANS  256
#define STYLE_BIT(cat)   (1u << (cat))
#define STYLE_MASK_ALL   (STYLE_BIT(STYLE_FILLER) | STYLE_BIT(STYLE_REDUNDANCY) | \
                          STYLE_BIT(STYLE_CLICHE))

/* Lazily scan the line and cache its cuttable-span map (recomputed when
   style_span_count is -1, which line_dirty sets on every edit). Returns the
   count; *out receives the span array, owned by the line. */
int style_line_spans(Line *l, const StyleSpan **out);

/* The decoration to draw at byte `col` of line `l` — STYLE_DECOR_NONE unless a
   span whose category is enabled by `style_mask` covers the column. */
StyleDecor style_decor_at(Line *l, unsigned int style_mask, int col);

#endif /* STYLE_CHECK_H */
