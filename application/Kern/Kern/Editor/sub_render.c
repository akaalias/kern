/* sub_render.c — display-only text→symbol substitution (see sub_render.h). */

#include <ctype.h>
#include <string.h>
#include <stdlib.h>
#include "sub_render.h"
#include "renderer.h"   /* r_has_glyph — fall back to literal for glyphs the font lacks */

/* Fixed substitutions matched at a position, longest-match-first (so "<=>" wins
   over "<="). Curated precision-first. `word` entries (Greek names, math operator
   words) must match a whole word — bounded by non-word characters on both sides —
   so "lambda" → λ but "lambdas" / "flambda" / "lambda_x" stay literal; they are
   also case-sensitive ("Delta" → Δ, "delta" → δ). Context-sensitive cases (smart
   quotes, the en dash between digits) are handled in sub_scan, not here.
   Deliberately excluded: triggers that are also common English words (in, sum,
   and, or, partial, …) — there is no clean way to tell prose from math, and
   reveal-on-contact can't rescue a false positive you didn't notice. */
struct Fixed { const char *src; const char *glyph; unsigned char category; unsigned char word; };

static const struct Fixed k_fixed[] = {
  /* arrows & relations */
  { "<=>", "⇔", SUB_ARROW, 0 },
  { "->",  "→", SUB_ARROW, 0 },
  { "<-",  "←", SUB_ARROW, 0 },
  { "=>",  "⇒", SUB_ARROW, 0 },
  { "!=",  "≠", SUB_ARROW, 0 },
  { "<=",  "≤", SUB_ARROW, 0 },
  { ">=",  "≥", SUB_ARROW, 0 },
  { "~=",  "≈", SUB_ARROW, 0 },
  /* typography */
  { "--",  "—", SUB_PUNCT, 0 },   /* em dash */
  { "...", "…", SUB_PUNCT, 0 },
  { "+-",  "±", SUB_PUNCT, 0 },
  { "(c)", "©", SUB_PUNCT, 0 },
  { "(r)", "®", SUB_PUNCT, 0 },
  { "(tm)","™", SUB_PUNCT, 0 },
  /* Greek by name — whole-word. Only names that are never English words: the
     short collision-prone ones (eta/nu/xi/chi/iota) are left out on purpose. */
  { "lambda",  "λ", SUB_GREEK, 1 }, { "Lambda",  "Λ", SUB_GREEK, 1 },
  { "alpha",   "α", SUB_GREEK, 1 },
  { "beta",    "β", SUB_GREEK, 1 },
  { "gamma",   "γ", SUB_GREEK, 1 }, { "Gamma",   "Γ", SUB_GREEK, 1 },
  { "delta",   "δ", SUB_GREEK, 1 }, { "Delta",   "Δ", SUB_GREEK, 1 },
  { "epsilon", "ε", SUB_GREEK, 1 },
  { "zeta",    "ζ", SUB_GREEK, 1 },
  { "theta",   "θ", SUB_GREEK, 1 }, { "Theta",   "Θ", SUB_GREEK, 1 },
  { "kappa",   "κ", SUB_GREEK, 1 },
  { "mu",      "μ", SUB_GREEK, 1 },
  { "pi",      "π", SUB_GREEK, 1 }, { "Pi",      "Π", SUB_GREEK, 1 },
  { "rho",     "ρ", SUB_GREEK, 1 },
  { "sigma",   "σ", SUB_GREEK, 1 }, { "Sigma",   "Σ", SUB_GREEK, 1 },
  { "tau",     "τ", SUB_GREEK, 1 },
  { "phi",     "φ", SUB_GREEK, 1 }, { "Phi",     "Φ", SUB_GREEK, 1 },
  { "psi",     "ψ", SUB_GREEK, 1 }, { "Psi",     "Ψ", SUB_GREEK, 1 },
  { "omega",   "ω", SUB_GREEK, 1 }, { "Omega",   "Ω", SUB_GREEK, 1 },
  /* math operators — whole-word, and only non-English words */
  { "forall",   "∀", SUB_MATH, 1 },
  { "exists",   "∃", SUB_MATH, 1 },
  { "nabla",    "∇", SUB_MATH, 1 },
  { "infinity", "∞", SUB_MATH, 1 },
  { "sqrt",     "√", SUB_MATH, 1 },
  /* typographic f-ligatures — mid-word, NOT whole-word (they sit inside "offer",
     "file", "flow"). Longest-match-first handles ffi/ffl before ff. The font may
     lack some of these glyphs; emit() skips any the font can't draw, so the
     source renders literally instead of as a tofu box. */
  { "ffi", "ﬃ", SUB_LIGATURE, 0 },
  { "ffl", "ﬄ", SUB_LIGATURE, 0 },
  { "ff",  "ﬀ", SUB_LIGATURE, 0 },
  { "fi",  "ﬁ", SUB_LIGATURE, 0 },
  { "fl",  "ﬂ", SUB_LIGATURE, 0 },
};
#define N_FIXED ((int)(sizeof k_fixed / sizeof k_fixed[0]))

/* a "word" character for whole-word boundary tests (so a Greek name inside an
   identifier — lambda_x, lambda2 — is left alone) */
static int is_word_char(unsigned char c) { return isalnum(c) || c == '_'; }

static void emit(SubSpan *out, int *pn, int max, int start, int len,
                 unsigned char cat, const char *glyph) {
  if (*pn >= max) return;
  /* skip the substitution when the font can't draw the glyph — the source then
     renders as literal text instead of a tofu box */
  if (!r_has_glyph(glyph, (int)strlen(glyph))) return;
  SubSpan *s = &out[(*pn)++];
  s->start = start; s->len = len; s->category = cat;
  int gl = (int)strlen(glyph);
  if (gl > (int)sizeof s->glyph - 1) gl = (int)sizeof s->glyph - 1;
  memcpy(s->glyph, glyph, gl);
  s->glyph[gl] = '\0';
  s->glyph_len = (unsigned char)gl;
}

/* A quote opens (curls left) at the start of text or after whitespace / an
   opening bracket / a dash; otherwise it closes — which also yields the right
   glyph for apostrophes ("don't", "James'" → ’). */
static int opens_quote(const char *t, int i) {
  if (i == 0) return 1;
  unsigned char p = (unsigned char)t[i - 1];
  return isspace(p) || p == '(' || p == '[' || p == '{' || p == '-';
}

static int starts_with(const char *t, int len, int i, const char *s) {
  int n = (int)strlen(s);
  if (i + n > len) return 0;
  return memcmp(t + i, s, (size_t)n) == 0;
}

static int sub_scan(const char *t, int len, SubSpan *out, int max) {
  int n = 0, i = 0;
  while (i < len && n < max) {
    char c = t[i];

    /* context-sensitive single-byte substitutions */
    if (c == '"') {
      emit(out, &n, max, i, 1, SUB_PUNCT, opens_quote(t, i) ? "“" : "”");
      i++; continue;
    }
    if (c == '\'') {
      emit(out, &n, max, i, 1, SUB_PUNCT, opens_quote(t, i) ? "‘" : "’");
      i++; continue;
    }
    /* a lone hyphen between two digits is an en dash ("1995-2026" → 1995–2026);
       a doubled hyphen "--" is the em dash, handled by the fixed table */
    if (c == '-' && i > 0 && i + 1 < len &&
        isdigit((unsigned char)t[i - 1]) && isdigit((unsigned char)t[i + 1])) {
      emit(out, &n, max, i, 1, SUB_PUNCT, "–");   /* – */
      i++; continue;
    }

    /* fixed longest-match tokens (word entries require whole-word boundaries) */
    int best = -1, best_len = 0;
    for (int k = 0; k < N_FIXED; k++) {
      int sl = (int)strlen(k_fixed[k].src);
      if (sl <= best_len || !starts_with(t, len, i, k_fixed[k].src)) continue;
      if (k_fixed[k].word) {
        if (i > 0 && is_word_char((unsigned char)t[i - 1])) continue;        /* left  */
        if (i + sl < len && is_word_char((unsigned char)t[i + sl])) continue; /* right */
      }
      best = k; best_len = sl;
    }
    if (best >= 0) {
      emit(out, &n, max, i, best_len, k_fixed[best].category, k_fixed[best].glyph);
      i += best_len; continue;
    }
    i++;
  }
  return n;
}

int sub_line_spans(Line *l, const SubSpan **out) {
  if (l->sub_span_count < 0) {
    free(l->sub_spans);
    l->sub_spans = NULL;
    SubSpan scratch[SUB_MAX_SPANS];
    int n = sub_scan(l->text, l->len, scratch, SUB_MAX_SPANS);
    if (n > 0) {
      l->sub_spans = malloc((size_t)n * sizeof(SubSpan));
      if (l->sub_spans) {
        for (int i = 0; i < n; i++) l->sub_spans[i] = scratch[i];
      } else {
        n = 0;   /* allocation failed: cache empty, render literal */
      }
    }
    l->sub_span_count = n;
  }
  *out = l->sub_spans;
  return l->sub_span_count;
}

const SubSpan *sub_at(Line *l, unsigned int sub_mask, int col) {
  if (!sub_mask) return NULL;
  const SubSpan *spans;
  int n = sub_line_spans(l, &spans);
  for (int i = 0; i < n; i++) {
    if (spans[i].start > col) break;
    if (spans[i].start == col && (sub_mask & SUB_BIT(spans[i].category)))
      return &spans[i];
  }
  return NULL;
}

static unsigned int g_sub_mask = 0;
void sub_set_mask(unsigned int m) { g_sub_mask = m; }
unsigned int sub_active_mask(void) { return g_sub_mask; }

/* reveal-on-contact: tokens on g_reveal_line whose byte range overlaps the
   inclusive reveal range [g_reveal_lo, g_reveal_hi] are drawn literally (not
   collapsed) so they can be read and edited. The range is the caret point, unioned
   with the active selection's extent on that line — so a symbol stays expanded for
   as long as it is selected, instead of flip-flopping as the caret passes it.
   g_reveal_line is compared by pointer, never dereferenced, so a stale value
   (after a line-array realloc between frames) is harmless. */
static const Line *g_reveal_line = NULL;
static int g_reveal_lo = 1, g_reveal_hi = 0;   /* lo > hi → empty (reveals nothing) */
void sub_set_reveal(const Line *l, int lo, int hi) {
  g_reveal_line = l; g_reveal_lo = lo; g_reveal_hi = hi;
}
void sub_set_caret(const Line *l, int col) { sub_set_reveal(l, col, col); }
int sub_token_revealed(const Line *l, int start, int len) {
  return l == g_reveal_line && start <= g_reveal_hi && start + len >= g_reveal_lo;
}
