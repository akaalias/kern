/* test.h — minimal single-header harness for Kern's headless test suite.
 *
 * Correctness assertions live here; memory safety is delegated to the
 * sanitizers (ASan/UBSan/LSan) the binary is compiled with. Shared counters
 * are defined once in test_main.c and referenced by every unit_*.c file. */
#ifndef KERN_TEST_H
#define KERN_TEST_H

#include <stdio.h>
#include <string.h>

extern int kt_checks;         /* total CHECK_* evaluated            */
extern int kt_failed_checks;  /* total CHECK_* that failed          */
extern int kt_cur_failed;     /* failures within the running test   */

#define CHECK(cond) do {                                                       \
    kt_checks++;                                                               \
    if (!(cond)) { kt_cur_failed++; kt_failed_checks++;                        \
      fprintf(stderr, "  FAIL %s:%d  CHECK(%s)\n", __FILE__, __LINE__, #cond); }\
  } while (0)

#define CHECK_IEQ(got, want) do {                                              \
    kt_checks++;                                                               \
    long _g = (long)(got), _w = (long)(want);                                  \
    if (_g != _w) { kt_cur_failed++; kt_failed_checks++;                       \
      fprintf(stderr, "  FAIL %s:%d  %s == %s  (got %ld, want %ld)\n",         \
              __FILE__, __LINE__, #got, #want, _g, _w); }                      \
  } while (0)

#define CHECK_SEQ(got, want) do {                                             \
    kt_checks++;                                                              \
    const char *_g = (got), *_w = (want);                                     \
    if (strcmp(_g, _w) != 0) { kt_cur_failed++; kt_failed_checks++;           \
      fprintf(stderr, "  FAIL %s:%d  %s == %s  (got \"%s\", want \"%s\")\n",   \
              __FILE__, __LINE__, #got, #want, _g, _w); }                      \
  } while (0)

typedef void (*kt_test_fn)(void);
void kt_run(const char *name, kt_test_fn fn);
#define RUN(fn) kt_run(#fn, (fn))

#endif /* KERN_TEST_H */
