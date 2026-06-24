/* unit_buffer.c — unit tests for Editor/buffer.c (load/save, growth, region). */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include "test.h"
#include "ed_fixture.h"

/* ---- temp-file helpers ---- */

/* Write `data` to a fresh temp file; `path` receives its name. Returns 0/-1. */
static int temp_write(char *path, size_t pathsz, const char *data, size_t len) {
  snprintf(path, pathsz, "/tmp/kern_test_XXXXXX");
  int fd = mkstemp(path);
  if (fd < 0) return -1;
  ssize_t w = write(fd, data, len);
  close(fd);
  return (w == (ssize_t)len) ? 0 : -1;
}

/* Read a whole file into `out` (NUL-terminated). Returns byte count or -1. */
static long read_file(const char *path, char *out, size_t outsz) {
  FILE *f = fopen(path, "rb");
  if (!f) return -1;
  size_t n = fread(out, 1, outsz - 1, f);
  fclose(f);
  out[n] = '\0';
  return (long)n;
}

/* ---- load ---- */

static void test_load_basic(void) {
  char path[64];
  const char *in = "alpha\nbeta\ngamma";
  CHECK_IEQ(temp_write(path, sizeof path, in, strlen(in)), 0);

  EditorState ed = {0};
  CHECK_IEQ(buf_load_file(&ed, path), 0);
  CHECK_IEQ(ed.line_count, 3);
  CHECK_SEQ(LINE(ed, 0), "alpha");
  CHECK_SEQ(LINE(ed, 1), "beta");
  CHECK_SEQ(LINE(ed, 2), "gamma");
  ed_teardown(&ed);
  unlink(path);
}

static void test_load_strips_crlf(void) {
  char path[64];
  const char *in = "x\r\ny\r\n";
  CHECK_IEQ(temp_write(path, sizeof path, in, strlen(in)), 0);

  EditorState ed = {0};
  CHECK_IEQ(buf_load_file(&ed, path), 0);
  CHECK_IEQ(ed.line_count, 2);
  CHECK_SEQ(LINE(ed, 0), "x");        /* trailing \r removed */
  CHECK_SEQ(LINE(ed, 1), "y");
  ed_teardown(&ed);
  unlink(path);
}

static void test_load_missing_returns_error(void) {
  EditorState ed = {0};
  buf_init_empty(&ed);
  CHECK_IEQ(buf_load_file(&ed, "/tmp/kern_does_not_exist_zzz"), -1);
  ed_teardown(&ed);
}

static void test_load_splits_overlong_line(void) {
  size_t big = (size_t)MAX_LINE_LEN + 10;
  char *in = malloc(big);
  memset(in, 'a', big);
  char path[64];
  CHECK_IEQ(temp_write(path, sizeof path, in, big), 0);
  free(in);

  EditorState ed = {0};
  CHECK_IEQ(buf_load_file(&ed, path), 0);
  CHECK_IEQ(ed.line_count, 2);
  CHECK_IEQ(ed.lines[0].len, MAX_LINE_LEN);
  CHECK_IEQ(ed.lines[1].len, 10);
  ed_teardown(&ed);
  unlink(path);
}

/* ---- save / round-trip ---- */

static void test_save_load_roundtrip(void) {
  char src[64], dst[64];
  const char *in = "alpha\nbeta\ngamma";   /* no trailing newline */
  CHECK_IEQ(temp_write(src, sizeof src, in, strlen(in)), 0);

  EditorState ed = {0};
  CHECK_IEQ(buf_load_file(&ed, src), 0);

  snprintf(dst, sizeof dst, "/tmp/kern_test_out_XXXXXX");
  int fd = mkstemp(dst);
  CHECK(fd >= 0);
  close(fd);
  CHECK_IEQ(buf_save(&ed, dst), 0);

  char out[256];
  long n = read_file(dst, out, sizeof out);
  CHECK_IEQ(n, (long)strlen(in));
  CHECK_SEQ(out, in);                  /* bytes survive the round-trip */

  ed_teardown(&ed);
  unlink(src);
  unlink(dst);
}

/* ---- line-array growth (beyond the initial 4096 cap) ---- */

static void test_line_array_growth(void) {
  EditorState ed = {0};
  buf_init_empty(&ed);                 /* starts with 1 empty line */
  for (int i = 0; i < 5000; i++)
    buf_insert_line_at(&ed, ed.line_count, "x", 1);
  CHECK_IEQ(ed.line_count, 5001);
  CHECK_SEQ(LINE(ed, 5000), "x");
  CHECK_SEQ(LINE(ed, 0), "");
  ed_teardown(&ed);
}

/* ---- mark / region ordering ---- */

static void test_region_orders_endpoints(void) {
  EditorState ed = {0};
  buf_init_empty(&ed);
  buf_insert_line_at(&ed, 1, "second", 6);
  buf_insert_line_at(&ed, 2, "third", 5);

  /* mark before cursor: forward */
  ed.cursor_line = 0; ed.cursor_col = 1;
  buf_mark_set(&ed);
  ed.cursor_line = 2; ed.cursor_col = 3;
  int sl, sc, el, ec;
  buf_region_ordered(&ed, &sl, &sc, &el, &ec);
  CHECK_IEQ(sl, 0); CHECK_IEQ(sc, 1);
  CHECK_IEQ(el, 2); CHECK_IEQ(ec, 3);

  /* mark after cursor: still ordered start<=end */
  ed.cursor_line = 2; ed.cursor_col = 3;
  buf_mark_set(&ed);
  ed.cursor_line = 0; ed.cursor_col = 1;
  buf_region_ordered(&ed, &sl, &sc, &el, &ec);
  CHECK_IEQ(sl, 0); CHECK_IEQ(sc, 1);
  CHECK_IEQ(el, 2); CHECK_IEQ(ec, 3);

  ed_teardown(&ed);
}

/* ---- documents-dir path resolution & filename completion ---- */

static void touch(const char *dir, const char *name) {
  char p[1100];
  snprintf(p, sizeof p, "%s/%s", dir, name);
  FILE *f = fopen(p, "w");
  if (f) fclose(f);
}

static void rm(const char *dir, const char *name) {
  char p[1100];
  snprintf(p, sizeof p, "%s/%s", dir, name);
  unlink(p);
}

static void test_resolve_path(void) {
  char dir[] = "/tmp/kern_docs_XXXXXX";
  CHECK(mkdtemp(dir) != NULL);
  buf_set_documents_dir(dir);

  char out[1200], want[1200];

  buf_resolve_path("note.md", out, sizeof out);          /* relative → under dir */
  snprintf(want, sizeof want, "%s/note.md", dir);
  CHECK_SEQ(out, want);

  buf_resolve_path("/etc/passwd", out, sizeof out);      /* external → basename */
  snprintf(want, sizeof want, "%s/passwd", dir);
  CHECK_SEQ(out, want);

  buf_resolve_path(want, out, sizeof out);               /* already resolved → as-is */
  CHECK_SEQ(out, want);

  buf_set_documents_dir("");                             /* reset global */
  rmdir(dir);
}

static void test_complete_and_list_filenames(void) {
  char dir[] = "/tmp/kern_docs_XXXXXX";
  CHECK(mkdtemp(dir) != NULL);
  touch(dir, "alpha.md");
  touch(dir, "alphabet.md");
  touch(dir, "beta.md");
  buf_set_documents_dir(dir);

  char out[1024];
  CHECK_IEQ(buf_complete_filename("alpha", out, sizeof out), 1);
  CHECK_SEQ(out, "alpha.md");                  /* alphabetically-first longer match */
  CHECK_IEQ(buf_complete_filename("zzz", out, sizeof out), 0);

  char list[8][256];
  int n = buf_list_matches("a", list, 8);
  CHECK_IEQ(n, 2);
  CHECK_SEQ(list[0], "alpha.md");
  CHECK_SEQ(list[1], "alphabet.md");

  buf_set_documents_dir("");
  rm(dir, "alpha.md"); rm(dir, "alphabet.md"); rm(dir, "beta.md");
  rmdir(dir);
}

void suite_buffer(void) {
  RUN(test_load_basic);
  RUN(test_load_strips_crlf);
  RUN(test_load_missing_returns_error);
  RUN(test_load_splits_overlong_line);
  RUN(test_save_load_roundtrip);
  RUN(test_line_array_growth);
  RUN(test_region_orders_endpoints);
  RUN(test_resolve_path);
  RUN(test_complete_and_list_filenames);
}
