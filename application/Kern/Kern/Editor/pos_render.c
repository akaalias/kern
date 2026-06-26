/* pos_render.c — per-line POS span cache + class colors (see pos_render.h). */

#include <ctype.h>
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

/* The muted "ground": when syntax highlighting is active, everything that isn't a
   shown class collapses to this so the shown classes pop (isolate, not hide). One
   step *below* the function-word value (112) — same warm-grey tint (g=r-4, b=r-9)
   — so even toggling the function-word group on/off has a visible effect. */
static const Color k_mute_color = { 92, 88, 83, 255 };

Color pos_mute_color(void) { return k_mute_color; }

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

/* ---- word-in-progress + fade-in animation (see pos_render.h) ------------- */

/* What counts as part of a single typed word: letters/digits, the in-word
   punctuation ' - _, and any continuation/lead byte of a multibyte (UTF-8)
   codepoint, so accented words and curly apostrophes ("don't") stay whole. */
static int pos_is_word_byte(unsigned char c) {
  return isalnum(c) || c == '\'' || c == '-' || c == '_' || c >= 0x80;
}

int pos_word_bounds(Line *l, int col, int *lo, int *hi) {
  if (col < 0) col = 0;
  if (col > l->len) col = l->len;
  int a = col, b = col;
  while (a > 0 && pos_is_word_byte((unsigned char)l->text[a - 1])) a--;
  while (b < l->len && pos_is_word_byte((unsigned char)l->text[b])) b++;
  *lo = a; *hi = b;
  return b > a;   /* false when the caret touches no word char on either side */
}

/* Published in-progress word (held at base color). Pointer-compared, never
   dereferenced — a stale pointer after a line-array realloc simply fails to
   match and the word colors normally (same discipline as sub_render). */
static const Line *g_wip_line;
static int g_wip_lo = 1, g_wip_hi = 0;
static unsigned int g_now_ms;

void pos_set_now(unsigned int now_ms) { g_now_ms = now_ms; }

void pos_set_wip(const Line *l, int lo, int hi) {
  g_wip_line = (l && lo < hi) ? l : NULL;
  g_wip_lo = lo; g_wip_hi = hi;
}

/* A handful of words can be mid-fade at once (typing faster than 1s/word). */
#define POS_FADE_SLOTS 16
typedef struct { const Line *line; int lo, hi; unsigned int t0; } PosFade;
static PosFade g_fades[POS_FADE_SLOTS];

void pos_fade_begin(const Line *l, int lo, int hi, unsigned int now_ms) {
  if (!l || lo >= hi) return;
  /* reuse a free/expired slot, else evict the oldest. */
  int slot = 0;
  unsigned int oldest = ~0u;
  for (int i = 0; i < POS_FADE_SLOTS; i++) {
    if (!g_fades[i].line || now_ms - g_fades[i].t0 >= POS_FADE_MS) { slot = i; break; }
    if (g_fades[i].t0 < oldest) { oldest = g_fades[i].t0; slot = i; }
  }
  g_fades[slot] = (PosFade){ l, lo, hi, now_ms };
}

int pos_fades_active(unsigned int now_ms) {
  for (int i = 0; i < POS_FADE_SLOTS; i++)
    if (g_fades[i].line && now_ms - g_fades[i].t0 < POS_FADE_MS) return 1;
  return 0;
}

void pos_anim_reset(void) {
  g_wip_line = NULL; g_wip_lo = 1; g_wip_hi = 0; g_now_ms = 0;
  for (int i = 0; i < POS_FADE_SLOTS; i++) g_fades[i] = (PosFade){ 0 };
}

/* Fade progress in [0,1] for byte `col` of line `l`: 1.0 (settled at the resting
   color) when no in-flight fade covers it, else eased toward 1 over POS_FADE_MS.
   Picks the most-recently-started covering fade so a re-typed word wins. */
static float pos_fade_frac(const Line *l, int col, unsigned int now_ms) {
  float f = 1.0f;
  unsigned int best = 0; int found = 0;
  for (int i = 0; i < POS_FADE_SLOTS; i++) {
    const PosFade *fd = &g_fades[i];
    if (fd->line != l || col < fd->lo || col >= fd->hi) continue;
    unsigned int dt = now_ms - fd->t0;
    if (dt >= POS_FADE_MS) continue;
    if (!found || fd->t0 >= best) { best = fd->t0; f = (float)dt / (float)POS_FADE_MS; found = 1; }
  }
  if (!found) return 1.0f;
  return f * f * (3.0f - 2.0f * f);   /* smoothstep ease */
}

static int lerp_u8(int a, int b, float t) {
  return a + (int)((b - a) * t + (b >= a ? 0.5f : -0.5f));
}

Color pos_resolve_color(Line *l, unsigned int syntax_mask, int col, Color base) {
  /* in-progress word: not yet colored (avoids the mid-word retag flicker). */
  if (g_wip_line == l && col >= g_wip_lo && col < g_wip_hi) return base;
  Color pc;
  Color target = pos_color_at(l, syntax_mask, col, &pc) ? pc : k_mute_color;
  float f = pos_fade_frac(l, col, g_now_ms);
  if (f >= 1.0f) return target;
  return color(lerp_u8(base.r, target.r, f), lerp_u8(base.g, target.g, f),
               lerp_u8(base.b, target.b, f), lerp_u8(base.a, target.a, f));
}
