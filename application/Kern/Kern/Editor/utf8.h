/* utf8.h — minimal UTF-8 stepping/decoding.
 *
 * Header-only (static inline) so it compiles into the app, the SDL renderer,
 * and the headless test binary without a build-system entry. Columns elsewhere
 * stay byte-indexed; these helpers keep the caret on codepoint boundaries and
 * let the renderer decode whole codepoints. Invalid/truncated bytes degrade to
 * a single byte so nothing ever loops forever or reads past `max`. */
#ifndef KERN_UTF8_H
#define KERN_UTF8_H

/* Byte length of the codepoint that starts at s[0], clamped to `max` bytes.
   Returns 1 for ASCII, an invalid lead byte, a truncated sequence, or a bad
   continuation byte (so callers always make progress). */
static inline int utf8_len(const char *s, int max) {
  if (max <= 0) return 0;
  unsigned char c = (unsigned char)s[0];
  int n;
  if (c < 0x80)            return 1;
  else if ((c & 0xE0) == 0xC0) n = 2;
  else if ((c & 0xF0) == 0xE0) n = 3;
  else if ((c & 0xF8) == 0xF0) n = 4;
  else                     return 1;          /* stray continuation / invalid lead */
  if (n > max) return 1;                       /* truncated at end of buffer */
  for (int i = 1; i < n; i++)
    if (((unsigned char)s[i] & 0xC0) != 0x80) return 1;   /* bad continuation */
  return n;
}

/* Number of bytes from `pos` back to the start of the previous codepoint
   (1..4). Skips continuation bytes (0x80..0xBF). 0 if already at the start. */
static inline int utf8_back(const char *s, int pos) {
  if (pos <= 0) return 0;
  int i = pos - 1, n = 1;
  while (i > 0 && ((unsigned char)s[i] & 0xC0) == 0x80 && n < 4) { i--; n++; }
  return n;
}

/* Decode the codepoint at s[0] (within `max` bytes) into *cp; returns its byte
   length. Invalid/truncated input yields the raw byte as *cp with length 1. */
static inline int utf8_decode(const char *s, int max, int *cp) {
  int n = utf8_len(s, max);
  unsigned char c = (unsigned char)s[0];
  if (n == 1) { *cp = c; return 1; }
  int v = (n == 2) ? (c & 0x1F) : (n == 3) ? (c & 0x0F) : (c & 0x07);
  for (int i = 1; i < n; i++) v = (v << 6) | ((unsigned char)s[i] & 0x3F);
  *cp = v;
  return n;
}

#endif /* KERN_UTF8_H */
