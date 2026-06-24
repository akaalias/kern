/* perf_main.c — headless performance harness for Kern.
 *
 * Loads a corpus and times the operations that drive interactive latency on
 * large files: load, full wrap computation (what "jump to end" needs), cursor
 * mapping, incremental search, and a full re-wrap (as on window resize). Uses
 * the deterministic stub renderer so we measure the *algorithm*, not font
 * rasterization — which is exactly what a refactor must keep parity on.
 *
 * Usage: kern_perf <corpus-file> [--check]
 *   --check fails (exit 1) if any scenario exceeds its budget (generous, to
 *   catch gross/algorithmic regressions, not micro-noise).
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include "editor_types.h"
#include "buffer.h"
#include "navigation.h"

static double now_ms(void) {
  struct timespec ts;
  clock_gettime(CLOCK_MONOTONIC, &ts);
  return ts.tv_sec * 1000.0 + ts.tv_nsec / 1.0e6;
}

#define TIME(label_var, stmt) do {       \
    double _t0 = now_ms();               \
    stmt;                                \
    label_var = now_ms() - _t0;          \
  } while (0)

typedef struct { const char *name; double ms; double budget_ms; } Result;

int main(int argc, char **argv) {
  if (argc < 2) { fprintf(stderr, "usage: kern_perf <corpus> [--check]\n"); return 2; }
  const char *path = argv[1];
  int check = (argc > 2 && strcmp(argv[2], "--check") == 0);

  EditorState ed = {0};
  ViewState vs = {0};
  vs.content_y = 80;
  vs.content_h = 600;
  vs.font_size = 18.0f;

  Result r[8];
  int n = 0;

  /* 1. load the whole file into the buffer */
  double load_ms;
  TIME(load_ms, {
    if (buf_load_file(&ed, path) != 0) {
      fprintf(stderr, "failed to load %s\n", path);
      return 2;
    }
  });
  r[n++] = (Result){ "load", load_ms, 4000 };

  long bytes = 0;
  for (int i = 0; i < ed.line_count; i++) bytes += ed.lines[i].len + 1;
  printf("corpus: %s  (%d lines, ~%.1f MB)\n\n", path, ed.line_count, bytes / 1e6);

  /* 2. cold wrap of the entire document — what computing the scrollbar /
        jumping to the end requires the first time */
  int total_cold = 0;
  double wrap_cold_ms;
  TIME(wrap_cold_ms, { total_cold = nav_total_visual_lines(&ed); });
  r[n++] = (Result){ "wrap-all (cold)", wrap_cold_ms, 6000 };

  /* 3. warm re-query — must be cheap (cache hit) */
  double wrap_warm_ms;
  TIME(wrap_warm_ms, { (void)nav_total_visual_lines(&ed); });
  r[n++] = (Result){ "wrap-all (warm)", wrap_warm_ms, 50 };

  /* 4. jump to end: map the last line to a visual row */
  double jump_end_ms;
  int vis_end = 0;
  TIME(jump_end_ms, {
    vis_end = nav_cursor_to_visual(&ed, ed.line_count - 1,
                                   ed.lines[ed.line_count - 1].len);
  });
  r[n++] = (Result){ "jump-to-end", jump_end_ms, 2000 };

  /* 5. jump back to top */
  double jump_top_ms;
  TIME(jump_top_ms, { (void)nav_cursor_to_visual(&ed, 0, 0); });
  r[n++] = (Result){ "jump-to-top", jump_top_ms, 500 };

  /* 6. incremental search forward for a token that exists throughout */
  vs.search_active = 1;
  vs.search_direction = 1;
  snprintf(vs.search_buf, sizeof vs.search_buf, "consequat");
  vs.search_len = (int)strlen(vs.search_buf);
  double search_ms;
  TIME(search_ms, { nav_search_find_first(&ed, &vs); });
  r[n++] = (Result){ "search-forward", search_ms, 1500 };

  /* 7. full re-wrap, as on a window resize */
  double rewrap_ms;
  TIME(rewrap_ms, {
    buf_invalidate_all_wraps(&ed);
    (void)nav_total_visual_lines(&ed);
  });
  r[n++] = (Result){ "re-wrap (resize)", rewrap_ms, 6000 };

  /* report */
  printf("%-20s %10s %10s\n", "scenario", "ms", "budget");
  printf("%-20s %10s %10s\n", "--------", "--", "------");
  int failed = 0;
  for (int i = 0; i < n; i++) {
    int over = check && r[i].ms > r[i].budget_ms;
    printf("%-20s %10.2f %10.0f%s\n", r[i].name, r[i].ms, r[i].budget_ms,
           over ? "   OVER" : "");
    if (over) failed = 1;
  }
  printf("\n(total visual lines: %d, end row: %d)\n", total_cold, vis_end);

  buf_free_all_lines(&ed);
  free(ed.lines);
  return failed ? 1 : 0;
}
