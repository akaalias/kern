/* style_check.c — curated cuttable-word detection (see style_check.h). */

#include <ctype.h>
#include <stdlib.h>
#include "style_check.h"

/* A pattern is one or more lowercase words separated by single spaces, matched
   against whole word tokens (so "just" never matches inside "adjust"). Multi-word
   entries match a consecutive token run. Curated for precision over recall: each
   entry is cuttable in (almost) any context, so we don't need sentence parsing. */
struct Pattern { const char *words; unsigned char category; };

static const struct Pattern k_patterns[] = {
  /* fillers — hedges and intensifiers that can usually go */
  { "very", STYLE_FILLER }, { "really", STYLE_FILLER }, { "just", STYLE_FILLER },
  { "quite", STYLE_FILLER }, { "rather", STYLE_FILLER }, { "actually", STYLE_FILLER },
  { "basically", STYLE_FILLER }, { "simply", STYLE_FILLER }, { "totally", STYLE_FILLER },
  { "literally", STYLE_FILLER }, { "essentially", STYLE_FILLER }, { "virtually", STYLE_FILLER },
  { "somewhat", STYLE_FILLER }, { "fairly", STYLE_FILLER }, { "truly", STYLE_FILLER },
  { "definitely", STYLE_FILLER }, { "certainly", STYLE_FILLER }, { "probably", STYLE_FILLER },
  { "of course", STYLE_FILLER }, { "in order to", STYLE_FILLER },
  { "kind of", STYLE_FILLER }, { "sort of", STYLE_FILLER },
  /* redundancies — phrases that say a thing twice */
  { "added bonus", STYLE_REDUNDANCY }, { "totally complete", STYLE_REDUNDANCY },
  { "end result", STYLE_REDUNDANCY }, { "free gift", STYLE_REDUNDANCY },
  { "past history", STYLE_REDUNDANCY }, { "past experience", STYLE_REDUNDANCY },
  { "basic fundamentals", STYLE_REDUNDANCY }, { "final outcome", STYLE_REDUNDANCY },
  { "unexpected surprise", STYLE_REDUNDANCY }, { "advance warning", STYLE_REDUNDANCY },
  { "close proximity", STYLE_REDUNDANCY }, { "future plans", STYLE_REDUNDANCY },
  { "personal opinion", STYLE_REDUNDANCY }, { "exact same", STYLE_REDUNDANCY },
  { "each and every", STYLE_REDUNDANCY }, { "first and foremost", STYLE_REDUNDANCY },
  { "null and void", STYLE_REDUNDANCY },
};
#define N_PATTERNS ((int)(sizeof k_patterns / sizeof k_patterns[0]))

#define MAX_TOK 1024
struct Tok { int start, end; };

static int is_word(char c) { return isalpha((unsigned char)c) || c == '\''; }

static int tokenize(const char *t, int len, struct Tok *toks, int max) {
  int n = 0, i = 0;
  while (i < len && n < max) {
    if (!is_word(t[i])) { i++; continue; }
    int j = i;
    while (j < len && is_word(t[j])) j++;
    toks[n].start = i; toks[n].end = j; n++;
    i = j;
  }
  return n;
}

/* Number of consecutive tokens from `s` that match pattern `pat` (0 = no match).
   `pat` words are lowercase and space-separated; the comparison is case-folded. */
static int match_at(const char *text, const struct Tok *toks, int ntok,
                    int s, const char *pat) {
  int ti = s, matched = 0;
  const char *p = pat;
  while (*p) {
    const char *pw = p;
    while (*p && *p != ' ') p++;
    int pwlen = (int)(p - pw);
    while (*p == ' ') p++;
    if (ti >= ntok) return 0;
    if (toks[ti].end - toks[ti].start != pwlen) return 0;
    for (int k = 0; k < pwlen; k++)
      if ((char)tolower((unsigned char)text[toks[ti].start + k]) != pw[k]) return 0;
    ti++; matched++;
  }
  return matched;
}

static int style_scan(const char *text, int len, StyleSpan *out, int max) {
  struct Tok toks[MAX_TOK];
  int ntok = tokenize(text, len, toks, MAX_TOK);
  int n = 0, s = 0;
  while (s < ntok && n < max) {
    int best_len = 0;
    unsigned char best_cat = STYLE_NONE;
    for (int p = 0; p < N_PATTERNS; p++) {
      int m = match_at(text, toks, ntok, s, k_patterns[p].words);
      if (m > best_len) { best_len = m; best_cat = k_patterns[p].category; }
    }
    if (best_len > 0) {
      out[n].start = toks[s].start;
      out[n].end   = toks[s + best_len - 1].end;
      out[n].category = best_cat;
      n++;
      s += best_len;          /* don't overlap a match with the next */
    } else {
      s++;
    }
  }
  return n;
}

int style_line_spans(Line *l, const StyleSpan **out) {
  if (l->style_span_count < 0) {
    free(l->style_spans);
    l->style_spans = NULL;
    StyleSpan scratch[STYLE_MAX_SPANS];
    int n = style_scan(l->text, l->len, scratch, STYLE_MAX_SPANS);
    if (n > 0) {
      l->style_spans = malloc((size_t)n * sizeof(StyleSpan));
      if (l->style_spans) {
        for (int i = 0; i < n; i++) l->style_spans[i] = scratch[i];
      } else {
        n = 0;   /* allocation failed: cache empty, render unmarked */
      }
    }
    l->style_span_count = n;
  }
  *out = l->style_spans;
  return l->style_span_count;
}

int style_struck_at(Line *l, unsigned int style_mask, int col) {
  if (!style_mask) return 0;
  const StyleSpan *spans;
  int n = style_line_spans(l, &spans);
  for (int i = 0; i < n; i++) {
    if (col < spans[i].start) break;
    if (col < spans[i].end)
      return (style_mask & STYLE_BIT(spans[i].category)) != 0;
  }
  return 0;
}
