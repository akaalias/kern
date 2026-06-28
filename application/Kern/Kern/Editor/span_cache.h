/* span_cache.h — the one lazy per-line span cache, shared by every span layer.
 *
 * md / pos / style / sub each keep a `(T *spans, int span_count)` pair on `Line`,
 * computed once and reused until line_dirty() resets the count to -1. The
 * compute-on-demand body was identical in three places (pos_render, sub_render,
 * style_check); this macro is its single source of truth.
 *
 * Instantiate one accessor per layer:
 *     KERN_DEFINE_SPAN_CACHE(pos_line_spans, PosSpan,
 *                            pos_spans, pos_span_count, pos_scan, POS_MAX_SPANS)
 *
 * SCAN must have the shape `int SCAN(const char *text, int len, T *out, int max)`
 * and fill at most `max` spans, returning the count (a layer that needs extra
 * post-processing wraps its scanner — see pos_render.c's pos_scan).
 *
 * Requires <stdlib.h> and <string.h> in the including TU, plus the Line type.
 */
#ifndef KERN_SPAN_CACHE_H
#define KERN_SPAN_CACHE_H

#define KERN_DEFINE_SPAN_CACHE(FN, T, ARR, CNT, SCAN, MAXN)             \
  int FN(Line *l, const T **out) {                                      \
    if (l->CNT < 0) {                                                   \
      free(l->ARR);                                                     \
      l->ARR = NULL;                                                    \
      T scratch[MAXN];                                                  \
      int n = SCAN(l->text, l->len, scratch, MAXN);                     \
      if (n > 0) {                                                      \
        l->ARR = malloc((size_t)n * sizeof(T));                         \
        if (l->ARR) memcpy(l->ARR, scratch, (size_t)n * sizeof(T));     \
        else n = 0; /* alloc failed: cache empty, render unstyled */    \
      }                                                                 \
      l->CNT = n;                                                       \
    }                                                                   \
    *out = l->ARR;                                                      \
    return l->CNT;                                                      \
  }

#endif /* KERN_SPAN_CACHE_H */
