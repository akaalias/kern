/* style_check.c — curated cuttable-word detection (see style_check.h). */

#include <ctype.h>
#include <stdlib.h>
#include <string.h>
#include "style_check.h"
#include "span_cache.h"

/* A pattern is one or more lowercase words separated by single spaces, matched
   against whole word tokens (so "just" never matches inside "adjust"). Multi-word
   entries match a consecutive token run. Curated for precision over recall: each
   entry is cuttable in (almost) any context, so we don't need sentence parsing.

   cut_from/cut_len select which tokens of the match to *strike* — for a
   redundancy the cuttable word(s) only, so the word you keep stays unmarked
   ("added bonus" strikes just "added"). cut_len == 0 means the whole match
   (fillers cut entirely; clichés underline the whole phrase). */
struct Pattern {
  const char *words;
  unsigned char category;
  unsigned char cut_from, cut_len;
};

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
  /* redundancies — phrases that say a thing twice; strike only the cuttable
     word(s) (cut_from, cut_len), leaving the word you keep unmarked */
  { "added bonus", STYLE_REDUNDANCY, 0, 1 },     /* keep "bonus"        */
  { "totally complete", STYLE_REDUNDANCY, 0, 1 },/* keep "complete"     */
  { "end result", STYLE_REDUNDANCY, 0, 1 },      /* keep "result"       */
  { "free gift", STYLE_REDUNDANCY, 0, 1 },
  { "past history", STYLE_REDUNDANCY, 0, 1 },
  { "past experience", STYLE_REDUNDANCY, 0, 1 },
  { "basic fundamentals", STYLE_REDUNDANCY, 0, 1 },
  { "final outcome", STYLE_REDUNDANCY, 0, 1 },
  { "unexpected surprise", STYLE_REDUNDANCY, 0, 1 },
  { "advance warning", STYLE_REDUNDANCY, 0, 1 },
  { "close proximity", STYLE_REDUNDANCY, 0, 1 },
  { "future plans", STYLE_REDUNDANCY, 0, 1 },
  { "personal opinion", STYLE_REDUNDANCY, 0, 1 },
  { "exact same", STYLE_REDUNDANCY, 0, 1 },
  { "each and every", STYLE_REDUNDANCY, 0, 2 },  /* keep "every": cut "each and" */
  { "first and foremost", STYLE_REDUNDANCY, 1, 2 }, /* keep "first": cut "and foremost" */
  { "null and void", STYLE_REDUNDANCY, 0, 2 },   /* keep "void": cut "null and" */
  /* clichés — tired stock phrases */
  { "at the end of the day", STYLE_CLICHE }, { "think outside the box", STYLE_CLICHE },
  { "outside the box", STYLE_CLICHE }, { "low hanging fruit", STYLE_CLICHE },
  { "the bottom line", STYLE_CLICHE }, { "back to the drawing board", STYLE_CLICHE },
  { "tip of the iceberg", STYLE_CLICHE }, { "level playing field", STYLE_CLICHE },
  { "best of both worlds", STYLE_CLICHE }, { "in this day and age", STYLE_CLICHE },
  { "when all is said and done", STYLE_CLICHE }, { "last but not least", STYLE_CLICHE },
  { "par for the course", STYLE_CLICHE }, { "calm before the storm", STYLE_CLICHE },
  { "blessing in disguise", STYLE_CLICHE }, { "read between the lines", STYLE_CLICHE },
  { "food for thought", STYLE_CLICHE }, { "the elephant in the room", STYLE_CLICHE },
  { "move the needle", STYLE_CLICHE }, { "circle back", STYLE_CLICHE },
  { "touch base", STYLE_CLICHE }, { "push the envelope", STYLE_CLICHE },
  { "raise the bar", STYLE_CLICHE }, { "the new normal", STYLE_CLICHE },
  { "only time will tell", STYLE_CLICHE }, { "needless to say", STYLE_CLICHE },
  { "it goes without saying", STYLE_CLICHE }, { "when push comes to shove", STYLE_CLICHE },
  { "the writing on the wall", STYLE_CLICHE }, { "game changer", STYLE_CLICHE },
  { "cutting edge", STYLE_CLICHE }, { "double edged sword", STYLE_CLICHE },
  { "the bigger picture", STYLE_CLICHE }, { "on the same page", STYLE_CLICHE },
  { "ahead of the curve", STYLE_CLICHE }, { "a perfect storm", STYLE_CLICHE },
  { "the grass is greener", STYLE_CLICHE }, { "boil the ocean", STYLE_CLICHE },
  { "win win", STYLE_CLICHE },
  /* generalizations — sweeping absolutes that usually want qualifying */
  { "obviously", STYLE_CLICHE }, { "clearly", STYLE_CLICHE },
  { "everyone knows", STYLE_CLICHE }, { "without a doubt", STYLE_CLICHE },
  { "without question", STYLE_CLICHE }, { "always", STYLE_CLICHE },
  { "never", STYLE_CLICHE }, { "everyone", STYLE_CLICHE },
  { "everybody", STYLE_CLICHE }, { "nobody", STYLE_CLICHE },
  { "no one", STYLE_CLICHE }, { "everything", STYLE_CLICHE },
  { "nothing", STYLE_CLICHE },
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
    int best_len = 0, best_p = -1;
    for (int p = 0; p < N_PATTERNS; p++) {
      int m = match_at(text, toks, ntok, s, k_patterns[p].words);
      if (m > best_len) { best_len = m; best_p = p; }
    }
    if (best_len > 0) {
      const struct Pattern *pat = &k_patterns[best_p];
      /* decorate only the cuttable tokens (cut_len 0 = the whole match) */
      int from = pat->cut_len ? s + pat->cut_from : s;
      int to   = pat->cut_len ? s + pat->cut_from + pat->cut_len - 1 : s + best_len - 1;
      if (to > s + best_len - 1) to = s + best_len - 1;   /* clamp bad data */
      out[n].start = toks[from].start;
      out[n].end   = toks[to].end;
      out[n].category = pat->category;
      out[n].decor = pat->category == STYLE_CLICHE     ? STYLE_DECOR_UNDERLINE_DOTTED
                   : pat->category == STYLE_REDUNDANCY ? STYLE_DECOR_STRIKE_WAVY
                                                       : STYLE_DECOR_STRIKE;
      n++;
      s += best_len;          /* advance past the whole match, no overlap */
    } else {
      s++;
    }
  }
  return n;
}

KERN_DEFINE_SPAN_CACHE(style_line_spans, StyleSpan,
                       style_spans, style_span_count, style_scan, STYLE_MAX_SPANS)

StyleDecor style_decor_at(Line *l, unsigned int style_mask, int col) {
  if (!style_mask) return STYLE_DECOR_NONE;
  const StyleSpan *spans;
  int n = style_line_spans(l, &spans);
  for (int i = 0; i < n; i++) {
    if (col < spans[i].start) break;
    if (col < spans[i].end) {
      if (style_mask & STYLE_BIT(spans[i].category)) return (StyleDecor)spans[i].decor;
      return STYLE_DECOR_NONE;   /* covered, but category masked off */
    }
  }
  return STYLE_DECOR_NONE;
}
