/* buffer.c — Document buffer, file I/O, kill buffer, mark/region */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <sys/stat.h>
#include <dirent.h>
#include <strings.h>
#include "buffer.h"
#include "undo.h"
#include "recent.h"   /* MRU ranking for wikilink suggestions */

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

/* Does the basename of `path` end in a note extension (.md/.markdown/.txt)? */
static int has_note_ext(const char *path) {
  const char *base = strrchr(path, '/');
  base = base ? base + 1 : path;
  const char *dot = strrchr(base, '.');
  return dot && (strcmp(dot, ".md") == 0 || strcmp(dot, ".markdown") == 0 ||
                 strcmp(dot, ".txt") == 0);
}

static int file_exists(const char *path) {
  struct stat st;
  return stat(path, &st) == 0;
}

void buf_resolve_note_path(const char *input, char *out, int outsz) {
  buf_resolve_path(input, out, outsz);
  if (has_note_ext(out)) return;             /* explicit note name — use as-is */
  if (file_exists(out)) return;              /* an existing file wins verbatim */
  static const char *exts[] = { ".md", ".markdown", ".txt" };
  char cand[2304];
  for (int i = 0; i < 3; i++) {
    snprintf(cand, sizeof cand, "%s%s", out, exts[i]);
    if (file_exists(cand)) {
      snprintf(out, outsz, "%s", cand);
      return;
    }
  }
  snprintf(cand, sizeof cand, "%s.md", out); /* brand-new note: create as .md */
  snprintf(out, outsz, "%s", cand);
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

/* Case-insensitive substring search: byte offset of `needle` within `hay`, or
   -1. An empty needle matches at 0. */
static int find_ci(const char *hay, const char *needle) {
  if (!*needle) return 0;
  for (int i = 0; hay[i]; i++) {
    int j = 0;
    while (needle[j] && tolower((unsigned char)hay[i + j]) == tolower((unsigned char)needle[j])) j++;
    if (!needle[j]) return i;
  }
  return -1;
}

/* Match score of query `q` against filename `name`: -1 if `name` doesn't contain
   `q` as a (case-insensitive) substring, else higher is better — a prefix match
   beats a word-start match beats a mid-word match. This is a literal "contains"
   filter, NOT a loose subsequence ("Hello" matches "Hello World" / "My Hello"
   but not "histories"). An empty query scores 0 (the recent-files case is
   handled by the caller). */
static int match_score(const char *name, const char *q) {
  if (!*q) return 0;
  int off = find_ci(name, q);
  if (off < 0) return -1;
  if (off == 0) return 100;                    /* name starts with the query */
  unsigned char p = (unsigned char)name[off - 1];
  if (p == ' ' || p == '-' || p == '_') return 50;   /* a word in the name starts with it */
  return 10;                                   /* substring somewhere inside a word */
}

/* Note files only (wikilinks point at notes, not PDFs/images in the vault). */
static int is_note_file(const char *name) {
  const char *dot = strrchr(name, '.');
  if (!dot) return 0;
  return strcasecmp(dot, ".md") == 0 || strcasecmp(dot, ".markdown") == 0 ||
         strcasecmp(dot, ".txt") == 0 || strcasecmp(dot, ".text") == 0;
}

/* Position of `name` in the most-recently-used list (0 = most recent, which is
   the currently-open file), or a large sentinel if it isn't in the MRU. Compared
   on basename, case-insensitively, since the MRU stores full paths. */
#define RECENCY_NONE 100000
static int recency_rank(const char *name) {
  int n = recent_count();
  for (int i = 0; i < n; i++) {
    const char *p = recent_get(i);
    if (p && strcasecmp(path_base(p), name) == 0) return i;
  }
  return RECENCY_NONE;
}

/* Collect up to `max` note filenames in the documents dir for the wikilink
   autocomplete. Two modes:
     - empty prefix → the most-recently-opened notes only (MRU order), so a bare
       "[[" proposes where you've been, not a random slice of the vault;
     - non-empty   → notes whose name *contains* the query (case-insensitive
       substring; "Hello" → "Hello World" / "My Hello", never "histories").
   Either way only note files count (.md/.markdown/.txt), the currently-open file
   (MRU index 0) is excluded, and results rank by match quality, then recency,
   then alphabetically. Returns the count. */
int buf_list_matches(const char *prefix, char out[][256], int max) {
  if (g_documents_dir[0] == '\0' || max <= 0) return 0;
  DIR *d = opendir(g_documents_dir);
  if (!d) return 0;

  int empty = (prefix[0] == '\0');
  enum { CAND_MAX = 1024 };
  static char names[CAND_MAX][256];
  static int  scores[CAND_MAX];
  static int  recency[CAND_MAX];
  int ncand = 0;
  struct dirent *e;
  while ((e = readdir(d)) != NULL && ncand < CAND_MAX) {
    const char *name = e->d_name;
    if (name[0] == '.') continue;                       /* skip ., .., hidden */
    if (strlen(name) >= 256) continue;
    if (!is_note_file(name)) continue;                  /* notes only, not PDFs etc. */
    int rec = recency_rank(name);
    if (rec == 0) continue;                             /* the current file — skip self */
    if (empty && rec == RECENCY_NONE) continue;         /* bare "[[" → recent notes only */
    int sc = match_score(name, prefix);
    if (sc < 0) continue;                               /* doesn't contain the query */
    snprintf(names[ncand], 256, "%s", name);
    scores[ncand] = sc;
    recency[ncand] = rec;
    ncand++;
  }
  closedir(d);

  /* selection-sort the top `max`: score desc, then recency asc, then name asc */
  int count = ncand < max ? ncand : max;
  for (int i = 0; i < count; i++) {
    int best = i;
    for (int j = i + 1; j < ncand; j++) {
      int cmp;
      if (scores[j] != scores[best])        cmp = scores[j] - scores[best];        /* >0 → j better */
      else if (recency[j] != recency[best]) cmp = recency[best] - recency[j];       /* fewer = better */
      else                                  cmp = strcasecmp(names[best], names[j]);/* a < b → j better */
      if (cmp > 0) best = j;
    }
    if (best != i) {
      int ts = scores[i]; scores[i] = scores[best]; scores[best] = ts;
      int tr = recency[i]; recency[i] = recency[best]; recency[best] = tr;
      char tn[256];
      memcpy(tn, names[i], 256);
      memcpy(names[i], names[best], 256);
      memcpy(names[best], tn, 256);
    }
    memcpy(out[i], names[i], 256);
  }
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
  l->pos_spans = NULL;
  l->pos_span_count = -1;
  l->style_spans = NULL;
  l->style_span_count = -1;
  l->sub_spans = NULL;
  l->sub_span_count = -1;
}

/* any edit invalidates this line's cached wraps, inline spans, POS tags,
   style-check spans, and symbol-substitution spans */
static unsigned long g_edit_seq;
unsigned long buf_edit_seq(void) { return g_edit_seq; }

void line_dirty(Line *l) {
  l->wrap_count = -1; l->md_span_count = -1;
  l->pos_span_count = -1; l->style_span_count = -1;
  l->sub_span_count = -1;
  g_edit_seq++;
}

/* Free every per-line span cache (md / pos / style / sub). One place to add a
   new span layer's free, so a new layer can't leak by being forgotten at a
   free site. Does not touch l->text. */
static void line_free_spans(Line *l) {
  free(l->md_spans);
  free(l->pos_spans);
  free(l->style_spans);
  free(l->sub_spans);
}

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
  /* keep the read-only Context boundary tracking the section as content above it
     grows (a line added at or before the boundary pushes the section down) */
  if (ed->readonly_from > 0 && idx <= ed->readonly_from) ed->readonly_from++;
}

void buf_delete_line_at(EditorState *ed, int idx) {
  free(ed->lines[idx].text);
  line_free_spans(&ed->lines[idx]);
  memmove(&ed->lines[idx], &ed->lines[idx + 1], (ed->line_count - idx - 1) * sizeof(Line));
  ed->line_count--;
  if (ed->readonly_from > 0 && idx < ed->readonly_from) ed->readonly_from--;
}

/* Append line+1 onto `line` (the "+1 keeps NUL" copy), then delete line+1 and
   mark the merged line dirty. The shared mechanic behind every line join:
   backspace/delete at a line boundary, kill-to-end-of-line, and undo of a
   split. Caller guarantees line+1 exists. */
void buf_join_line_with_next(EditorState *ed, int line) {
  Line *l = &ed->lines[line];
  Line *next = &ed->lines[line + 1];
  line_ensure_cap(l, l->len + next->len);
  memcpy(l->text + l->len, next->text, next->len + 1);  /* +1 keeps the NUL */
  l->len += next->len;
  buf_delete_line_at(ed, line + 1);
  line_dirty(l);
}

/* Clamp the cursor into the buffer (line into [0,line_count), col into
   [0,line.len]). Used after edits that may shrink the buffer under the caret. */
void buf_clamp_cursor(EditorState *ed) {
  if (ed->cursor_line >= ed->line_count) ed->cursor_line = ed->line_count - 1;
  if (ed->cursor_line < 0) ed->cursor_line = 0;
  if (ed->cursor_col > ed->lines[ed->cursor_line].len)
    ed->cursor_col = ed->lines[ed->cursor_line].len;
  if (ed->cursor_col < 0) ed->cursor_col = 0;
}

/* Number of lines that belong to the document proper (excludes the auto-generated
   read-only Context section). Save/serialize use this so the section never hits
   disk or the clipboard/publish payload. */
int buf_content_line_count(const EditorState *ed) {
  return ed->readonly_from > 0 ? ed->readonly_from : ed->line_count;
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
  ed->dirty = 0;        /* freshly loaded == matches disk */
  ed->readonly_from = 0;/* no Context section yet; caller regenerates one */
  undo_clear(ed);       /* old file's undo ops reference a buffer that no longer exists */
  return 0;
}

void buf_free_all_lines(EditorState *ed) {
  for (int i = 0; i < ed->line_count; i++) {
    free(ed->lines[i].text);
    line_free_spans(&ed->lines[i]);
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
  ed->readonly_from = 0;
  undo_clear(ed);  /* no history carries across a buffer reset */
}

int buf_save(EditorState *ed, const char *path) {
  mkdir_parents(path);
  FILE *f = fopen(path, "wb");
  if (!f) return -1;
  int n = buf_content_line_count(ed);   /* never write the read-only Context section */
  for (int i = 0; i < n; i++) {
    fwrite(ed->lines[i].text, 1, ed->lines[i].len, f);
    if (i < n - 1) fwrite("\n", 1, 1, f);
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

/* Grow the kill buffer to hold at least `need` bytes (the caller includes room
   for the NUL). Returns 1 on success, 0 if the realloc failed. */
static int kill_ensure_cap(EditorState *ed, int need) {
  if (need <= ed->kill_cap) return 1;
  int cap = need * 2;
  char *tmp = realloc(ed->kill_buf, cap);
  if (!tmp) return 0;
  ed->kill_buf = tmp;
  ed->kill_cap = cap;
  return 1;
}

void buf_kill_set(EditorState *ed, const char *text, int len) {
  if (!kill_ensure_cap(ed, len + 1)) return;
  memcpy(ed->kill_buf, text, len);
  ed->kill_buf[len] = '\0';
  ed->kill_len = len;
}

void buf_kill_append(EditorState *ed, const char *text, int len) {
  if (!kill_ensure_cap(ed, ed->kill_len + len + 1)) return;
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
