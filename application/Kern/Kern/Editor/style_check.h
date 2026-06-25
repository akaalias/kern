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
  STYLE_CATEGORY_COUNT
} StyleCategory;

/* A cuttable run: byte range [start,end) into the line text and its category.
   Tagged so editor_types.h can forward-declare it for the per-line cache. */
typedef struct StyleSpan { int start, end; unsigned char category; } StyleSpan;

#define STYLE_MAX_SPANS  256
#define STYLE_BIT(cat)   (1u << (cat))
#define STYLE_MASK_ALL   (STYLE_BIT(STYLE_FILLER) | STYLE_BIT(STYLE_REDUNDANCY))

/* Lazily scan the line and cache its cuttable-span map (recomputed when
   style_span_count is -1, which line_dirty sets on every edit). Returns the
   count; *out receives the span array, owned by the line. */
int style_line_spans(Line *l, const StyleSpan **out);

/* Whether byte `col` of line `l` falls in a struck span whose category is
   enabled by `style_mask`. */
int style_struck_at(Line *l, unsigned int style_mask, int col);

#endif /* STYLE_CHECK_H */
