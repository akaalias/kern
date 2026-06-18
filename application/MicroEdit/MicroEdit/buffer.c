/* buffer.c — Document buffer, file I/O, kill buffer, mark/region */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include "buffer.h"

/* ---- sandboxed document location ---- */

/* All user-entered file paths resolve under this directory (the app's
   sandbox-container Documents folder, set from Swift at launch). When empty
   (e.g. the CLI build), paths are used verbatim. */
static char g_documents_dir[1024] = "";

void buf_set_documents_dir(const char *dir) {
  if (dir) snprintf(g_documents_dir, sizeof(g_documents_dir), "%s", dir);
}

/* Create the parent directories of `path` (mkdir -p of the dirname). */
static void mkdir_parents(const char *path) {
  char tmp[1024];
  snprintf(tmp, sizeof(tmp), "%s", path);
  char *last = strrchr(tmp, '/');
  if (!last) return;
  *last = '\0';                 /* tmp is now the directory portion */
  for (char *p = tmp + 1; *p; p++) {
    if (*p == '/') { *p = '\0'; mkdir(tmp, 0755); *p = '/'; }
  }
  mkdir(tmp, 0755);
}

/* Map a user-typed name onto a path inside the documents dir. Relative names
   (incl. subfolders) are kept under it; anything absolute or starting with '~'
   we can't reach under the sandbox, so we keep just the filename. Idempotent:
   a path already inside the documents dir is returned unchanged. */
void buf_resolve_path(const char *input, char *out, int outsz) {
  while (*input == ' ' || *input == '\t') input++;

  if (g_documents_dir[0] == '\0') {            /* not sandboxed: use as-is */
    snprintf(out, outsz, "%s", input);
    return;
  }
  size_t dl = strlen(g_documents_dir);
  if (strncmp(input, g_documents_dir, dl) == 0) {   /* already resolved */
    snprintf(out, outsz, "%s", input);
    return;
  }
  const char *rel = input;
  if (rel[0] == '~' || rel[0] == '/') {        /* external path -> basename */
    const char *slash = strrchr(rel, '/');
    rel = slash ? slash + 1 : rel;
  }
  if (*rel == '\0') rel = "untitled.txt";
  snprintf(out, outsz, "%s/%s", g_documents_dir, rel);
}

/* ---- line operations ---- */

void line_ensure_cap(Line *l, int need) {
  if (need + 1 > l->cap) {
    l->cap = (need + 1) * 2;
    l->text = realloc(l->text, l->cap);
  }
}

void line_init(Line *l, const char *s, int len) {
  l->cap = len + 16;
  l->text = malloc(l->cap);
  memcpy(l->text, s, len);
  l->text[len] = '\0';
  l->len = len;
  l->wrap_count = -1;
}

void line_dirty(Line *l) { l->wrap_count = -1; }

void buf_ensure_lines_cap(EditorState *ed, int need) {
  if (need > ed->line_cap) {
    ed->line_cap = need * 2;
    Line *tmp = realloc(ed->lines, ed->line_cap * sizeof(Line));
    if (!tmp) { fprintf(stderr, "out of memory\n"); exit(1); }
    ed->lines = tmp;
  }
}

void buf_insert_line_at(EditorState *ed, int idx, const char *s, int len) {
  buf_ensure_lines_cap(ed, ed->line_count + 1);
  memmove(&ed->lines[idx + 1], &ed->lines[idx], (ed->line_count - idx) * sizeof(Line));
  line_init(&ed->lines[idx], s, len);
  ed->line_count++;
}

void buf_delete_line_at(EditorState *ed, int idx) {
  free(ed->lines[idx].text);
  memmove(&ed->lines[idx], &ed->lines[idx + 1], (ed->line_count - idx - 1) * sizeof(Line));
  ed->line_count--;
}

/* ---- file I/O ---- */

int buf_load_file(EditorState *ed, const char *path) {
  FILE *f = fopen(path, "rb");
  if (!f) return -1;                 /* missing/unreadable -> caller starts fresh */
  fseek(f, 0, SEEK_END);
  long sz = ftell(f);
  if (sz < 0) { fclose(f); return -1; }
  fseek(f, 0, SEEK_SET);
  char *buf = malloc((size_t)sz + 1);
  if (!buf) { fclose(f); return -1; }
  size_t nread = fread(buf, 1, (size_t)sz, f);
  buf[nread] = '\0';
  fclose(f);

  ed->line_cap = 4096;
  ed->lines = malloc(ed->line_cap * sizeof(Line));
  if (!ed->lines) { free(buf); return -1; }
  ed->line_count = 0;

  char *p = buf;
  while (*p) {
    char *nl = strchr(p, '\n');
    int len;
    if (nl) {
      len = nl - p;
      if (len > 0 && p[len - 1] == '\r') len--;
    } else {
      len = strlen(p);
    }
    /* split overlong lines to prevent pathological wrapping/rendering */
    while (len > MAX_LINE_LEN) {
      buf_ensure_lines_cap(ed, ed->line_count + 1);
      line_init(&ed->lines[ed->line_count], p, MAX_LINE_LEN);
      ed->line_count++;
      p += MAX_LINE_LEN;
      len -= MAX_LINE_LEN;
    }
    buf_ensure_lines_cap(ed, ed->line_count + 1);
    line_init(&ed->lines[ed->line_count], p, len);
    ed->line_count++;
    if (nl) p = nl + 1; else break;
  }

  if (ed->line_count == 0) {
    buf_ensure_lines_cap(ed, 1);
    line_init(&ed->lines[0], "", 0);
    ed->line_count = 1;
  }

  free(buf);
  return 0;
}

void buf_free_all_lines(EditorState *ed) {
  for (int i = 0; i < ed->line_count; i++) free(ed->lines[i].text);
  ed->line_count = 0;
}

void buf_init_empty(EditorState *ed) {
  if (ed->lines) buf_free_all_lines(ed);
  else { ed->line_cap = 4096; ed->lines = malloc(ed->line_cap * sizeof(Line)); }
  buf_ensure_lines_cap(ed, 1);
  line_init(&ed->lines[0], "", 0);
  ed->line_count = 1;
  ed->cursor_line = 0;
  ed->cursor_col = 0;
  ed->cursor_target_col = 0;
}

int buf_save(EditorState *ed, const char *path) {
  mkdir_parents(path);
  FILE *f = fopen(path, "wb");
  if (!f) return -1;
  for (int i = 0; i < ed->line_count; i++) {
    fwrite(ed->lines[i].text, 1, ed->lines[i].len, f);
    if (i < ed->line_count - 1) fwrite("\n", 1, 1, f);
  }
  /* surface write/flush errors so the UI can report failure honestly */
  int ok = (ferror(f) == 0);
  if (fclose(f) != 0) ok = 0;
  return ok ? 0 : -1;
}

/* ---- kill buffer ---- */

void buf_kill_set(EditorState *ed, const char *text, int len) {
  if (len + 1 > ed->kill_cap) {
    ed->kill_cap = (len + 1) * 2;
    char *tmp = realloc(ed->kill_buf, ed->kill_cap);
    if (!tmp) return;
    ed->kill_buf = tmp;
  }
  memcpy(ed->kill_buf, text, len);
  ed->kill_buf[len] = '\0';
  ed->kill_len = len;
}

void buf_kill_append(EditorState *ed, const char *text, int len) {
  if (ed->kill_len + len + 1 > ed->kill_cap) {
    ed->kill_cap = (ed->kill_len + len + 1) * 2;
    char *tmp = realloc(ed->kill_buf, ed->kill_cap);
    if (!tmp) return;
    ed->kill_buf = tmp;
  }
  memcpy(ed->kill_buf + ed->kill_len, text, len);
  ed->kill_len += len;
  ed->kill_buf[ed->kill_len] = '\0';
}

/* ---- mark / region ---- */

void buf_mark_set(EditorState *ed) {
  ed->mark_active = 1;
  ed->mark_line = ed->cursor_line;
  ed->mark_col = ed->cursor_col;
}

void buf_mark_clear(EditorState *ed) {
  ed->mark_active = 0;
}

void buf_region_ordered(EditorState *ed, int *sl, int *sc, int *el, int *ec) {
  if (ed->mark_line < ed->cursor_line ||
      (ed->mark_line == ed->cursor_line && ed->mark_col <= ed->cursor_col)) {
    *sl = ed->mark_line; *sc = ed->mark_col;
    *el = ed->cursor_line; *ec = ed->cursor_col;
  } else {
    *sl = ed->cursor_line; *sc = ed->cursor_col;
    *el = ed->mark_line; *ec = ed->mark_col;
  }
}

/* ---- wrap invalidation ---- */

void buf_invalidate_all_wraps(EditorState *ed) {
  for (int i = 0; i < ed->line_count; i++) ed->lines[i].wrap_count = -1;
}
