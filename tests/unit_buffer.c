/* unit_buffer.c — unit tests for Editor/buffer.c (load/save, growth, region). */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/stat.h>
#include "test.h"
#include "ed_fixture.h"
#include "recent.h"

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

/* buf_save stops at the read-only Context section (readonly_from), so the
   auto-generated section is never written to disk. */
static void test_save_excludes_readonly_section(void) {
  EditorState ed = {0};
  buf_init_empty(&ed);
  line_ensure_cap(&ed.lines[0], 5);
  memcpy(ed.lines[0].text, "hello", 6);
  ed.lines[0].len = 5;
  /* a fake static section below the one document line */
  buf_insert_line_at(&ed, 1, "", 0);              /* separator */
  buf_insert_line_at(&ed, 2, "## Context", 10);
  buf_insert_line_at(&ed, 3, "- [[Other.md]]", 14);
  ed.readonly_from = 1;                            /* section starts at the separator */
  CHECK_IEQ(buf_content_line_count(&ed), 1);

  char dst[64]; snprintf(dst, sizeof dst, "/tmp/kern_test_ro_XXXXXX");
  int fd = mkstemp(dst); CHECK(fd >= 0); close(fd);
  CHECK_IEQ(buf_save(&ed, dst), 0);

  char out[256];
  long n = read_file(dst, out, sizeof out);
  CHECK_IEQ(n, 5);                                 /* just "hello" — no section */
  CHECK_SEQ(out, "hello");
  ed_teardown(&ed);
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
  recent_reset();                              /* no MRU → ties break alphabetically */

  char out[1024];
  CHECK_IEQ(buf_complete_filename("alpha", out, sizeof out), 1);
  CHECK_SEQ(out, "alpha.md");                  /* alphabetically-first longer match */
  CHECK_IEQ(buf_complete_filename("zzz", out, sizeof out), 0);

  /* fuzzy subsequence match: "a" is in all three names; the two anchored at the
     start (alpha, alphabet) outrank "beta" (mid-word 'a'). */
  char list[8][256];
  int n = buf_list_matches("a", list, 8);
  CHECK_IEQ(n, 3);
  CHECK_SEQ(list[0], "alpha.md");
  CHECK_SEQ(list[1], "alphabet.md");
  CHECK_SEQ(list[2], "beta.md");

  /* a non-prefix subsequence still matches: "lph" → "alpha.md" / "alphabet.md" */
  n = buf_list_matches("lph", list, 8);
  CHECK_IEQ(n, 2);
  CHECK_SEQ(list[0], "alpha.md");
  CHECK_SEQ(list[1], "alphabet.md");

  /* "abt" is a subsequence of "alphabet.md" only */
  n = buf_list_matches("abt", list, 8);
  CHECK_IEQ(n, 1);
  CHECK_SEQ(list[0], "alphabet.md");

  /* a char in none of the names yields nothing */
  CHECK_IEQ(buf_list_matches("z", list, 8), 0);

  /* recency ranking: on an empty query (all tie at score 0) the most-recently
     opened note floats to the top; the current file (MRU index 0) is excluded. */
  recent_reset();
  char pa[1100], pb[1100];
  snprintf(pa, sizeof pa, "%s/alpha.md", dir);
  snprintf(pb, sizeof pb, "%s/beta.md", dir);
  recent_push(pa);            /* older */
  recent_push(pb);            /* most recent → MRU index 0 = "current file" */
  n = buf_list_matches("", list, 8);
  CHECK_IEQ(n, 2);            /* beta.md excluded as the current file */
  CHECK_SEQ(list[0], "alpha.md");   /* the recent (non-current) note leads */
  recent_reset();

  buf_set_documents_dir("");
  rm(dir, "alpha.md"); rm(dir, "alphabet.md"); rm(dir, "beta.md");
  rmdir(dir);
}

/* getter round-trips the documents dir; when unset, paths resolve verbatim. */
static void test_documents_dir_and_unsandboxed_resolve(void) {
  buf_set_documents_dir("/some/docs");
  CHECK_SEQ(buf_get_documents_dir(), "/some/docs");

  buf_set_documents_dir("");                  /* not sandboxed */
  char out[128];
  buf_resolve_path("foo.md", out, sizeof out);
  CHECK_SEQ(out, "foo.md");                   /* used as-is */
}

/* completion across a relative subfolder ("notes/dr" → "notes/draft.md"). */
static void test_complete_filename_subdir(void) {
  char dir[] = "/tmp/kern_docs_XXXXXX";
  CHECK(mkdtemp(dir) != NULL);
  char sub[1200]; snprintf(sub, sizeof sub, "%s/notes", dir);
  CHECK_IEQ(mkdir(sub, 0755), 0);
  char fp[1400]; snprintf(fp, sizeof fp, "%s/draft.md", sub);
  FILE *f = fopen(fp, "w"); if (f) fclose(f);

  buf_set_documents_dir(dir);
  char out[1024];
  CHECK_IEQ(buf_complete_filename("notes/dr", out, sizeof out), 1);
  CHECK_SEQ(out, "notes/draft.md");

  buf_set_documents_dir("");
  unlink(fp); rmdir(sub); rmdir(dir);
}

/* buf_save_text writes a blob; loading an empty file yields one empty line. */
static void test_save_text_and_empty_load(void) {
  char dir[] = "/tmp/kern_docs_XXXXXX";
  CHECK(mkdtemp(dir) != NULL);

  char path[1200]; snprintf(path, sizeof path, "%s/note.md", dir);
  CHECK_IEQ(buf_save_text(path, "hi there", 8), 0);
  char out[64];
  CHECK_IEQ(read_file(path, out, sizeof out), 8);
  CHECK_SEQ(out, "hi there");

  char ep[1200]; snprintf(ep, sizeof ep, "%s/empty.md", dir);
  CHECK_IEQ(buf_save_text(ep, "", 0), 0);     /* zero-length write */
  EditorState ed = {0};
  CHECK_IEQ(buf_load_file(&ed, ep), 0);
  CHECK_IEQ(ed.line_count, 1);
  CHECK_SEQ(LINE(ed, 0), "");
  ed_teardown(&ed);

  unlink(path); unlink(ep); rmdir(dir);
}

/* leading-whitespace skip, "~" external path, and empty-name → untitled. */
static void test_resolve_path_branches(void) {
  char dir[] = "/tmp/kern_docs_XXXXXX";
  CHECK(mkdtemp(dir) != NULL);
  buf_set_documents_dir(dir);

  char out[1300], want[1300];
  buf_resolve_path("  spaced.md", out, sizeof out);    /* leading spaces skipped */
  snprintf(want, sizeof want, "%s/spaced.md", dir);
  CHECK_SEQ(out, want);

  buf_resolve_path("~/x.md", out, sizeof out);         /* "~" → basename only */
  snprintf(want, sizeof want, "%s/x.md", dir);
  CHECK_SEQ(out, want);

  buf_resolve_path("/", out, sizeof out);              /* nothing usable → untitled */
  snprintf(want, sizeof want, "%s/untitled.txt", dir);
  CHECK_SEQ(out, want);

  buf_set_documents_dir("");
  rmdir(dir);
}

static void test_complete_filename_edge_branches(void) {
  char out[1024];
  buf_set_documents_dir("");
  CHECK_IEQ(buf_complete_filename("x", out, sizeof out), 0);    /* no docs dir */

  char dir[] = "/tmp/kern_docs_XXXXXX";
  CHECK(mkdtemp(dir) != NULL);
  buf_set_documents_dir(dir);
  CHECK_IEQ(buf_complete_filename("", out, sizeof out), 0);     /* empty prefix */
  CHECK_IEQ(buf_complete_filename("/abs", out, sizeof out), 0); /* absolute prefix */
  CHECK_IEQ(buf_complete_filename("nope/x", out, sizeof out), 0); /* opendir fails */

  buf_set_documents_dir("");
  rmdir(dir);
}

static void test_list_matches_edge_branches(void) {
  char list[8][256];
  buf_set_documents_dir("");
  CHECK_IEQ(buf_list_matches("a", list, 8), 0);    /* no docs dir */

  char dir[] = "/tmp/kern_docs_XXXXXX";
  CHECK(mkdtemp(dir) != NULL);
  buf_set_documents_dir(dir);
  CHECK_IEQ(buf_list_matches("a", list, 0), 0);    /* max <= 0 */

  buf_set_documents_dir("/no/such/dir/zzz");
  CHECK_IEQ(buf_list_matches("a", list, 8), 0);    /* opendir fails */

  buf_set_documents_dir("");
  rmdir(dir);
}

static void test_sanitize_zero_outsz_is_noop(void) {
  char out[2] = "Z";
  buf_sanitize_note_title("hi", 2, out, 0);        /* outsz <= 0 → returns at once */
  CHECK_IEQ(out[0], 'Z');                          /* buffer untouched */
}

/* Re-initializing an existing buffer frees the old lines (the non-NULL branch). */
static void test_init_empty_reinit(void) {
  EditorState ed = {0};
  buf_init_empty(&ed);                             /* lines == NULL → allocate */
  buf_insert_line_at(&ed, 1, "x", 1);
  buf_init_empty(&ed);                             /* lines != NULL → free + reset */
  CHECK_IEQ(ed.line_count, 1);
  CHECK_SEQ(LINE(ed, 0), "");
  ed_teardown(&ed);
}

static void test_save_unwritable_path_fails(void) {
  EditorState ed = {0};
  buf_init_empty(&ed);
  CHECK_IEQ(buf_save(&ed, "/dev/null/x.md"), -1);  /* parent isn't a directory */
  ed_teardown(&ed);
}

static void test_sanitize_note_title(void) {
  char out[64];
  /* spaces between words are kept */
  buf_sanitize_note_title("Foo Bar Baz", 11, out, sizeof out);
  CHECK_SEQ(out, "Foo Bar Baz");
  /* punctuation and markdown markers dropped; surrounding spaces trimmed */
  buf_sanitize_note_title("## Heading: Wow!", 16, out, sizeof out);
  CHECK_SEQ(out, "Heading Wow");
  /* only the first line is used */
  buf_sanitize_note_title("first line\nsecond", 17, out, sizeof out);
  CHECK_SEQ(out, "first line");
  /* nothing usable -> empty (caller refuses) */
  buf_sanitize_note_title("###", 3, out, sizeof out);
  CHECK_SEQ(out, "");
}

void suite_buffer(void) {
  RUN(test_load_basic);
  RUN(test_load_strips_crlf);
  RUN(test_load_missing_returns_error);
  RUN(test_load_splits_overlong_line);
  RUN(test_save_load_roundtrip);
  RUN(test_save_excludes_readonly_section);
  RUN(test_line_array_growth);
  RUN(test_region_orders_endpoints);
  RUN(test_resolve_path);
  RUN(test_complete_and_list_filenames);
  RUN(test_documents_dir_and_unsandboxed_resolve);
  RUN(test_complete_filename_subdir);
  RUN(test_save_text_and_empty_load);
  RUN(test_resolve_path_branches);
  RUN(test_complete_filename_edge_branches);
  RUN(test_list_matches_edge_branches);
  RUN(test_sanitize_zero_outsz_is_noop);
  RUN(test_init_empty_reinit);
  RUN(test_save_unwritable_path_fails);
  RUN(test_sanitize_note_title);
}
