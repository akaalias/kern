/* pos_tagger_fake.c — deterministic POS tagger for the headless tests.
 *
 * Implements the pos_tagger.h seam with a tiny fixed lexicon instead of
 * NSLinguisticTagger, so tagging is reproducible and platform-free. Words not in
 * the lexicon tag as POS_OTHER (no span), matching the real tagger's contract.
 * The wordlist is intentionally small and unambiguous — just enough to exercise
 * pos_render.c and the md_render color path. */
#include <ctype.h>
#include <string.h>
#include "pos_tagger.h"

struct entry { const char *w; unsigned char cls; };

/* lowercase word -> class. Only colored classes are listed. */
static const struct entry k_lex[] = {
  /* nouns */
  { "cat", POS_NOUN }, { "dog", POS_NOUN }, { "fox", POS_NOUN },
  { "work", POS_NOUN }, { "week", POS_NOUN }, { "home", POS_NOUN },
  { "routine", POS_NOUN }, { "tool", POS_NOUN },
  /* verbs */
  { "run", POS_VERB }, { "runs", POS_VERB }, { "learn", POS_VERB },
  { "built", POS_VERB }, { "keep", POS_VERB }, { "establish", POS_VERB },
  /* adjectives */
  { "quick", POS_ADJECTIVE }, { "brown", POS_ADJECTIVE },
  { "solid", POS_ADJECTIVE }, { "deep", POS_ADJECTIVE },
  { "simple", POS_ADJECTIVE },
  /* adverbs */
  { "really", POS_ADVERB }, { "fully", POS_ADVERB },
  { "quickly", POS_ADVERB }, { "basically", POS_ADVERB },
  /* conjunctions */
  { "and", POS_CONJUNCTION }, { "or", POS_CONJUNCTION },
  { "but", POS_CONJUNCTION }, { "so", POS_CONJUNCTION },
};

static unsigned char lookup(const char *w, int n) {
  char buf[64];
  if (n >= (int)sizeof buf) return POS_OTHER;
  for (int i = 0; i < n; i++) buf[i] = (char)tolower((unsigned char)w[i]);
  buf[n] = '\0';
  for (size_t i = 0; i < sizeof k_lex / sizeof k_lex[0]; i++)
    if (strcmp(buf, k_lex[i].w) == 0) return k_lex[i].cls;
  return POS_OTHER;
}

int pos_tag_line(const char *text, int len, PosSpan *out, int max) {
  int count = 0, i = 0;
  while (i < len && count < max) {
    if (!isalpha((unsigned char)text[i])) { i++; continue; }
    int j = i;
    while (j < len && (isalpha((unsigned char)text[j]) || text[j] == '\'')) j++;
    unsigned char cls = lookup(text + i, j - i);
    if (cls != POS_OTHER) {
      out[count].start = i;
      out[count].end   = j;
      out[count].cls   = cls;
      count++;
    }
    i = j;
  }
  return count;
}

void pos_tagger_warm(void) { /* nothing to warm in the fake */ }
