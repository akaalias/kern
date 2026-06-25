/* sub_render.c — display-only text→symbol substitution (see sub_render.h). */

#include <ctype.h>
#include <string.h>
#include <stdlib.h>
#include "sub_render.h"

/* Fixed substitutions matched literally at a position, longest-match-first (so
   "<=>" wins over "<="). Curated precision-first: each is unambiguous enough to
   render in any prose context. Context-sensitive cases (smart quotes, the en
   dash between digits) are handled in sub_scan, not here. */
struct Fixed { const char *src; const char *glyph; unsigned char category; };

static const struct Fixed k_fixed[] = {
  /* arrows & relations */
  { "<=>", "⇔", SUB_ARROW },   /* ⇔ */
  { "->",  "→", SUB_ARROW },   /* → */
  { "<-",  "←", SUB_ARROW },   /* ← */
  { "=>",  "⇒", SUB_ARROW },   /* ⇒ */
  { "!=",  "≠", SUB_ARROW },   /* ≠ */
  { "<=",  "≤", SUB_ARROW },   /* ≤ */
  { ">=",  "≥", SUB_ARROW },   /* ≥ */
  { "~=",  "≈", SUB_ARROW },   /* ≈ */
  /* typography */
  { "--",  "—", SUB_PUNCT },   /* — em dash */
  { "...", "…", SUB_PUNCT },   /* … */
  { "+-",  "±", SUB_PUNCT },   /* ± */
  { "(c)", "©", SUB_PUNCT },   /* © */
  { "(r)", "®", SUB_PUNCT },   /* ® */
  { "(tm)","™", SUB_PUNCT },   /* ™ */
};
#define N_FIXED ((int)(sizeof k_fixed / sizeof k_fixed[0]))

static void emit(SubSpan *out, int *pn, int max, int start, int len,
                 unsigned char cat, const char *glyph) {
  if (*pn >= max) return;
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

    /* fixed longest-match tokens */
    int best = -1, best_len = 0;
    for (int k = 0; k < N_FIXED; k++) {
      int sl = (int)strlen(k_fixed[k].src);
      if (sl > best_len && starts_with(t, len, i, k_fixed[k].src)) {
        best = k; best_len = sl;
      }
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
