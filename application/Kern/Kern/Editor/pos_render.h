/* pos_render.h — per-line POS span cache + color mapping for syntax highlighting.
 *
 * The headless-testable consumer of the pos_tagger.h seam. Owns the lazy
 * per-Line span cache (invalidated by line_dirty, exactly like the markdown span
 * cache) and the class→Color table. md_render.c calls pos_color_at() while
 * drawing; this module decides whether and how a character is colored. */
#ifndef POS_RENDER_H
#define POS_RENDER_H

#include "editor_types.h"
#include "gfx.h"
#include "pos_tagger.h"

/* Cap on tagged words cached per logical line. Lines longer than this still
   render — the tail just goes uncolored. Generous for prose. */
#define POS_MAX_SPANS 512

/* Bit `cls` of a syntax mask = "show class cls". A mask of 0 disables syntax
   highlighting entirely (the default), so existing render output is unchanged. */
#define POS_BIT(cls)     (1u << (cls))
#define SYNTAX_MASK_ALL  (POS_BIT(POS_NOUN) | POS_BIT(POS_VERB) | \
                          POS_BIT(POS_ADJECTIVE) | POS_BIT(POS_ADVERB) | \
                          POS_BIT(POS_CONJUNCTION) | POS_BIT(POS_DETERMINER) | \
                          POS_BIT(POS_PREPOSITION) | POS_BIT(POS_PRONOUN) | \
                          POS_BIT(POS_PARTICLE))

/* Lazily tag the line and cache its span map (recomputed when pos_span_count is
   -1, which line_dirty sets on every edit). Returns the count; *out receives the
   span array, owned by the line. */
int pos_line_spans(Line *l, const PosSpan **out);

/* The highlight color for a class. Returns 1 and writes *out for a colored
   class; returns 0 for POS_OTHER (no color). */
int pos_class_color(PosClass cls, Color *out);

/* The muted "ground" color. With syntax highlighting active, md_render paints any
   text that isn't a currently-shown class with this, so the shown classes stand
   out against a uniform dim background (one step below the function-word value). */
Color pos_mute_color(void);

/* Whether class `cls` is currently shown by `syntax_mask`. */
int pos_class_enabled(unsigned int syntax_mask, PosClass cls);

/* Resolve the POS color for byte `col` of line `l`, honoring `syntax_mask`.
   Returns 1 and writes *out when a shown class covers the column; 0 otherwise
   (the caller keeps the base color). */
int pos_color_at(Line *l, unsigned int syntax_mask, int col, Color *out);

#endif /* POS_RENDER_H */
