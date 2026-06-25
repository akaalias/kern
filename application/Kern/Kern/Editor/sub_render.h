/* sub_render.h — display-only text→symbol substitution ("ligatures"). A fourth
 * per-line span layer alongside md_spans / pos_spans / style_spans: sub_spans
 * mark byte ranges of source text (e.g. "->", "--", a straight quote) that the
 * renderer draws as a single replacement glyph (→, —, "). Display only — the
 * buffer keeps the literal bytes, so copy/paste, search, and editing are
 * unchanged; toggling the layer never rewrites text.
 *
 * Phase 1 is "always-on" (no reveal-on-contact): the substitution is a pure
 * function of the line text, so the per-line cache invalidates with line_dirty
 * exactly like the other span layers, and the wrap cache (also edit-only) stays
 * correct. Pure C — curated table + a little context logic for smart quotes —
 * so the whole thing is headless-testable.
 *
 * The three width walks that must agree on a substituted token's advance width
 * (md_render.c's md_draw_text and md_x_to_col, navigation.c's
 * nav_get_wrap_breaks) all read the same cache and the same active mask
 * (sub_active_mask), so caret-x, click-to-cursor, and wrapping stay in lockstep.
 * A caret resting *inside* a collapsed multi-byte token renders at the glyph's
 * left edge; click/vertical-move only ever land the caret on a token boundary. */
#ifndef SUB_RENDER_H
#define SUB_RENDER_H

#include "editor_types.h"

/* Substitution categories. Enum values double as bit indices for
   ViewState.sub_mask. (SUB_GREEK / SUB_CODE are planned phase-2 additions, gated
   on whole-word boundaries / code context.) */
typedef enum {
  SUB_NONE = 0,
  SUB_PUNCT,   /* typography: smart quotes, em/en dash, ellipsis, © ® ™, ± */
  SUB_ARROW,   /* arrows & relations: -> <- => <=> != <= >= ~= */
  SUB_CATEGORY_COUNT
} SubCategory;

/* A substitution: the source byte range [start,start+len) and the UTF-8
   replacement glyph drawn in its place. Tagged so editor_types.h can
   forward-declare it for the per-line cache. */
typedef struct SubSpan {
  int  start;
  int  len;                 /* source bytes consumed */
  unsigned char category;   /* SubCategory, for masking */
  unsigned char glyph_len;  /* bytes in glyph */
  char glyph[8];            /* UTF-8 replacement glyph, NUL-terminated */
} SubSpan;

#define SUB_MAX_SPANS  512
#define SUB_BIT(cat)   (1u << (cat))
#define SUB_MASK_ALL   (SUB_BIT(SUB_PUNCT) | SUB_BIT(SUB_ARROW))

/* Lazily scan the line and cache its substitution-span map (recomputed when
   sub_span_count is -1, which line_dirty sets on every edit). Returns the count;
   *out receives the array, owned by the line. The cache is mask-independent —
   every category is scanned; the walks mask at read time. */
int sub_line_spans(Line *l, const SubSpan **out);

/* The substitution beginning exactly at byte `col` of line `l` whose category is
   enabled by `sub_mask`, or NULL. (Linear; for tests and one-off lookups — the
   render/wrap walks stream the span array with a forward cursor instead.) */
const SubSpan *sub_at(Line *l, unsigned int sub_mask, int col);

/* The active substitution mask, shared by md_render.c and navigation.c so the
   render, caret, click, and wrap walks all agree. The render pass sets it from
   ViewState.sub_mask each frame (0 = off, the default), then clears it. */
void sub_set_mask(unsigned int m);
unsigned int sub_active_mask(void);

/* Reveal-on-contact: the render and measure walks draw a token literally (its
   source bytes), not the glyph, when the caret touches it [start, start+len], so
   it stays editable. The caller marks the caret's line+column each frame (and
   before event-time click/vertical-move measurement); sub_reveal_col returns that
   column for the caret's line, -1 for any other line (whose tokens stay
   collapsed). Wrap is measured literally regardless, so reveal never reflows. */
void sub_set_caret(const Line *l, int col);
int  sub_reveal_col(const Line *l);

#endif /* SUB_RENDER_H */
