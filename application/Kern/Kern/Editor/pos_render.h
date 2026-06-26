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

/* ---- word-in-progress + fade-in animation -------------------------------- *
 * While a word is being typed its POS tag keeps changing letter-by-letter
 * ("comp"→"compute"→"computer"), so coloring it mid-stroke flickers. The render
 * loop instead holds the word under the caret at the base ("max") color until
 * it's finished (the caret leaves it — a space, punctuation, or a move), then
 * fades it from the base color to its real POS color over POS_FADE_MS. The
 * timing state lives here (a render-module static), not on the Line, because
 * line_dirty rebuilds the per-line span cache on every keystroke. */

#define POS_FADE_MS 5000   /* word base-color → POS-color fade duration ("ink drying") */

/* Byte range [*lo,*hi) of the "word" the caret at `col` sits in or against
   (letters/digits plus ' - _ and any byte ≥0x80, so hyphenated/apostrophe'd and
   accented words stay whole). Returns 1 with the bounds when `col` touches a
   word, 0 (and *lo==*hi==col) when it's on whitespace/between words. */
int pos_word_bounds(Line *l, int col, int *lo, int *hi);

/* Per-frame current time (ms), so the resolver below can compute fade progress
   without a clock dependency or signature change. Set once per frame. */
void pos_set_now(unsigned int now_ms);

/* Publish the in-progress word (byte range [lo,hi) on line `l`); held at the
   base color, never colored or muted. Pass l==NULL / lo>hi for "none". The line
   is compared by pointer only, never dereferenced (stale-pointer safe, like
   sub_render's reveal range). */
void pos_set_wip(const Line *l, int lo, int hi);

/* Begin a base-color → POS-color fade for the word [lo,hi) on line `l`, starting
   at `now_ms`. Kept in a small ring (oldest entry recycled). */
void pos_fade_begin(const Line *l, int lo, int hi, unsigned int now_ms);

/* Whether any fade is still in flight at `now_ms` (so the event loop should keep
   repainting at ~60fps). */
int pos_fades_active(unsigned int now_ms);

/* Clear all word-in-progress + fade state (tests / buffer switches). */
void pos_anim_reset(void);

/* Final color for byte `col` of line `l` under `syntax_mask`, given the editor's
   `base` color. Returns `base` for the in-progress word; otherwise the resting
   color (POS class color, or the muted ground for an off/untagged class), lerped
   up from `base` while a fade covering the column is in flight. Used by
   md_draw_text in place of the raw pos_color_at / pos_mute_color choice. */
Color pos_resolve_color(Line *l, unsigned int syntax_mask, int col, Color base);

#endif /* POS_RENDER_H */
