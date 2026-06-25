/* unit_utf8.c — the header-only UTF-8 stepping/decoding helpers (utf8.h).
 * These underpin codepoint-aware cursor movement, deletion, wrapping, and the
 * renderer's glyph lookup. */
#include <string.h>
#include "test.h"
#include "utf8.h"

/* U+2019 RIGHT SINGLE QUOTATION MARK ("smart apostrophe") = E2 80 99 (3 bytes).
   This is the exact char that pasted from Chrome triggered the bug report. */
#define RSQUO "\xE2\x80\x99"
/* U+2014 EM DASH = E2 80 94; U+00E9 é = C3 A9 (2 bytes); U+1F600 😀 = 4 bytes */
#define EMDASH "\xE2\x80\x94"
#define EACUTE "\xC3\xA9"
#define EMOJI  "\xF0\x9F\x98\x80"

static void test_len_ascii(void) {
  CHECK_IEQ(utf8_len("a", 1), 1);
  CHECK_IEQ(utf8_len("hello", 5), 1);   /* one char at a time */
}
static void test_len_multibyte(void) {
  CHECK_IEQ(utf8_len(RSQUO, 3), 3);
  CHECK_IEQ(utf8_len(EACUTE, 2), 2);
  CHECK_IEQ(utf8_len(EMOJI, 4), 4);
}
static void test_len_truncated_is_one(void) {
  CHECK_IEQ(utf8_len(RSQUO, 2), 1);     /* not enough bytes for a 3-byte char */
  CHECK_IEQ(utf8_len("\x80", 1), 1);    /* stray continuation byte */
  CHECK_IEQ(utf8_len("\xFF", 1), 1);    /* invalid lead byte */
}
static void test_len_bad_continuation(void) {
  CHECK_IEQ(utf8_len("\xE2\x41\x99", 3), 1);   /* 2nd byte not a continuation */
}

static void test_back_ascii(void) {
  CHECK_IEQ(utf8_back("abc", 3), 1);
  CHECK_IEQ(utf8_back("abc", 0), 0);
}
static void test_back_over_multibyte(void) {
  /* "x" + RSQUO: positions — byte 0 'x', bytes 1..3 the smart quote, len 4 */
  const char *s = "x" RSQUO;
  CHECK_IEQ(utf8_back(s, 4), 3);   /* from end, step back over the 3-byte quote */
  CHECK_IEQ(utf8_back(s, 1), 1);   /* from after 'x', step back one byte */
}

static void test_decode_values(void) {
  int cp;
  CHECK_IEQ(utf8_decode("A", 1, &cp), 1);   CHECK_IEQ(cp, 0x41);
  CHECK_IEQ(utf8_decode(RSQUO, 3, &cp), 3);  CHECK_IEQ(cp, 0x2019);
  CHECK_IEQ(utf8_decode(EMDASH, 3, &cp), 3); CHECK_IEQ(cp, 0x2014);
  CHECK_IEQ(utf8_decode(EACUTE, 2, &cp), 2); CHECK_IEQ(cp, 0xE9);
  CHECK_IEQ(utf8_decode(EMOJI, 4, &cp), 4);  CHECK_IEQ(cp, 0x1F600);
}
static void test_decode_invalid_is_raw_byte(void) {
  int cp;
  CHECK_IEQ(utf8_decode("\xFF", 1, &cp), 1); CHECK_IEQ(cp, 0xFF);
}

void suite_utf8(void) {
  RUN(test_len_ascii);
  RUN(test_len_multibyte);
  RUN(test_len_truncated_is_one);
  RUN(test_len_bad_continuation);
  RUN(test_back_ascii);
  RUN(test_back_over_multibyte);
  RUN(test_decode_values);
  RUN(test_decode_invalid_is_raw_byte);
}
