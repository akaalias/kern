/* buffer.c — Document buffer, file I/O, kill buffer, mark/region */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <sys/stat.h>
#include <dirent.h>
#include <strings.h>
#include "buffer.h"

/* ---- sandboxed document location ---- */

/* All user-entered file paths resolve under this directory (the app's
   sandbox-container Documents folder, set from Swift at launch). When empty
   (e.g. the CLI build), paths are used verbatim. */
static char g_documents_dir[1024] = "";

void buf_set_documents_dir(const char *dir) {
  if (dir) snprintf(g_documents_dir, sizeof(g_documents_dir), "%s", dir);
}

const char *buf_get_documents_dir(void) {
  return g_documents_dir;
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

/* Find the alphabetically-first existing file in the documents dir whose name
   starts with `prefix` (and is longer than it). Writes the full completed name
   to `out` (which therefore begins with `prefix`) and returns 1, else 0.
   Supports a relative subfolder in the prefix (e.g. "notes/dr"). Case-sensitive
   so the completion shares the typed prefix exactly. */
int buf_complete_filename(const char *prefix, char *out, int outsz) {
  if (g_documents_dir[0] == '\0' || !prefix || !*prefix) return 0;
  if (prefix[0] == '/' || prefix[0] == '~') return 0;   /* unreachable in sandbox */

  /* split into a relative subdir and the basename prefix being typed */
  char subdir[1024] = "";
  const char *base = prefix;
  const char *slash = strrchr(prefix, '/');
  if (slash) {
    int dl = (int)(slash - prefix);
    if (dl > (int)sizeof(subdir) - 1) dl = sizeof(subdir) - 1;
    memcpy(subdir, prefix, dl);
    subdir[dl] = '\0';
    base = slash + 1;
  }

  char dirpath[2048];
  if (subdir[0]) snprintf(dirpath, sizeof(dirpath), "%s/%s", g_documents_dir, subdir);
  else           snprintf(dirpath, sizeof(dirpath), "%s", g_documents_dir);

  DIR *d = opendir(dirpath);
  if (!d) return 0;

  size_t blen = strlen(base);
  char best[1024] = "";
  struct dirent *e;
  while ((e = readdir(d)) != NULL) {
    const char *name = e->d_name;
    if (name[0] == '.') continue;                      /* skip ., .., hidden */
    if (strncmp(name, base, blen) != 0) continue;      /* must match prefix */
    if (strlen(name) <= blen) continue;                /* nothing to suggest */
    if (best[0] == '\0' || strcmp(name, best) < 0)
      snprintf(best, sizeof(best), "%s", name);
  }
  closedir(d);
  if (best[0] == '\0') return 0;

  if (subdir[0]) snprintf(out, outsz, "%s/%s", subdir, best);
  else           snprintf(out, outsz, "%s", best);
  return 1;
}

/* Collect up to `max` existing filenames in the documents dir whose names start
   with `prefix` (case-insensitive), alphabetically. Returns the count. Used for
   the wikilink autocomplete dropdown. */
int buf_list_matches(const char *prefix, char out[][256], int max) {
  if (g_documents_dir[0] == '\0' || max <= 0) return 0;
  DIR *d = opendir(g_documents_dir);
  if (!d) return 0;

  size_t plen = strlen(prefix);
  int count = 0;
  struct dirent *e;
  while ((e = readdir(d)) != NULL) {
    const char *name = e->d_name;
    if (name[0] == '.') continue;                       /* skip ., .., hidden */
    if (strlen(name) >= 256) continue;
    if (plen > 0 && strncasecmp(name, prefix, plen) != 0) continue;

    /* insert into out[] keeping it alphabetical, capped at max */
    int idx = 0;
    while (idx < count && strcasecmp(out[idx], name) < 0) idx++;
    if (idx >= max) continue;                           /* sorts after a full list */
    int end = (count < max) ? count : max - 1;          /* last slot to shift into */
    for (int j = end; j > idx; j--) memcpy(out[j], out[j-1], 256);
    snprintf(out[idx], 256, "%s", name);
    if (count < max) count++;
  }
  closedir(d);
  return count;
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
  l->md_spans = NULL;
  l->md_span_count = -1;
}

/* any edit invalidates this line's cached wraps and inline-span map */
void line_dirty(Line *l) { l->wrap_count = -1; l->md_span_count = -1; }

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
  free(ed->lines[idx].md_spans);
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

  /* Drop any buffer already loaded into `ed` before reallocating the line
     array — otherwise loading a second file into the same EditorState (buffer
     switch, wikilink follow/back/forward) leaks the previous array.
     buf_free_all_lines frees the line contents; this frees the array itself.
     Safe when ed->lines is NULL (a fresh EditorState). */
  buf_free_all_lines(ed);
  free(ed->lines);

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
  ed->dirty = 0;   /* freshly loaded == matches disk */
  return 0;
}

void buf_free_all_lines(EditorState *ed) {
  for (int i = 0; i < ed->line_count; i++) {
    free(ed->lines[i].text);
    free(ed->lines[i].md_spans);
  }
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
  ed->dirty = 0;   /* a fresh empty buffer has nothing to save */
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
  if (ok) ed->dirty = 0;   /* in sync with disk again */
  return ok ? 0 : -1;
}

/* Derive a note filename base from the first line of `text`: keep letters,
   digits and spaces (so "Foo Bar Baz" stays intact), drop other punctuation,
   and trim surrounding spaces. Writes a NUL-terminated result — empty if the
   first line has nothing usable. Does not append ".md". */
void buf_sanitize_note_title(const char *text, int len, char *out, int outsz) {
  if (outsz <= 0) return;
  int oi = 0;
  for (int i = 0; i < len && text[i] != '\n' && oi < outsz - 1; i++) {
    unsigned char c = (unsigned char)text[i];
    if (isalnum(c) || c == ' ') out[oi++] = (char)c;
  }
  out[oi] = '\0';
  int s = 0;
  while (out[s] == ' ') s++;
  if (s > 0) memmove(out, out + s, strlen(out + s) + 1);
  int e = (int)strlen(out);
  while (e > 0 && out[e - 1] == ' ') out[--e] = '\0';
}

/* Write a raw text blob to `path` (creating parent dirs). Used to spin off a
   new note file without disturbing the open EditorState. Returns 0 on success. */
int buf_save_text(const char *path, const char *text, int len) {
  mkdir_parents(path);
  FILE *f = fopen(path, "wb");
  if (!f) return -1;
  if (len > 0) fwrite(text, 1, len, f);
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
