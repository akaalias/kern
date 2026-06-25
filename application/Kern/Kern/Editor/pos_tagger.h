/* pos_tagger.h — part-of-speech tagging behind a seam.
 *
 * Syntax highlighting colors prose by lexical class (noun / verb / adjective /
 * adverb / conjunction), the way iA Writer does. The tagging itself is the only
 * platform-specific piece, so it lives behind this seam: the app backs it with
 * Foundation's NSLinguisticTagger (pos_tagger_nl.m, the same on-device models
 * NLTagger wraps); the headless tests back it with a deterministic fixture
 * lexicon (tests/pos_tagger_fake.c). Mirrors the clock.h / clipboard.h pattern.
 *
 * Tagging is synchronous and fast — sub-millisecond for a sentence on the
 * measured machine — so per-line results are simply cached on the Line (see
 * pos_render.h) and recomputed on edit, never per frame. */
#ifndef POS_TAGGER_H
#define POS_TAGGER_H

/* Highlighted lexical classes. Anything the tagger reports that isn't one of
   these (pronouns, determiners, prepositions, numbers, punctuation, …) maps to
   POS_OTHER and produces no span — those words render in the base color. The
   enum values double as bit indices for ViewState.syntax_mask. */
typedef enum {
  POS_OTHER = 0,
  POS_NOUN,
  POS_VERB,
  POS_ADJECTIVE,
  POS_ADVERB,
  POS_CONJUNCTION,
  POS_CLASS_COUNT
} PosClass;

/* A tagged word: a byte range [start,end) into the line text and its class.
   Tagged (a struct tag, not just a typedef) so editor_types.h can forward-
   declare it for the per-line cache without pulling in this header. */
typedef struct PosSpan { int start, end; unsigned char cls; } PosSpan;

/* Tag [text, text+len) into part-of-speech spans, writing up to `max` of them to
   `out` and returning the count written. Only words whose class is one of the
   highlighted PosClass values get a span; POS_OTHER words are skipped, so the
   span array is sparse and ordered by start. Byte offsets, matching Kern's
   byte-indexed columns. */
int pos_tag_line(const char *text, int len, PosSpan *out, int max);

/* Pay the tagger's one-time model-load cost up front (~100ms on first use) so
   the first real tag is warm. Call once at startup, off the render path. Safe to
   call repeatedly; safe to skip entirely (the first tag just pays it instead). */
void pos_tagger_warm(void);

#endif /* POS_TAGGER_H */
