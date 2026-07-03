/* We provide our own entry point (Swift @main calls editor_main), so tell SDL
   not to redefine main / expect SDL_main. */
#define SDL_MAIN_HANDLED
#include <SDL2/SDL.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <time.h>
#include <pthread.h>
#include <sys/stat.h>
#include <dirent.h>
#include "renderer.h"
#include "gfx.h"
#include "macos_style.h"
#include "editor_types.h"
#include "buffer.h"
#include "navigation.h"
#include "editing.h"
#include "md_render.h"
#include "pos_render.h"
#include "pos_tagger.h"
#include "style_check.h"
#include "sub_render.h"
#include "undo.h"
#include "recent.h"
#include "clipboard.h"
#include "clock.h"
#include "commands.h"
#include "editor_loop.h"

/* ---- two struct instances hold all mutable state ---- */
static EditorState g_ed = {0};
static ViewState   g_vs = {0};

/* ---- X / Twitter publishing bridge (the network + OAuth half lives in Swift,
   KernApp.swift) ---------------------------------------------------------- */
extern void kern_x_publish(const char *text);   /* Swift @_cdecl: async post */
extern int  kern_x_is_connected(void);          /* Swift @_cdecl: 1 if linked */

/* Account identity for the tweet-preview overlay, fetched from /2/users/me at
   connect time (Swift @_cdecl). display_name/handle never NULL (empty if
   unknown); avatar_rgba returns tightly-packed RGBA pixels or NULL if the photo
   hasn't downloaded yet (the overlay then draws an initials disc). */
extern const char         *kern_x_display_name(void);
extern const char         *kern_x_handle(void);
extern const unsigned char *kern_x_avatar_rgba(int *w, int *h);

/* show/hide the title-bar "Publish to X" button (macos_style.m) */
extern void kern_titlebar_set_x_connected(int connected);

/* marketing version string (macos_style.m) — used only for the dev-build label */
extern const char *kern_app_version(void);

/* Swift calls this (possibly off the main thread) to report a publish result.
   We only stash a string + timestamp; the 250ms event-loop tick repaints it,
   so no synthetic SDL event is needed. */
void kern_x_set_status(const char *msg) { nav_status_set(&g_vs, msg); }

/* ---- view-toggle bridges (called from C-x chords and the title-bar menu) ----
   Both run on the main thread (the menu action fires during the SDL event pump),
   the same thread as the render loop, so touching g_vs is safe. */
void kern_toggle_syntax(void) {
  g_vs.syntax_mask = g_vs.syntax_mask ? 0 : SYNTAX_MASK_ALL;
  nav_status_set(&g_vs, g_vs.syntax_mask ? "Syntax highlight on"
                                         : "Syntax highlight off");
}
int  kern_syntax_enabled(void) { return g_vs.syntax_mask != 0; }

void kern_toggle_style(void) {
  g_vs.style_mask = g_vs.style_mask ? 0 : STYLE_MASK_ALL;
  nav_status_set(&g_vs, g_vs.style_mask ? "Style check on" : "Style check off");
}
int  kern_style_enabled(void) { return g_vs.style_mask != 0; }

void kern_toggle_subs(void) {
  g_vs.sub_mask = g_vs.sub_mask ? 0 : SUB_MASK_ALL;
  nav_status_set(&g_vs, g_vs.sub_mask ? "Symbols on" : "Symbols off");
}
int  kern_subs_enabled(void) { return g_vs.sub_mask != 0; }

/* ---- per-type View toggles (one syntax class / style category each) ----
   Each flips its own bit of the relevant mask; the menu's per-item checkmark
   reads the matching *_enabled() query. The masters above flip everything. */

/* The five dim "function words" share one value in the palette, so the menu
   treats them as a single toggle. */
#define KERN_FUNCTION_WORDS (POS_BIT(POS_CONJUNCTION) | POS_BIT(POS_DETERMINER) | \
                             POS_BIT(POS_PREPOSITION) | POS_BIT(POS_PRONOUN)    | \
                             POS_BIT(POS_PARTICLE))

static void toggle_syntax_bits(unsigned int bits, const char *label) {
  if (g_vs.syntax_mask & bits) g_vs.syntax_mask &= ~bits;   /* any on → off */
  else                         g_vs.syntax_mask |=  bits;   /* else     → on */
  char msg[64];
  snprintf(msg, sizeof msg, "%s %s", label, (g_vs.syntax_mask & bits) ? "on" : "off");
  nav_status_set(&g_vs, msg);
}
void kern_toggle_verbs(void)          { toggle_syntax_bits(POS_BIT(POS_VERB), "Verbs"); }
int  kern_verbs_enabled(void)         { return (g_vs.syntax_mask & POS_BIT(POS_VERB)) != 0; }
void kern_toggle_nouns(void)          { toggle_syntax_bits(POS_BIT(POS_NOUN), "Nouns"); }
int  kern_nouns_enabled(void)         { return (g_vs.syntax_mask & POS_BIT(POS_NOUN)) != 0; }
void kern_toggle_adjectives(void)     { toggle_syntax_bits(POS_BIT(POS_ADJECTIVE), "Adjectives"); }
int  kern_adjectives_enabled(void)    { return (g_vs.syntax_mask & POS_BIT(POS_ADJECTIVE)) != 0; }
void kern_toggle_adverbs(void)        { toggle_syntax_bits(POS_BIT(POS_ADVERB), "Adverbs"); }
int  kern_adverbs_enabled(void)       { return (g_vs.syntax_mask & POS_BIT(POS_ADVERB)) != 0; }
void kern_toggle_function_words(void) { toggle_syntax_bits(KERN_FUNCTION_WORDS, "Function words"); }
int  kern_function_words_enabled(void){ return (g_vs.syntax_mask & KERN_FUNCTION_WORDS) != 0; }

static void toggle_style_bit(unsigned int bit, const char *label) {
  g_vs.style_mask ^= bit;
  char msg[64];
  snprintf(msg, sizeof msg, "%s %s", label, (g_vs.style_mask & bit) ? "on" : "off");
  nav_status_set(&g_vs, msg);
}
void kern_toggle_fillers(void)        { toggle_style_bit(STYLE_BIT(STYLE_FILLER), "Fillers"); }
int  kern_fillers_enabled(void)       { return (g_vs.style_mask & STYLE_BIT(STYLE_FILLER)) != 0; }
void kern_toggle_cliches(void)        { toggle_style_bit(STYLE_BIT(STYLE_CLICHE), "Cliches"); }
int  kern_cliches_enabled(void)       { return (g_vs.style_mask & STYLE_BIT(STYLE_CLICHE)) != 0; }
void kern_toggle_redundancies(void)   { toggle_style_bit(STYLE_BIT(STYLE_REDUNDANCY), "Redundancies"); }
int  kern_redundancies_enabled(void)  { return (g_vs.style_mask & STYLE_BIT(STYLE_REDUNDANCY)) != 0; }


/* ---- minibuffer filename completion ---- */

static int  minibuf_completing = 0;   /* show ghost completion for this prompt */
static char minibuf_suggest[1024];    /* full completed name; begins with minibuf_text */

/* Recompute the ghost suggestion from the current minibuffer text. */
static void minibuf_refresh_completion(void) {
  minibuf_suggest[0] = '\0';
  if (minibuf_completing && g_vs.minibuf_len > 0) {
    char full[1024];
    if (buf_complete_filename(g_vs.minibuf_text, full, sizeof(full)))
      snprintf(minibuf_suggest, sizeof(minibuf_suggest), "%s", full);
  }
}

/* ---- file operations (glue — uses status_set and r_set_title) ---- */

/* recent-files MRU (for C-x b buffer switching) lives in recent.c */

/* per-file cursor memory (defined below, near session persistence) */
static void filepos_remember_current(void);
static void filepos_restore_current(void);

/* ---- "Opened after" history --------------------------------------------------
   For each note, remember the files that were open right before it was opened
   (via C-x C-f, C-x C-b, a wikilink follow, or the daily-note jump). Surfaced in
   the read-only Context section ("Opened after") and persisted to
   ".kern_opened_after" alongside the session. The predecessor is kept as a
   basename ("Foo.md") so it renders as a [[wikilink]] like the other lists.
   Compiled into both builds (the table + record logic); only the file I/O is
   app-guarded, matching the filepos machinery. */
#define OPENED_AFTER_MAX 256   /* files tracked */
#define OA_PRED_MAX 24         /* predecessors kept per file (matches CTX_MAX) */
typedef struct {
  char path[1024];               /* the opened file (full sandboxed path) */
  char preds[OA_PRED_MAX][256];  /* basenames of files open just before it, MRU */
  int  npred;
} OpenedAfter;
static OpenedAfter g_opened_after[OPENED_AFTER_MAX];
static int         g_opened_after_count;

static OpenedAfter *opened_after_find(const char *path, int create) {
  for (int i = 0; i < g_opened_after_count; i++)
    if (strcmp(g_opened_after[i].path, path) == 0) return &g_opened_after[i];
  if (!create) return NULL;
  int i = g_opened_after_count < OPENED_AFTER_MAX ? g_opened_after_count++
                                                  : OPENED_AFTER_MAX - 1;
  OpenedAfter *e = &g_opened_after[i];
  snprintf(e->path, sizeof(e->path), "%s", path);
  e->npred = 0;
  return e;
}

/* Add `prevbase` (a basename) to `newpath`'s predecessor list, most-recent
   first and deduped (an existing entry is lifted to the front). */
static void opened_after_add(const char *newpath, const char *prevbase) {
  if (!newpath || !newpath[0] || !prevbase || !prevbase[0]) return;
  OpenedAfter *e = opened_after_find(newpath, 1);
  if (!e) return;
  int at = -1;
  for (int i = 0; i < e->npred; i++)
    if (strcmp(e->preds[i], prevbase) == 0) { at = i; break; }
  if (at == 0) return;                       /* already newest */
  if (at > 0) {                              /* move an existing entry up front */
    char tmp[256]; snprintf(tmp, sizeof(tmp), "%s", e->preds[at]);
    memmove(&e->preds[1], &e->preds[0], sizeof(e->preds[0]) * at);
    snprintf(e->preds[0], sizeof(e->preds[0]), "%s", tmp);
    return;
  }
  int keep = e->npred < OA_PRED_MAX ? e->npred : OA_PRED_MAX - 1;   /* drop oldest if full */
  memmove(&e->preds[1], &e->preds[0], sizeof(e->preds[0]) * keep);
  snprintf(e->preds[0], sizeof(e->preds[0]), "%s", prevbase);
  if (e->npred < OA_PRED_MAX) e->npred++;
}

/* Record the transition from `prevpath` to `newpath` (both full paths): the file
   we're leaving becomes a predecessor of the one we're opening. No-op on a
   self-transition or an empty previous path (e.g. the launch open). */
static void opened_after_record(const char *newpath, const char *prevpath) {
  if (!prevpath || !prevpath[0]) return;
  if (newpath && strcmp(newpath, prevpath) == 0) return;
  opened_after_add(newpath, path_base(prevpath));
}

static void opened_after_save(void) {
#ifndef KERN_HEADLESS_TEST
  if (!buf_get_documents_dir()[0]) return;
  char path[1024];
  buf_resolve_path(".kern_opened_after", path, sizeof(path));
  FILE *f = fopen(path, "wb");
  if (!f) return;
  /* one "<predecessor-basename>\t<full-path>" line per pair; path last (it may
     contain spaces), oldest-first so a reload via opened_after_add rebuilds the
     same MRU order (newest ends up at the front). */
  for (int i = 0; i < g_opened_after_count; i++)
    for (int j = g_opened_after[i].npred - 1; j >= 0; j--)
      fprintf(f, "%s\t%s\n", g_opened_after[i].preds[j], g_opened_after[i].path);
  fclose(f);
#endif
}

/* ---- auto-generated, read-only "Context" section ----------------------------
   Appended below the document: related notes (same creation day) and backlinks
   (other notes that [[link]] to this one). The lines are real buffer lines but
   guarded read-only (EditorState.readonly_from) and excluded from save, so the
   caret can move in (and follow wikilinks) yet nothing there is editable or
   persisted. Regenerated on every file open. App-only: it scans the documents
   dir + file creation times, so it's a no-op under the headless test build (the
   read-only/save machinery is unit-tested directly via readonly_from). */
#ifndef KERN_HEADLESS_TEST
#define CTX_MAX 24   /* cap each list so a huge vault can't bloat the section */

/* YYYY-MM-DD of a file's creation (birth) time; 0 on stat failure. */
static int ctx_birth_day(const char *path, char *out, int outsz) {
  struct stat st;
  if (stat(path, &st) != 0) return 0;
  time_t t = (time_t)st.st_birthtimespec.tv_sec;
  struct tm tm;
  localtime_r(&t, &tm);
  return (int)strftime(out, (size_t)outsz, "%Y-%m-%d", &tm) > 0;
}

/* Does the file at `path` contain a [[wikilink]] to `base` ("Foo.md") or its
   extension-less form ("Foo")? Reads up to 256 KB — enough for any note. */
static int ctx_links_to(const char *path, const char *base, const char *base_noext) {
  FILE *f = fopen(path, "rb");
  if (!f) return 0;
  static char buf[256 * 1024];   /* main-thread only (load time); keep off the stack */
  size_t n = fread(buf, 1, sizeof(buf) - 1, f);
  fclose(f);
  buf[n] = '\0';
  char needle[300];
  snprintf(needle, sizeof(needle), "[[%s]]", base);
  if (strstr(buf, needle)) return 1;
  snprintf(needle, sizeof(needle), "[[%s]]", base_noext);
  return strstr(buf, needle) != NULL;
}

/* Fill `out` (up to `max` basenames) with the files opened right before `path`,
   most-recent first. No on-disk existence filter: a file opened via C-x C-f may
   not be saved to disk yet (a blank new note stays in memory until autosave), but
   it was genuinely opened, so it belongs in the list — and a [[wikilink]] to a
   note that doesn't exist just creates it on follow, like everywhere else. */
static int opened_after_list(const char *path, char out[][256], int max) {
  OpenedAfter *e = opened_after_find(path, 0);
  if (!e) return 0;
  int n = 0;
  for (int i = 0; i < e->npred && n < max; i++)
    snprintf(out[n++], 256, "%s", e->preds[i]);
  return n;
}

/* Append one read-only line at the end of the buffer (readonly_from is 0 while
   building, so the boundary bookkeeping in buf_insert_line_at stays inert). */
static void ctx_append(const char *s) {
  buf_insert_line_at(&g_ed, g_ed.line_count, s, (int)strlen(s));
}

/* Strip any existing Context section (lines >= readonly_from). */
static void context_strip(void) {
  if (g_ed.readonly_from <= 0) return;
  int from = g_ed.readonly_from;
  g_ed.readonly_from = 0;   /* disable boundary bookkeeping during the delete */
  while (g_ed.line_count > from && g_ed.line_count > 1)
    buf_delete_line_at(&g_ed, g_ed.line_count - 1);
  if (g_ed.cursor_line >= g_ed.line_count) g_ed.cursor_line = g_ed.line_count - 1;
}

static void context_refresh(void) {
  context_strip();
  const char *docs = buf_get_documents_dir();
  if (!docs[0] || !g_ed.filepath[0]) return;

  const char *self = path_base(g_ed.filepath);           /* "Foo.md" */
  char self_noext[256];
  snprintf(self_noext, sizeof(self_noext), "%s", self);
  char *dot = strrchr(self_noext, '.');
  if (dot) *dot = '\0';

  char myday[16];
  int have_day = ctx_birth_day(g_ed.filepath, myday, sizeof(myday));

  char sameday[CTX_MAX][256]; int n_same = 0;
  char backlinks[CTX_MAX][256]; int n_back = 0;
  char opened[CTX_MAX][256];
  int n_open = opened_after_list(g_ed.filepath, opened, CTX_MAX);

  DIR *d = opendir(docs);
  if (d) {
    struct dirent *e;
    while ((e = readdir(d)) != NULL) {
      const char *name = e->d_name;
      if (name[0] == '.') continue;                      /* hidden / dotfiles */
      if (strlen(name) >= 256) continue;
      if (strcmp(name, self) == 0) continue;             /* skip this file */
      const char *ext = strrchr(name, '.');
      if (!ext || strcmp(ext, ".md") != 0) continue;     /* notes only */

      char full[1100];
      snprintf(full, sizeof(full), "%s/%s", docs, name);

      if (have_day && n_same < CTX_MAX) {
        char day[16];
        if (ctx_birth_day(full, day, sizeof(day)) && strcmp(day, myday) == 0)
          snprintf(sameday[n_same++], 256, "%s", name);
      }
      if (n_back < CTX_MAX && ctx_links_to(full, self, self_noext))
        snprintf(backlinks[n_back++], 256, "%s", name);
    }
    closedir(d);
  }

  if (n_same == 0 && n_back == 0 && n_open == 0) return;   /* nothing related → no section */

  int start = g_ed.line_count;
  /* The section opens with the divider line itself (readonly_from — do_render
     draws a dotted rule + centered "Context" label across it; the ~1.5 lines of
     natural gap above it is the spacing), then a blank line for spacing below.
     start is the save cut-point, and the first read-only line, so the caret only
     turns amber once it's actually on the divider or below — not on a blank line
     that still looks like editable page. */
  ctx_append("");   /* divider line (readonly_from) */
  ctx_append("");   /* spacing below the divider */
  int first = 1;
  if (n_back > 0) {
    ctx_append("Backlinks");
    for (int i = 0; i < n_back; i++) {
      char line[300];
      snprintf(line, sizeof(line), "- [[%s]]", backlinks[i]);
      ctx_append(line);
    }
    first = 0;
  }
  if (n_same > 0) {
    if (!first) ctx_append("");
    ctx_append("Created the same day");
    for (int i = 0; i < n_same; i++) {
      char line[300];
      snprintf(line, sizeof(line), "- [[%s]]", sameday[i]);
      ctx_append(line);
    }
    first = 0;
  }
  if (n_open > 0) {
    if (!first) ctx_append("");
    ctx_append("Opened after");
    for (int i = 0; i < n_open; i++) {
      char line[300];
      snprintf(line, sizeof(line), "- [[%s]]", opened[i]);
      ctx_append(line);
    }
  }
  g_ed.readonly_from = start;   /* everything from the spacing line down is static */
}
#else
static void context_refresh(void) {}
#endif

static void open_or_create_file(const char *path) {
  filepos_remember_current();   /* stash where we were in the file we're leaving */
  char prev[1024];              /* the file we're leaving, for the "Opened after" list */
  snprintf(prev, sizeof(prev), "%s", g_ed.filepath);
  /* resolve the typed name to a path inside the sandbox documents dir */
  buf_resolve_path(path, g_ed.filepath, sizeof(g_ed.filepath));
  g_ed.filename = path_base(g_ed.filepath);
  opened_after_record(g_ed.filepath, prev);   /* before context_refresh, so it lists prev */

  buf_free_all_lines(&g_ed);
  int existed = (buf_load_file(&g_ed, g_ed.filepath) == 0);
  if (!existed) buf_init_empty(&g_ed);

  g_ed.cursor_line = 0;
  g_ed.cursor_col = 0;
  g_ed.cursor_target_col = 0;
  g_vs.scroll_y = g_vs.typewriter_mode ? 0.0f : -nav_top_margin(&g_vs);  /* rest at the top page margin */
  context_refresh();                      /* append the read-only Context section */
  buf_invalidate_all_wraps(&g_ed);
  filepos_restore_current();              /* drop back to where we last were here */
  nav_ensure_cursor_visible(&g_ed, &g_vs);
  r_set_title(g_ed.filename);
  char msg[256];
  snprintf(msg, sizeof(msg), existed ? "Opened %s" : "New file %s", g_ed.filename);
  nav_status_set(&g_vs, msg);
  recent_push(g_ed.filepath);
}

static void save_to_path(const char *path) {
  buf_resolve_path(path, g_ed.filepath, sizeof(g_ed.filepath));
  g_ed.filename = path_base(g_ed.filepath);
  char msg[256];
  if (buf_save(&g_ed, g_ed.filepath) == 0) {
    snprintf(msg, sizeof(msg), "Wrote %s", g_ed.filename);
  } else {
    snprintf(msg, sizeof(msg), "FAILED to write %s", g_ed.filename);
  }
  nav_status_set(&g_vs, msg);
  r_set_title(g_ed.filename);
}

/* ---- wikilink navigation (Cmd-Enter follow, Cmd-Shift-←/→ back/forward) ---- */

#define NAV_MAX 64
typedef struct { char path[1024]; int line, col; } NavEntry;
static NavEntry nav_back[NAV_MAX]; static int nav_back_count;
static NavEntry nav_fwd[NAV_MAX];  static int nav_fwd_count;

static void nav_stack_push(NavEntry *stack, int *count,
                           const char *path, int line, int col) {
  if (!path || !path[0]) return;   /* nothing to remember (unsaved scratch) */
  if (*count == NAV_MAX) {
    memmove(&stack[0], &stack[1], sizeof(NavEntry) * (NAV_MAX - 1));
    (*count)--;
  }
  snprintf(stack[*count].path, sizeof(stack[0].path), "%s", path);
  stack[*count].line = line;
  stack[*count].col = col;
  (*count)++;
}

static void nav_goto(const NavEntry *e) {
  open_or_create_file(e->path);
  g_ed.cursor_line = e->line; g_ed.cursor_col = e->col;
  nav_cursor_clamp(&g_ed);
  g_ed.cursor_target_col = g_ed.cursor_col;
  nav_ensure_cursor_visible(&g_ed, &g_vs);
}

/* If the cursor is within a [[wikilink]] on the current line, copy its target
   (text between the brackets) to `out` and return 1. */
static int wikilink_at_cursor(char *out, int outsz) {
  Line *l = &g_ed.lines[g_ed.cursor_line];
  int c = g_ed.cursor_col;
  for (int i = 0; i + 1 < l->len; i++) {
    if (l->text[i] == '[' && l->text[i+1] == '[') {
      int j = i + 2;
      while (j + 1 < l->len && !(l->text[j] == ']' && l->text[j+1] == ']')) {
        if (l->text[j] == '[' && l->text[j+1] == '[') break;   /* nested open */
        j++;
      }
      if (j + 1 < l->len && l->text[j] == ']' && l->text[j+1] == ']') {
        if (c >= i && c <= j + 2) {                 /* cursor within [[ … ]] */
          int len = j - (i + 2);
          if (len <= 0 || len >= outsz) return 0;
          memcpy(out, l->text + i + 2, len);
          out[len] = '\0';
          return 1;
        }
        i = j + 1;                                  /* skip past this span */
      }
    }
  }
  return 0;
}

/* Flush the open buffer to disk if it has unsaved changes and a real path —
   the guard run before leaving the current file (wikilink jump, buffer switch,
   daily-note, quit). */
static void save_if_dirty(void) {
  if (g_ed.dirty && g_ed.filepath[0]) buf_save(&g_ed, g_ed.filepath);
}

static void cmd_follow_wikilink(void) {   /* Cmd-Enter */
  char target[1024];
  if (!wikilink_at_cursor(target, sizeof(target))) {
    nav_status_set(&g_vs, "No wikilink at cursor");
    return;
  }
  save_if_dirty();
  nav_stack_push(nav_back, &nav_back_count, g_ed.filepath, g_ed.cursor_line, g_ed.cursor_col);
  nav_fwd_count = 0;   /* a new jump starts a fresh branch — drop forward history */
  open_or_create_file(target);
}

static void cmd_nav_back(void) {          /* Cmd-Shift-Left */
  if (nav_back_count == 0) { nav_status_set(&g_vs, "No previous note"); return; }
  save_if_dirty();
  nav_stack_push(nav_fwd, &nav_fwd_count, g_ed.filepath, g_ed.cursor_line, g_ed.cursor_col);
  NavEntry e = nav_back[--nav_back_count];
  nav_goto(&e);
}

static void cmd_nav_forward(void) {       /* Cmd-Shift-Right */
  if (nav_fwd_count == 0) { nav_status_set(&g_vs, "No next note"); return; }
  save_if_dirty();
  nav_stack_push(nav_back, &nav_back_count, g_ed.filepath, g_ed.cursor_line, g_ed.cursor_col);
  NavEntry e = nav_fwd[--nav_fwd_count];
  nav_goto(&e);
}

/* Cmd-Shift-N: spin the marked region out into its own note. The note is named
   after the first line of the selection (alphanumerics only) + ".md", written
   to the documents folder, and the selection is replaced in place with a
   [[wikilink]] to the new note. */
static void cmd_extract_region_to_note(void) {
  if (!g_ed.mark_active) { nav_status_set(&g_vs, "Mark a region first (C-Space)"); return; }

  int rlen;
  char *region = ed_region_dup(&g_ed, &rlen);
  if (!region || rlen == 0) { free(region); nav_status_set(&g_vs, "Region is empty"); return; }

  /* title := first line of the selection (letters, digits and spaces kept) */
  char base[256];
  buf_sanitize_note_title(region, rlen, base, sizeof(base));
  if (base[0] == '\0') {
    free(region);
    nav_status_set(&g_vs, "First line has no letters or digits to name the note");
    return;
  }

  /* resolve to a filename that won't clobber an existing note */
  char fname[300], path[1024];
  int taken = 1;
  for (int n = 1; n <= 1000; n++) {
    if (n == 1) snprintf(fname, sizeof(fname), "%s.md", base);
    else        snprintf(fname, sizeof(fname), "%s-%d.md", base, n);
    buf_resolve_path(fname, path, sizeof(path));
    FILE *probe = fopen(path, "rb");
    if (!probe) { taken = 0; break; }
    fclose(probe);
  }
  if (taken) { free(region); nav_status_set(&g_vs, "Too many notes with that title"); return; }

  if (buf_save_text(path, region, rlen) != 0) {
    free(region);
    nav_status_set(&g_vs, "Couldn't write the new note");
    return;
  }
  free(region);

  /* replace the selection with a link to the new note, as one undo step */
  char link[320];
  snprintf(link, sizeof(link), "[[%s]]", fname);
  ed_replace_region(&g_ed, link);
  nav_ensure_cursor_visible(&g_ed, &g_vs);

  char msg[256];
  snprintf(msg, sizeof(msg), "Created %s", fname);
  nav_status_set(&g_vs, msg);
}

/* ---- margin notes (typewriter) / footnotes (normal) ---------------------- *
 * A margin note is stored as a standard markdown footnote: the marked region is
 * wrapped in ==…== with a [^id] marker before the closing ==, and a "[^id]: text"
 * definition is appended at the document bottom. In typewriter mode the
 * definitions render in the right margin aligned to their reference's row; in
 * normal mode they're just footnote lines at the bottom. Authored keyboard-first
 * via Cmd-Shift-M on a marked region. */
/* Margin-note text scale relative to the body font (perceived size tracks ink
   AREA, so ratios read smaller than they measure — 0.5 felt tiny). Applied as a
   quad-level draw scale (r_set_font_scale) and to the note line height. */
#define MN_SCALE 0.8f
static int mn_line_height(void) { return (int)(nav_line_height() * MN_SCALE + 0.5f); }

static int  mn_active;          /* margin-note input modal is open */
static char mn_text[1024];      /* the note being typed */
static int  mn_len;
static int  mn_ref_line;        /* logical line of the mark start (where the note aligns) */
static char mn_id[24];          /* this note's footnote id */
static unsigned int mn_seq;     /* disambiguates ids generated in the same ms */

/* Short, readable-ish footnote id from the clock + a sequence (base36). */
static void gen_footnote_id(char *out, int outsz) {
  unsigned int t = kern_now_ms() + (mn_seq++ * 2654435761u);
  static const char *d36 = "0123456789abcdefghijklmnopqrstuvwxyz";
  char tmp[16]; int n = 0;
  do { tmp[n++] = d36[t % 36]; t /= 36; } while (t && n < 8);
  int i = 0;
  if (outsz > 1) out[i++] = 'n';          /* a letter lead so it reads as a name */
  while (n > 0 && i < outsz - 1) out[i++] = tmp[--n];
  out[i] = '\0';
}

static void margin_note_commit(void) {
  if (mn_len == 0) { mn_active = 0; nav_status_set(&g_vs, "Empty note — cancelled"); return; }

  /* insert just the footnote marker at the write-head (no selection, no ==) */
  g_ed.mark_active = 0;
  char marker[40];
  snprintf(marker, sizeof(marker), "[^%s]", mn_id);
  ed_insert_char(&g_ed, marker);

  /* append the footnote definition at the document bottom — but above the
     read-only Context section, so it stays editable and is saved to disk */
  char def[1100];
  snprintf(def, sizeof(def), "[^%s]: %s", mn_id, mn_text);
  int at = g_ed.readonly_from > 0 ? g_ed.readonly_from : g_ed.line_count;
  buf_insert_line_at(&g_ed, at, def, (int)strlen(def));
  buf_invalidate_all_wraps(&g_ed);

  mn_active = 0; mn_text[0] = '\0'; mn_len = 0;
  nav_ensure_cursor_visible(&g_ed, &g_vs);
  nav_status_set(&g_vs, "Margin note saved");
}

/* Cmd-Shift-M: write a margin note at the caret (typewriter mode, no selection
   needed). The footnote marker is inserted at the write-head on commit. */
static int tv_margin_pad(void);   /* defined with the margin renderer */
static void cmd_margin_note(void) {
  /* typewriter mode always has room (the page can slide); normal mode needs the
     window margin to be wide enough to hold the note strip. */
  if (!g_vs.typewriter_mode && nav_page_margin() < tv_margin_pad()) {
    nav_status_set(&g_vs, "Widen the window (or use typewriter mode) for margin notes");
    return;
  }
  gen_footnote_id(mn_id, sizeof(mn_id));
  mn_ref_line = g_ed.cursor_line;
  mn_text[0] = '\0'; mn_len = 0;
  mn_active = 1;
  SDL_StartTextInput();
  nav_status_set(&g_vs, "Margin note — type, Enter to save, Esc to cancel");
}

/* Modal key handling while the margin-note input is open. Consumes all keys. */
static int handle_marginnote_key(int sym) {
  if (sym == SDLK_ESCAPE) {
    mn_active = 0; mn_text[0] = '\0'; mn_len = 0;
    nav_status_set(&g_vs, "Margin note cancelled");
    return 1;
  }
  if (sym == SDLK_RETURN) { margin_note_commit(); return 1; }
  if (sym == SDLK_BACKSPACE) {
    if (mn_len > 0) {
      int i = mn_len - 1;
      while (i > 0 && ((unsigned char)mn_text[i] & 0xC0) == 0x80) i--;   /* whole UTF-8 char */
      mn_text[i] = '\0'; mn_len = i;
    }
    return 1;
  }
  return 1;   /* swallow everything else while modal */
}

/* Join the whole buffer into one malloc'd, NUL-terminated string, lines
   separated by '\n'. Caller frees; *len_out gets the byte length. */
static char *buffer_dup_all(EditorState *ed, int *len_out) {
  int n = buf_content_line_count(ed);   /* the Context section isn't part of the note */
  int total = 1;  /* room for the trailing NUL even on an empty buffer */
  for (int i = 0; i < n; i++) total += ed->lines[i].len + 1;
  char *out = malloc(total);
  if (!out) { if (len_out) *len_out = 0; return NULL; }
  int p = 0;
  for (int i = 0; i < n; i++) {
    memcpy(out + p, ed->lines[i].text, ed->lines[i].len);
    p += ed->lines[i].len;
    if (i + 1 < n) out[p++] = '\n';
  }
  out[p] = '\0';
  if (len_out) *len_out = p;
  return out;
}

/* ---- X publish confirmation overlay ----------------------------------------
   Clicking the title-bar "Publish" button doesn't post immediately: it opens a
   confirmation overlay (drawn in do_render, part of the main loop — not a macOS
   sheet) that previews the note as it would look on X. Confirm posts; the async
   result lands in kern_x_publish_done() and is consumed on the next tick. */
enum { PUB_NONE = 0, PUB_CONFIRM = 1, PUB_SENDING = 2 };
static int  pub_state;              /* PUB_* */
static char pub_text[8192];         /* the snapshotted note/region being previewed */

/* Async result handed back from the Swift publisher (possibly off the main
   thread). We only stash it here; editor_tick() (main thread) applies it — that
   keeps the clipboard write + status update off any background thread. */
static int  pub_result;             /* 0 none, 1 ok, 2 fail */
static char pub_result_info[1024];  /* ok: tweet URL; fail: error message */

/* Publish the current note to X (title-bar button). If a region is marked, only
   that is previewed/posted; otherwise the whole note. Opens the confirmation
   overlay — the actual POST happens on confirm (pub_confirm). */
static void cmd_publish_to_x(void) {
  if (!kern_x_is_connected()) {
    nav_status_set(&g_vs, "Connect your X account first (Settings \xE2\x80\xBA X)");
    return;
  }
  int len = 0;
  char *text = g_ed.mark_active ? ed_region_dup(&g_ed, &len)
                                : buffer_dup_all(&g_ed, &len);
  if (!text || len == 0) { free(text); nav_status_set(&g_vs, "Nothing to publish"); return; }

  snprintf(pub_text, sizeof(pub_text), "%s", text);
  free(text);
  pub_state = PUB_CONFIRM;
  pub_result = 0;
  SDL_StartTextInput();   /* not really needed, but keeps input state sane */
}

/* Confirm the overlay: hand the snapshotted text to the async Swift publisher
   and switch to the "sending" state (the result arrives via
   kern_x_publish_done). */
static void pub_confirm(void) {
  if (pub_state != PUB_CONFIRM) return;
  pub_state = PUB_SENDING;
  nav_status_set(&g_vs, "Publishing to X\xE2\x80\xA6");
  kern_x_publish(pub_text);
}

static void pub_cancel(void) {
  pub_state = PUB_NONE;
  nav_status_set(&g_vs, "Publish cancelled");
}

/* Modal key handling while the overlay is open; consumes every key so nothing
   reaches the buffer. Enter confirms, Esc cancels. */
static int handle_publish_key(int sym) {
  if (sym == SDLK_ESCAPE) { pub_cancel(); return 1; }
  if (sym == SDLK_RETURN || sym == SDLK_KP_ENTER) { pub_confirm(); return 1; }
  return 1;   /* swallow everything else while modal */
}

/* Called from the Swift publisher when the POST completes (ok=1 with the tweet
   URL in `info`, or ok=0 with an error message). Stashed here; applied on the
   next editor_tick() so the clipboard/status touch happens on the main thread. */
void kern_x_publish_done(int ok, const char *info) {
  pub_result = ok ? 1 : 2;
  snprintf(pub_result_info, sizeof(pub_result_info), "%s", info ? info : "");
}

/* Main-thread application of a pending publish result (from editor_tick). */
static void pub_apply_result(void) {
  if (pub_result == 0) return;
  if (pub_result == 1) {                    /* success */
    if (pub_result_info[0]) {
      kern_clipboard_set(pub_result_info);
      nav_status_set(&g_vs, "Posted to X \xE2\x9C\x93 \xE2\x80\x94 link copied to clipboard");
    } else {
      nav_status_set(&g_vs, "Posted to X \xE2\x9C\x93");
    }
    pub_state = PUB_NONE;
  } else {                                  /* failure */
    nav_status_set(&g_vs, pub_result_info[0] ? pub_result_info : "X publish failed");
    if (pub_state == PUB_SENDING) pub_state = PUB_CONFIRM;   /* let the user retry */
  }
  pub_result = 0;
}

/* Called from the title-bar "Publish" button (macos_style.m, main thread). */
void kern_publish_to_x(void) { cmd_publish_to_x(); }

/* word wrapping, cursor navigation, editing, and markdown rendering
   now in navigation.c, editing.c, md_render.c (accessed via shims above) */

/* ---- undo is now in undo.c (operation-based) ---- */

/* The heading-marker-hang test now lives in navigation.c
   (nav_heading_markers_hang) so click/movement geometry shares the same render
   decision; do_render and the selection highlight call it below. */

/* ---- frame ---- */

/* mouse state, tracked from SDL events; only the scrollbar consumes it.
   g_mouse_pressed is the press edge for this frame. */
static int g_mouse_x, g_mouse_y, g_mouse_down, g_mouse_pressed;

/* Set by process_frame while scroll_y is gliding toward scroll_target_y (g_scroll_animating)
   or a focus crossfade is in progress (g_dim_animating); either makes the event loop
   poll faster (≈60fps) so the animation runs. */
static int g_scroll_animating;
static int g_dim_animating;
/* Set while a POS word-color fade-in is still running (see pos_render.h); also
   makes the loop poll at ~60fps so the fade animates. */
static int g_pos_animating;

/* Hold the word being typed at the base color and fade a just-finished word up
   to its POS color. Runs each frame: an edit (buf_edit_seq changed) establishes
   the caret's word as "in progress"; once the caret leaves that word — a space,
   punctuation, an arrow, a click — the word starts its base→POS-color fade.
   Pure caret/edit bookkeeping, so navigating onto already-colored text (no edit)
   never disturbs its color. */
static const Line *g_wip_line;       /* word being typed (pointer-compared only) */
static int g_wip_lo, g_wip_hi;
static unsigned long g_pos_prev_seq;
static void pos_anim_track(void) {
  unsigned int now = kern_now_ms();
  unsigned long seq = buf_edit_seq();
  int edited = (seq != g_pos_prev_seq);
  g_pos_prev_seq = seq;

  Line *cur = &g_ed.lines[g_ed.cursor_line];
  int col = g_ed.cursor_col;

  if (edited) {
    int lo, hi;
    if (pos_word_bounds(cur, col, &lo, &hi)) {
      /* a different word than the one in progress → finish the old one first */
      if (g_wip_line && (g_wip_line != cur || hi <= g_wip_lo || lo >= g_wip_hi))
        pos_fade_begin(g_wip_line, g_wip_lo, g_wip_hi, now);
      g_wip_line = cur; g_wip_lo = lo; g_wip_hi = hi;
    }
    /* an edit that left the caret on no word (a space/punctuation) is handled by
       the completion check below, which sees the caret outside the old word. */
  }

  /* finished: caret no longer within the in-progress word → start its fade */
  if (g_wip_line && !(g_wip_line == cur && col >= g_wip_lo && col <= g_wip_hi)) {
    pos_fade_begin(g_wip_line, g_wip_lo, g_wip_hi, now);
    g_wip_line = NULL;
  }

  pos_set_wip(g_wip_line, g_wip_lo, g_wip_hi);
  pos_set_now(now);
  g_pos_animating = pos_fades_active(now);
}

/* Lay out and immediately draw the frame's chrome: window background, content
   clip, selection/search highlights and the scrollbar. The markdown text is
   drawn afterward in do_render. */
/* Typewriter carriage: columns the caret currently floats past end-of-text — only
   while it's still exactly where the last move that set it left it (any other
   motion invalidates the float). Drives the caret render offset and type-time pad. */
static void tv_note_col(int side, int *x0, int *w);   /* defined with the margin renderer */

static int tv_virtual_cols(void) {
  if (!g_vs.typewriter_mode) return 0;
  if (g_vs.goal_line == g_ed.cursor_line && g_vs.goal_col == g_ed.cursor_col)
    return g_vs.virtual_col;
  return 0;
}

/* Pixel width of the current float (virtual columns × the mono space advance). */
static int tv_virtual_px(void) {
  int vc = tv_virtual_cols();
  if (vc <= 0) return 0;
  int saved = r_get_font_style();
  r_set_font_style(FONT_MONO);
  int w = r_get_text_width(" ", 1) * vc;
  r_set_font_style(saved);
  return w;
}

/* Set/extend the carriage float to `vcols` past EOL on the caret's line, and feed
   the goal column so a following vertical move continues from the float. */
static void tv_set_float(int vcols) {
  if (vcols < 0) vcols = 0;
  int eol_x = nav_cursor_x(&g_ed, g_ed.cursor_line, g_ed.cursor_col);
  int saved = r_get_font_style();
  r_set_font_style(FONT_MONO);
  int cw = r_get_text_width(" ", 1);
  r_set_font_style(saved);
  int right_edge = nav_page_margin() + nav_page_w();
  int maxv = cw > 0 ? (right_edge - eol_x) / cw : 0;     /* don't float past the margin */
  if (vcols > maxv) vcols = maxv;
  g_vs.virtual_col = vcols;
  g_vs.goal_x = eol_x + vcols * cw;
  g_vs.goal_line = g_ed.cursor_line;
  g_vs.goal_col = g_ed.cursor_col;
}

/* Typewriter horizontal arrow: stays on one line, moving the page (never jumps
   lines). Right past EOL floats into the blank page; left retracts the float,
   then walks back through the text; at BOL it simply stops. */
static void tv_horizontal_move(int dir) {
  if (g_vs.typewriter_mode) {
    Line *l = &g_ed.lines[g_ed.cursor_line];
    if (dir > 0) {
      if (g_ed.cursor_col == l->len) { tv_set_float(tv_virtual_cols() + 1); return; }
    } else {
      int vc = tv_virtual_cols();
      if (vc > 0)              { tv_set_float(vc - 1); return; }
      if (g_ed.cursor_col == 0) return;   /* BOL: don't jump to the previous line */
    }
  }
  if (dir > 0) cmd_forward_char(&g_ed, &g_vs);
  else         cmd_backward_char(&g_ed, &g_vs);
}

static void process_frame(void) {
  g_vs.vis_row_count = 0;
  md_set_force_mono(g_vs.typewriter_mode);   /* mono body font in typewriter mode (wrap + render) */
  pos_anim_track();   /* word-in-progress hold + base→POS-color fade bookkeeping */

  /* Measure with the body font — the same state the text is drawn in (see the
     r_set_font_* calls in do_render below). The previous frame ended with the
     status bar's FONT_MONO active; measuring wrap breaks and selection-highlight
     widths in mono while the text renders in the proportional body font skews
     them, most visibly on indented list lines. */
  r_set_font_size(g_vs.font_size);
  r_set_font_style(FONT_REGULAR);

  /* window background */
  r_draw_rect(rect(0, 0, nav_win_w(), nav_win_h()), color(50, 50, 50, 255));

  {
    int lh = nav_line_height();
    g_vs.content_y = TOP_PADDING;
    int status_bar_h = r_get_text_height() + 16;
    g_vs.content_h = nav_win_h() - TOP_PADDING - status_bar_h;
    if (!g_vs.typewriter_mode)
      g_vs.content_h -= (int)(2.0f * g_vs.font_size);   /* bottom page margin above the status bar */

    /* content area */
    int total_vis = nav_total_visual_lines(&g_ed);
    float max_scroll = (total_vis * lh) - g_vs.content_h;
    /* typewriter mode adds virtual whitespace below the last line so it can
       still pin at the golden line near EOF (matches iA Writer). */
    if (g_vs.typewriter_mode)
      max_scroll += (int)((1.0f - TYPEWRITER_FRACTION) * (g_vs.content_h - lh));
    if (max_scroll < 0) max_scroll = 0;
    /* normal mode: a bottom page margin below the last line, symmetric with the
       top margin (see nav_ensure_cursor_visible) — only when the page overflows. */
    if (!g_vs.typewriter_mode && max_scroll > 0)
      max_scroll += nav_top_margin(&g_vs);
    /* typewriter mode also adds virtual whitespace ABOVE the first line so it,
       too, can pin at the golden height (negative scroll). */
    float min_scroll = 0;
    if (g_vs.typewriter_mode)
      min_scroll = -(int)(TYPEWRITER_FRACTION * (g_vs.content_h - lh));
    else
      min_scroll = -nav_top_margin(&g_vs);          /* normal mode: a top page margin above line 0 */

    /* Clamp the ease target first so the glide settles exactly on a valid scroll
       position, then ease scroll_y toward it (typewriter mode only — every other
       scroll path writes scroll_y directly and snaps as before). */
    if (g_vs.scroll_target_y < min_scroll) g_vs.scroll_target_y = min_scroll;
    if (g_vs.scroll_target_y > max_scroll) g_vs.scroll_target_y = max_scroll;
    if (g_vs.typewriter_mode) {
      float d = g_vs.scroll_target_y - g_vs.scroll_y;
      if (d > -0.5f && d < 0.5f) g_vs.scroll_y = g_vs.scroll_target_y;  /* settle */
      else                       g_vs.scroll_y += d * SCROLL_EASE;
      g_scroll_animating = (g_vs.scroll_y != g_vs.scroll_target_y);
    } else {
      g_scroll_animating = 0;
    }

    /* Horizontal typewriter pin: hold the caret at the page-column center and
       glide the whole text pane under it. scroll_target_x is the caret's natural
       x (no shift) minus the pin x; the render/highlight passes draw every row at
       -scroll_x and click-to-cursor adds it back. Eases with the same glide as
       the vertical scroll (and shares g_scroll_animating). */
    if (g_vs.typewriter_mode) {
      /* Pin to the caret's full position including any carriage float past EOL, so
         the page slides under a fixed strike point. A vertical move onto a shorter
         line floats the caret so this x ≈ the goal column → the page holds its
         horizontal place; an arrow float past EOL grows it → the page glides. */
      int pin_x = nav_page_margin() + nav_page_w() / 2;   /* center of the text column */
      int target_x;
      if (mn_active) {
        /* writing a margin note: slide the page so the right note column lands in
           the (fixed, centered) hitzone — the page comes to the note, not vice versa. */
        int rx0, mw; tv_note_col(0, &rx0, &mw);
        target_x = rx0 + mw / 2;
      } else {
        target_x = nav_cursor_x(&g_ed, g_ed.cursor_line, g_ed.cursor_col) + tv_virtual_px();
      }
      g_vs.scroll_target_x = (float)(target_x - pin_x);
      float dx = g_vs.scroll_target_x - g_vs.scroll_x;
      if (dx > -0.5f && dx < 0.5f) g_vs.scroll_x = g_vs.scroll_target_x;
      else                         g_vs.scroll_x += dx * HSCROLL_EASE;
      if (g_vs.scroll_x != g_vs.scroll_target_x) g_scroll_animating = 1;
    } else {
      g_vs.scroll_x = g_vs.scroll_target_x = 0;
    }

    if (g_vs.scroll_y < min_scroll) g_vs.scroll_y = min_scroll;
    if (g_vs.scroll_y > max_scroll) g_vs.scroll_y = max_scroll;

    /* typewriter focus crossfade: when the caret changes line, start fading the
       old line down and the new line up; advance focus_t toward 1 (settled). */
    if (g_vs.typewriter_mode) {
      if (g_ed.cursor_line != g_vs.focus_cur_line) {
        g_vs.focus_prev_line = g_vs.focus_cur_line;
        g_vs.focus_cur_line  = g_ed.cursor_line;
        g_vs.focus_t = 0.0f;
      }
      if (g_vs.focus_t < 1.0f) {
        g_vs.focus_t += (1.0f - g_vs.focus_t) * FOCUS_EASE;
        if (g_vs.focus_t > 0.999f) g_vs.focus_t = 1.0f;
      }
      g_dim_animating = (g_vs.focus_t < 1.0f);
    } else {
      g_dim_animating = 0;
    }

    /* Negative scroll_y = virtual whitespace above line 1: keep the first row at
       index 0 and push it down by |scroll_y| instead of indexing off the top. */
    int first_vis; float y_offset;
    if (g_vs.scroll_y >= 0) {
      first_vis = (int)(g_vs.scroll_y / lh);
      y_offset  = g_vs.scroll_y - (first_vis * lh);
    } else {
      first_vis = 0;
      y_offset  = g_vs.scroll_y;   /* negative → py = content_y + |scroll_y| */
    }

    /* how many visual lines fit on screen (a negative y_offset pushes the first
       row down, so more rows are needed to fill the bottom) */
    int vis_on_screen = (g_vs.content_h - (int)y_offset) / lh + 4;

    /* clip to content area */
    r_set_clip_rect(rect(0, g_vs.content_y, nav_win_w(), g_vs.content_h));

    /* render visible visual lines */
    for (int vi = 0; vi < vis_on_screen; vi++) {
      int vis_idx = first_vis + vi;
      if (vis_idx >= total_vis) break;

      int py = g_vs.content_y + (vi * lh) - (int)y_offset;
      if (py + lh < g_vs.content_y || py > g_vs.content_y + g_vs.content_h) continue;

      /* map visual line to logical line + wrap row */
      int wrap_off;
      int ln = nav_visual_to_logical(&g_ed, vis_idx, &wrap_off);
      Line *l = &g_ed.lines[ln];
      nav_sub_reveal_for_line(&g_ed, ln);   /* highlights measure the same glyphs the text draws */

      int starts[256];
      int nrows = nav_get_wrap_breaks(l, starts, 256);
      int row_start = starts[wrap_off];
      int row_end = (wrap_off + 1 < nrows) ? starts[wrap_off + 1] : l->len;
      /* a heading's "### " prefix hangs in the LEFT margin (text flush at the
         page edge), so text/highlights flow from after it — unless the caret is
         at the line start, where the markers fold back inline for editing */
      int dstart = row_start;
      if (md_is_heading(l) && row_start == 0) {
        int hpre = md_heading_prefix_len(l);
        int reveal = (ln == g_ed.cursor_line && g_ed.cursor_col <= hpre);
        if (!reveal && nav_heading_markers_hang(l)) dstart = hpre;
      }
      /* match the text's indent so highlights align (incl. list hanging indent) */
      int row_indent = md_row_indent(l, row_start);

      /* draw mark region highlight */
      if (g_ed.mark_active && row_end > row_start) {
        int sl, sc, el, ec;
        buf_region_ordered(&g_ed, &sl, &sc, &el, &ec);
        if (ln >= sl && ln <= el) {
          int hl_start = (ln == sl) ? sc : 0;
          int hl_end   = (ln == el) ? ec : g_ed.lines[ln].len;
          /* clamp to this visual row */
          if (hl_start < row_end && hl_end > row_start) {
            int hs = hl_start < dstart ? dstart : hl_start;
            int he = hl_end > row_end ? row_end : hl_end;
            if (he > hs) {
              /* measure with the same per-span font metrics the text is drawn in */
              int x0 = nav_page_margin() + row_indent - (int)g_vs.scroll_x;
              int hx = md_col_x(l, dstart, row_end, x0, md_is_heading(l), hs);
              int hw = md_col_x(l, dstart, row_end, x0, md_is_heading(l), he) - hx;
              int font_h = r_get_text_height();
              r_draw_rect(rect(hx, py, hw, font_h),
                          color(60, 100, 160, 180));
            }
          }
        }
      }

      /* store row info for post-render markdown drawing */
      if (g_vs.vis_row_count < MAX_VIS_ROWS) {
        g_vs.vis_rows[g_vs.vis_row_count].ln = ln;
        g_vs.vis_rows[g_vs.vis_row_count].row_start = row_start;
        g_vs.vis_rows[g_vs.vis_row_count].row_end = row_end;
        g_vs.vis_rows[g_vs.vis_row_count].py = py;
        g_vs.vis_rows[g_vs.vis_row_count].heading = md_is_heading(l);
        g_vs.vis_row_count++;
      }

      /* draw search highlights on this visual row */
      if (g_vs.search_active && g_vs.search_len > 0 && row_end > row_start && (row_end - row_start) >= g_vs.search_len) {
        for (int sc = dstart; sc <= row_end - g_vs.search_len; sc++) {
          if (strncasecmp(l->text + sc, g_vs.search_buf, g_vs.search_len) == 0) {
            int x0 = nav_page_margin() + row_indent - (int)g_vs.scroll_x;
            int hx = md_col_x(l, dstart, row_end, x0, md_is_heading(l), sc);
            int hw = md_col_x(l, dstart, row_end, x0, md_is_heading(l), sc + g_vs.search_len) - hx;
            int font_h = r_get_text_height();
            /* current match gets brighter highlight */
            if (ln == g_vs.search_match_line && sc == g_vs.search_match_col) {
              r_draw_rect(rect(hx, py, hw, font_h),
                          color(200, 150, 0, 180));
            } else {
              r_draw_rect(rect(hx, py, hw, font_h),
                          color(120, 90, 0, 120));
            }
          }
        }
      }

      /* cursor drawing moved to post-render pass (uses g_cursor_x) */
    }

    r_set_clip_rect(rect(0, 0, nav_win_w(), nav_win_h()));   /* back to full window */

    /* scrollbar (hidden in typewriter mode — the page just glides) */
    if (max_scroll > 0 && !g_vs.typewriter_mode) {
      int sb_x = nav_win_w() - 8;
      int sb_w = 6;
      int sb_h = g_vs.content_h;
      float thumb_ratio = (float)g_vs.content_h / (total_vis * lh);
      int thumb_h = (int)(sb_h * thumb_ratio);
      if (thumb_h < 20) thumb_h = 20;
      float sb_ratio = g_vs.scroll_y / max_scroll;   /* may be <0 in typewriter mode */
      if (sb_ratio < 0) sb_ratio = 0;
      if (sb_ratio > 1) sb_ratio = 1;
      int thumb_y = g_vs.content_y + (int)(sb_ratio * (sb_h - thumb_h));

      int mx = g_mouse_x, my = g_mouse_y;
      int mouse_in_track = (mx >= sb_x - 4 && mx < sb_x + sb_w + 4 && my >= g_vs.content_y && my < g_vs.content_y + sb_h);

      if (g_vs.scrollbar_dragging) {
        if (g_mouse_down & MOUSE_LEFT) {
          float ratio = (my - g_vs.drag_offset - g_vs.content_y) / (float)(sb_h - thumb_h);
          if (ratio < 0) ratio = 0;
          if (ratio > 1) ratio = 1;
          g_vs.scroll_y = g_vs.scroll_target_y = ratio * max_scroll;
        } else {
          g_vs.scrollbar_dragging = 0;
        }
      } else if (mouse_in_track && (g_mouse_pressed & MOUSE_LEFT)) {
        if (my >= thumb_y && my < thumb_y + thumb_h) {
          g_vs.scrollbar_dragging = 1;
          g_vs.drag_offset = my - thumb_y;
        } else {
          float ratio = (my - g_vs.content_y - thumb_h / 2.0f) / (float)(sb_h - thumb_h);
          if (ratio < 0) ratio = 0;
          if (ratio > 1) ratio = 1;
          g_vs.scroll_y = g_vs.scroll_target_y = ratio * max_scroll;
          g_vs.scrollbar_dragging = 1;
          g_vs.drag_offset = thumb_h / 2.0f;
        }
      }

      Color thumb_color = g_vs.scrollbar_dragging ? color(140, 140, 140, 255) :
                             mouse_in_track     ? color(120, 120, 120, 255) :
                                                  color(80, 80, 80, 255);
      r_draw_rect(rect(sb_x, thumb_y, sb_w, thumb_h), thumb_color);
    }
  }

  g_mouse_pressed = 0;   /* the press edge is consumed once per frame */
}


/* ---- command functions ---- */

/* Most key bindings live in the de-globalized table in commands.c, dispatched
   via kern_dispatch_key: cursor movement (C-a C-e C-f C-b C-n C-p, M-f M-b),
   kill/yank/copy/case, scrolling/font, buffer-ends and mark. The few commands
   below need textview-local state (minibuffer, prefixes) so they're defined
   here and dispatched inline in the keydown handler. */

static void goto_line_cb(const char *text) {
  int n = atoi(text);
  if (n < 1) n = 1;
  if (n > g_ed.line_count) n = g_ed.line_count;
  g_ed.cursor_line = n - 1; g_ed.cursor_col = 0; g_ed.cursor_target_col = 0;
  nav_ensure_cursor_visible(&g_ed, &g_vs);
  char msg[64]; snprintf(msg, sizeof(msg), "Line %d", n); nav_status_set(&g_vs, msg);
}
/* Open the minibuffer with `prompt`, an empty input, and `cb` invoked on Enter.
   `completing` enables ghost filename completion (Tab to accept). */
static void minibuf_open(const char *prompt, int completing, void (*cb)(const char *)) {
  g_vs.minibuf_active = 1;
  minibuf_completing = completing;
  minibuf_suggest[0] = '\0';
  snprintf(g_vs.minibuf_prompt, sizeof(g_vs.minibuf_prompt), "%s", prompt);
  g_vs.minibuf_text[0] = '\0';
  g_vs.minibuf_len = 0;
  g_vs.minibuf_callback = cb;
}

static void cmd_goto_line(void) {       /* M-g */
  minibuf_open("Goto line: ", 0, goto_line_cb);
}

/* ---- modal key handlers ---- */

/* ---- C-x b buffer switching (recent files + Tab-cycling list) ---- */
static int  bufsw_active;
static int  bufsw_listing;     /* the candidate list is shown (after first Tab) */
static int  bufsw_sel;
static int  bufsw_count;
static char bufsw_default[1024];
static char bufsw_cands[RECENT_MAX][1024];

/* rebuild candidates from recents (excluding the current file), filtered by the
   typed text (case-insensitive on the filename) */
static void bufsw_filter(void) {
  bufsw_count = 0;
  size_t tl = strlen(g_vs.minibuf_text);
  for (int i = 1; i < recent_count() && bufsw_count < RECENT_MAX; i++) {
    const char *base = path_base(recent_get(i));
    if (tl == 0 || strncasecmp(base, g_vs.minibuf_text, tl) == 0)
      snprintf(bufsw_cands[bufsw_count++], sizeof(bufsw_cands[0]), "%s", recent_get(i));
  }
  if (bufsw_sel >= bufsw_count) bufsw_sel = bufsw_count > 0 ? bufsw_count - 1 : 0;
}

static void bufsw_switch(const char *path) {
  if (!path || !path[0]) { nav_status_set(&g_vs, "No other buffer"); return; }
  save_if_dirty();   /* save current */
  open_or_create_file(path);
}

static int handle_bufsw_key(int sym, int ctrl) {
  if (sym == SDLK_ESCAPE || (ctrl && sym == SDLK_g)) {
    bufsw_active = 0; g_vs.minibuf_active = 0; g_vs.minibuf_callback = NULL;
    nav_status_set(&g_vs, "Quit");
    return 1;
  }
  if (sym == SDLK_RETURN) {
    bufsw_active = 0; g_vs.minibuf_active = 0; g_vs.minibuf_callback = NULL;
    if (bufsw_listing && bufsw_count > 0) bufsw_switch(bufsw_cands[bufsw_sel]);
    else if (g_vs.minibuf_len == 0)            bufsw_switch(bufsw_default);
    else                                  bufsw_switch(g_vs.minibuf_text);
    return 1;
  }
  if (sym == SDLK_TAB) {              /* first Tab shows the list; then cycles */
    bufsw_filter();
    if (bufsw_count == 0) return 1;
    if (!bufsw_listing) { bufsw_listing = 1; bufsw_sel = 0; }
    else bufsw_sel = (bufsw_sel + 1) % bufsw_count;
    return 1;
  }
  if (sym == SDLK_BACKSPACE) {
    if (g_vs.minibuf_len > 0) { g_vs.minibuf_len--; g_vs.minibuf_text[g_vs.minibuf_len] = '\0'; }
    bufsw_filter();
    return 1;
  }
  return 1;   /* modal: swallow other keys (letters still arrive via text input) */
}

static void cmd_switch_buffer(void) {   /* C-x b */
  bufsw_default[0] = '\0';
  if (recent_count() >= 2) snprintf(bufsw_default, sizeof(bufsw_default), "%s", recent_get(1));
  g_vs.suppress_next_text = 1;   /* swallow the "b" text event that triggered this */
  char prompt[128];
  if (bufsw_default[0])
    snprintf(prompt, sizeof(prompt), "Switch to buffer (default %s): ", path_base(bufsw_default));
  else
    snprintf(prompt, sizeof(prompt), "Switch to buffer: ");
  minibuf_open(prompt, 0, NULL);
  bufsw_active = 1; bufsw_listing = 0; bufsw_sel = 0; bufsw_count = 0;
}

static int handle_minibuf_key(int sym, int ctrl) {
  if (bufsw_active) return handle_bufsw_key(sym, ctrl);
  if (sym == SDLK_RETURN) {
    g_vs.minibuf_active = 0;
    minibuf_completing = 0;
    minibuf_suggest[0] = '\0';
    g_vs.suppress_next_text = 0;   /* never let a stale suppression leak into the buffer */
    if (g_vs.minibuf_callback) g_vs.minibuf_callback(g_vs.minibuf_text);
    g_vs.minibuf_callback = NULL;
    return 1;
  }
  if (sym == SDLK_ESCAPE || (ctrl && sym == SDLK_g)) {
    g_vs.minibuf_active = 0;
    minibuf_completing = 0;
    minibuf_suggest[0] = '\0';
    g_vs.suppress_next_text = 0;
    g_vs.minibuf_callback = NULL;
    nav_status_set(&g_vs, "Quit");
    return 1;
  }
  if (sym == SDLK_TAB) {
    /* accept the ghost completion */
    if (minibuf_completing && minibuf_suggest[0] &&
        (int)strlen(minibuf_suggest) > g_vs.minibuf_len) {
      snprintf(g_vs.minibuf_text, sizeof(g_vs.minibuf_text), "%s", minibuf_suggest);
      g_vs.minibuf_len = (int)strlen(g_vs.minibuf_text);
      minibuf_refresh_completion();
    }
    /* Tab emits no text-input character, so nothing needs suppressing here.
       Setting the flag would linger and eat the next real keystroke. */
    return 1;
  }
  if (sym == SDLK_BACKSPACE) {
    if (g_vs.minibuf_len > 0) {
      g_vs.minibuf_len--;
      g_vs.minibuf_text[g_vs.minibuf_len] = '\0';
    }
    minibuf_refresh_completion();
    return 1;
  }
  return 0;
}

static int handle_search_key(int sym, int ctrl) {
  if (sym == SDLK_ESCAPE || sym == SDLK_RETURN) {
    g_vs.search_active = 0;
    return 1;
  }
  if (ctrl && sym == SDLK_s) {
    g_vs.search_direction = 1;
    if (g_vs.search_match_line >= 0) nav_search_find_next(&g_ed, &g_vs, g_vs.search_match_line, g_vs.search_match_col);
    else nav_search_find_first(&g_ed, &g_vs);
    return 1;
  }
  if (ctrl && (sym == SDLK_r || sym == SDLK_b)) {
    g_vs.search_direction = -1;
    if (g_vs.search_match_line >= 0) nav_search_find_prev(&g_ed, &g_vs, g_vs.search_match_line, g_vs.search_match_col);
    else nav_search_find_first(&g_ed, &g_vs);
    return 1;
  }
  if (ctrl && sym == SDLK_f) {
    g_vs.search_direction = 1;
    if (g_vs.search_match_line >= 0) nav_search_find_next(&g_ed, &g_vs, g_vs.search_match_line, g_vs.search_match_col);
    else nav_search_find_first(&g_ed, &g_vs);
    return 1;
  }
  if (ctrl && sym == SDLK_g) {
    g_vs.search_active = 0;
    g_vs.search_match_line = -1;
    return 1;
  }
  if (sym == SDLK_BACKSPACE) {
    if (g_vs.search_len > 0) {
      g_vs.search_len--;
      g_vs.search_buf[g_vs.search_len] = '\0';
      if (g_vs.search_len > 0) nav_search_find_current_dir(&g_ed, &g_vs);
      else g_vs.search_match_line = -1;
    }
    return 1;
  }
  return 0;
}

static int handle_esc_prefix_key(int sym, int shift) {
  g_vs.esc_prefix = 0;
  SDL_StartTextInput();
  if (shift && sym == SDLK_PERIOD) {
    /* M-> : end of buffer, set mark first */
    buf_mark_set(&g_ed);
    g_ed.cursor_line = buf_content_line_count(&g_ed) - 1;   /* stop at the editable page */
    g_ed.cursor_col = g_ed.lines[g_ed.cursor_line].len;
    g_ed.cursor_target_col = g_ed.cursor_col;
    nav_ensure_cursor_visible(&g_ed, &g_vs);
    nav_status_set(&g_vs, "Mark set");
    g_vs.suppress_next_text = 1;
    return 1;
  }
  if (shift && sym == SDLK_COMMA) {
    /* M-< : beginning of buffer, set mark first */
    buf_mark_set(&g_ed);
    g_ed.cursor_line = 0; g_ed.cursor_col = 0; g_ed.cursor_target_col = 0;
    nav_ensure_cursor_visible(&g_ed, &g_vs);
    nav_status_set(&g_vs, "Mark set");
    g_vs.suppress_next_text = 1;
    return 1;
  }
  if (sym == SDLK_f) {
    ed_emacs_forward_word(&g_ed); nav_ensure_cursor_visible(&g_ed, &g_vs); return 1;
  }
  if (sym == SDLK_b) {
    ed_emacs_backward_word(&g_ed); nav_ensure_cursor_visible(&g_ed, &g_vs); return 1;
  }
  if (sym == SDLK_w) { cmd_copy_region(&g_ed, &g_vs); return 1; }      /* M-w copy */
  if (sym == SDLK_v) { cmd_page_up(&g_ed, &g_vs); return 1; }  /* M-v page up */
  if (sym == SDLK_d) { cmd_kill_word_fwd(&g_ed, &g_vs); return 1; }    /* M-d kill word */
  if (sym == SDLK_u) { cmd_upcase_word(&g_ed, &g_vs); return 1; }      /* M-u upcase */
  if (sym == SDLK_l) { cmd_downcase_word(&g_ed, &g_vs); return 1; }    /* M-l downcase */
  if (sym == SDLK_c) { cmd_capitalize_word(&g_ed, &g_vs); return 1; }  /* M-c capitalize */
  if (sym == SDLK_g) { cmd_goto_line(); return 1; }        /* M-g goto line */
  return 1; /* consume even if unrecognized — prefix is cleared */
}

static int handle_cx_prefix_key(int sym, int ctrl) {
  g_vs.ctrl_x_prefix = 0;
  if (ctrl && sym == SDLK_s) {
    /* C-x C-s: save (honest status, sandbox-resolved path) */
    if (g_ed.filepath[0]) {
      save_to_path(g_ed.filepath);
    } else {
      minibuf_open("Write file (Documents): ", 1, save_to_path);
    }
    return 1;
  }
  if (ctrl && sym == SDLK_c) {
    exit(EXIT_SUCCESS);
    return 1;
  }
  if (ctrl && sym == SDLK_f) {   /* C-x C-f: find file */
    minibuf_open("Find file (Documents): ", 1, open_or_create_file);
    return 1;
  }
  if (ctrl && sym == SDLK_w) {   /* C-x C-w: write-file (save as) */
    minibuf_open("Write file (Documents): ", 1, save_to_path);
    return 1;
  }
  if (!ctrl && sym == SDLK_h) { cmd_mark_whole_buffer(&g_ed, &g_vs); return 1; }   /* C-x h */
  if (ctrl && sym == SDLK_x)  { cmd_exchange_point_mark(&g_ed, &g_vs); return 1; } /* C-x C-x */
  if (!ctrl && sym == SDLK_b) { cmd_switch_buffer(); return 1; }       /* C-x b */
  if (!ctrl && sym == SDLK_t) {                                       /* C-x t */
    cmd_toggle_typewriter(&g_ed, &g_vs);
    g_vs.suppress_next_text = 1;   /* swallow the "t" text event that triggered this */
    return 1;
  }
  if (!ctrl && sym == SDLK_y) {                                       /* C-x y */
    kern_toggle_syntax();
    g_vs.suppress_next_text = 1;   /* swallow the "y" text event */
    return 1;
  }
  if (!ctrl && sym == SDLK_s) {                                       /* C-x s */
    kern_toggle_style();
    g_vs.suppress_next_text = 1;   /* swallow the "s" text event */
    return 1;
  }
  if (!ctrl && sym == SDLK_l) {                                       /* C-x l */
    kern_toggle_subs();
    g_vs.suppress_next_text = 1;   /* swallow the "l" text event */
    return 1;
  }
  if (!ctrl && sym == SDLK_p) {                                       /* C-x p */
    g_vs.page_furniture_hidden = !g_vs.page_furniture_hidden;
    nav_status_set(&g_vs, g_vs.page_furniture_hidden ? "Page borders hidden" : "Page borders shown");
    g_vs.suppress_next_text = 1;   /* swallow the "p" text event */
    return 1;
  }
  return 1; /* consume even if unrecognized — prefix is cleared */
}

/* ---- SDL boilerplate ---- */

static const char button_map[256] = {
  [ SDL_BUTTON_LEFT   & 0xff ] =  MOUSE_LEFT,
  [ SDL_BUTTON_RIGHT  & 0xff ] =  MOUSE_RIGHT,
  [ SDL_BUTTON_MIDDLE & 0xff ] =  MOUSE_MIDDLE,
};


/* ---- render pass (callable from main loop and resize watcher) ---- */

/* ---- wikilink autocomplete ([[ … ]]) ---- */
#define WL_MAX 5
static int  wl_active;
static int  wl_count;
static int  wl_sel;
static int  wl_query_col;
static int  wl_query_line;
static char wl_query[256];
static char wl_last_query[256];
static char wl_matches[WL_MAX][256];
static char wl_suppressed[256];
static int  wl_has_suppress;

/* Recompute the active "[[" query before the caret and its match list. */
static void wikilink_refresh(void) {
  wl_active = 0;
  if (g_vs.minibuf_active || g_vs.search_active) return;
  Line *l = &g_ed.lines[g_ed.cursor_line];
  int c = g_ed.cursor_col;
  if (c < 2) return;
  int open = -1;
  for (int i = c - 2; i >= 0; i--) {
    if (l->text[i] == ']') return;                       /* closed / not a link */
    if (l->text[i] == '[' && l->text[i+1] == '[') { open = i; break; }
  }
  if (open < 0) return;
  int qstart = open + 2;
  for (int i = qstart; i < c; i++)
    if (l->text[i] == '[' || l->text[i] == ']') return;  /* query must be clean */
  /* if this "[[" is already closed by a "]]" ahead of the caret, it's an
     existing link being edited — not a query being typed. Don't show the
     dropdown (so Cmd-Enter navigates instead of rewriting the link). */
  for (int i = c; i + 1 < l->len; i++) {
    if (l->text[i] == '[' && l->text[i+1] == '[') break;     /* next link starts */
    if (l->text[i] == ']' && l->text[i+1] == ']') return;    /* already closed */
  }
  int qlen = c - qstart;
  if (qlen >= (int)sizeof(wl_query)) return;
  memcpy(wl_query, l->text + qstart, qlen);
  wl_query[qlen] = '\0';

  if (strcmp(wl_query, wl_last_query) != 0) {             /* new query → top item */
    wl_sel = 0;
    snprintf(wl_last_query, sizeof(wl_last_query), "%s", wl_query);
  }
  if (wl_has_suppress && strcmp(wl_query, wl_suppressed) == 0) return;  /* Esc-dismissed */
  wl_has_suppress = 0;

  wl_count = buf_list_matches(wl_query, wl_matches, WL_MAX);
  if (wl_count <= 0) return;
  if (wl_sel >= wl_count) wl_sel = wl_count - 1;
  if (wl_sel < 0) wl_sel = 0;
  wl_query_col = qstart;
  wl_query_line = g_ed.cursor_line;
  wl_active = 1;
}

/* Replace the typed "[[query" with the selected match, closed: "[[Name]]". */
static int wikilink_accept(void) {
  if (!wl_active || wl_count == 0) return 0;
  int qlen = (int)strlen(wl_query);
  if (g_ed.cursor_line != wl_query_line || g_ed.cursor_col != wl_query_col + qlen) return 0;
  undo_begin_group(&g_ed);
  for (int i = 0; i < qlen; i++) ed_backspace(&g_ed);   /* delete the typed query */
  ed_insert_char(&g_ed, wl_matches[wl_sel]);              /* insert the note name */
  ed_insert_char(&g_ed, "]]");                            /* close the link */
  undo_end_group(&g_ed);
  wl_active = 0;
  nav_ensure_cursor_visible(&g_ed, &g_vs);
  return 1;
}

static int handle_wikilink_key(int sym, int ctrl) {
  if (sym == SDLK_RETURN || sym == SDLK_TAB) return wikilink_accept();
  if (sym == SDLK_DOWN) { wl_sel = (wl_sel + 1) % wl_count; return 1; }
  if (sym == SDLK_UP)   { wl_sel = (wl_sel - 1 + wl_count) % wl_count; return 1; }
  if (sym == SDLK_ESCAPE || (ctrl && sym == SDLK_g)) {   /* dismiss (Esc or C-g) */
    snprintf(wl_suppressed, sizeof(wl_suppressed), "%s", wl_query);
    wl_has_suppress = 1;
    wl_active = 0;
    return 1;
  }
  return 0;
}

/* Fill a guard rect with ONE rounded top corner (the inner one): the body below
   the corner is a full rect; the top r rows taper in via horizontal strips along
   a quarter circle. round_right rounds the top-right corner, else the top-left. */
static void fill_rounded_guard(int x, int y, int w, int h, int r, int round_right, Color c) {
  if (r > w) r = w;
  if (r > h) r = h;
  r_draw_rect(rect(x, y + r, w, h - r), c);            /* body below the rounded rows */
  for (int i = 0; i < r; i++) {
    int dy = r - i;                                     /* distance above the arc center */
    int inset = r - (int)(sqrt((double)(r * r - dy * dy)) + 0.5);
    if (round_right) r_draw_rect(rect(x, y + i, w - inset, 1), c);          /* taper the right */
    else             r_draw_rect(rect(x + inset, y + i, w - inset, 1), c);  /* taper the left  */
  }
}

/* A dotted vertical line from y0 to y1. */
static void draw_dotted_vline(int x, int y0, int y1, Color c) {
  for (int y = y0; y < y1; y += 7) {        /* 3px dash, 4px gap */
    int dh = 3; if (y + dh > y1) dh = y1 - y;
    r_draw_rect(rect(x, y, 1, dh), c);
  }
}

/* Width of each page margin strip (where notes live). */
static int tv_margin_pad(void) { return (int)(g_vs.font_size * 12.5f); }   /* note-strip width */

/* The two dotted "gutter" lines that hug a note column (side 0 = right, 1 = left),
   in natural/unshifted coords (*a < *b). Each is inset by the same `npad` — the
   outer line that far from the page border, the inner line that far from the
   page's dotted writing edge — so the note sits in a symmetric gutter. */
static void tv_note_gutter(int side, int *a, int *b) {
  int margin = nav_page_margin(), page_w = nav_page_w(), pad = tv_margin_pad();
  int npad = (int)(pad * 0.18f);
  if (side == 0) { *a = margin + page_w + npad; *b = margin + page_w + pad - npad; }
  else           { *a = margin - pad + npad;    *b = margin - npad; }
}
/* Note-text column for a side, padded inside the gutter. */
static void tv_note_col(int side, int *x0, int *w) {
  int a, b, tp = 8;
  tv_note_gutter(side, &a, &b);
  *x0 = a + tp; *w = (b - a) - 2 * tp;
}

/* Page furniture: the dotted writing-area edges, the dotted note gutters, the
   solid page-margin rails, and the page top/bottom edges. One drawing for ALL
   modes — a fixed framed page: a closed rectangle filling the writing area (top
   of content down to just above the status bar), with the vertical rails clipped
   to that frame and solid top/bottom edges closing it off. Tracks scroll_x (0 in
   normal mode, the horizontal-pin offset in typewriter mode). */
static void draw_page_furniture(void) {
  int margin = nav_page_margin();
  int page_w = nav_page_w();
  int sx     = (int)g_vs.scroll_x;
  int cy     = g_vs.content_y, ch = g_vs.content_h;

  Color dotted = color(120, 120, 125, 80);
  Color solid  = color(120, 120, 125, 150);
  int pad = tv_margin_pad();
  int top_y    = cy;                                   /* fixed frame filling the writing area */
  int bottom_y = cy + ch - 1;
  int vtop = top_y, vbot = bottom_y;
  /* the solid page edge sits npad outside the outer gutter; halve that overhang
     (pulls the edge in toward the notes, leaving a bit more window margin). */
  int npad2 = (int)(pad * 0.09f);                      /* half of tv_note_gutter's npad (0.18) */
  int left_x  = margin - pad + npad2 - sx;
  int right_x = margin + page_w + pad - npad2 - sx;
  if (vbot > vtop) {
    draw_dotted_vline(margin - sx, vtop, vbot, dotted);               /* writing area left  */
    draw_dotted_vline(margin + page_w - sx, vtop, vbot, dotted);      /* writing area right */
    int ga, gb;                                                       /* note gutter (two lines per side) */
    tv_note_gutter(0, &ga, &gb);
    draw_dotted_vline(ga - sx, vtop, vbot, dotted);
    draw_dotted_vline(gb - sx, vtop, vbot, dotted);
    tv_note_gutter(1, &ga, &gb);
    draw_dotted_vline(ga - sx, vtop, vbot, dotted);
    draw_dotted_vline(gb - sx, vtop, vbot, dotted);
    r_draw_rect(rect(left_x,  vtop, 1, vbot - vtop), solid);          /* page margin left   */
    r_draw_rect(rect(right_x, vtop, 1, vbot - vtop), solid);          /* page margin right  */
  }
  r_draw_rect(rect(left_x, top_y, right_x - left_x + 1, 1), solid);   /* page TOP edge      */
  r_draw_rect(rect(left_x, bottom_y, right_x - left_x + 1, 1), solid);/* page BOTTOM edge   */
}

/* Typewriter frosted guards + hitzone (typewriter mode only): the two rounded
   plastic guards flanking the centred clear band, plus the bottom frost panel.
   Drawn over the text + page furniture but under the caret. */
static void draw_typewriter_fog(void) {
  int lh     = nav_line_height();
  int win_w  = nav_win_w();
  int margin = nav_page_margin();
  int page_w = nav_page_w();
  int cy     = g_vs.content_y, ch = g_vs.content_h;

  /* the strike line is FIXED at the golden pin height — not the (gliding) caret
     row — so the frame stays put while the page scrolls under it. Guards are two
     lines tall with the active line flush at their BOTTOM, so the extra height
     grows upward (the rounded tops sit above the line, like the reference). */
  int strike = cy + (int)((ch - lh) * TYPEWRITER_FRACTION);
  int gh = 2 * lh;
  int gy = strike + lh - gh;                           /* bottom covers the full line height (descenders) */

  /* The clear "hitzone" stays centered on the page; the page scrolls the note
     column into it (see process_frame). While writing a note the band just widens
     to fit the note column. */
  int pin_x = margin + page_w / 2;
  int half;
  if (mn_active) {
    int rx0, mw; tv_note_col(0, &rx0, &mw);
    half = mw / 2 + 24;
  } else {
    half = (int)(page_w * 0.12f);                      /* clear band half-width (hugs the caret) */
  }
  int left_edge  = pin_x - half;
  int right_edge = pin_x + half;
  Color plastic = color(45, 45, 47, 165);              /* darker frosted tint over the blur */
  int radius = gh / 2;                                 /* ~50% rounded inner top corner */

  /* Each guard: stencil-mask the rounded shape, then draw the blur + tint clipped
     to it, so both the blur and the tint share the one rounded corner (no square
     blur boundary poking past the round). */
  if (left_edge > 0) {
    Rect gr = rect(0, gy, left_edge, gh);
    r_clip_mask_begin();
    fill_rounded_guard(0, gy, left_edge, gh, radius, 1, plastic);   /* defines the mask */
    r_clip_mask_use();
    r_blur_rect(gr, 5);
    r_draw_rect(gr, plastic);
    r_clip_mask_end();
  }
  if (right_edge < win_w) {
    Rect gr = rect(right_edge, gy, win_w - right_edge, gh);
    r_clip_mask_begin();
    fill_rounded_guard(right_edge, gy, win_w - right_edge, gh, radius, 0, plastic);
    r_clip_mask_use();
    r_blur_rect(gr, 5);
    r_draw_rect(gr, plastic);
    r_clip_mask_end();
  }

  /* third panel: the whole area below the guards to the bottom of the content,
     frosting everything past the current line (no rounding — a plain rect). */
  int below_y = gy + gh;
  int below_h = (cy + ch) - below_y;
  if (below_h > 0) {
    Rect br = rect(0, below_y, win_w, below_h);
    r_blur_rect(br, 5);
    r_draw_rect(br, plastic);
  }
}

/* Word-wrap `s` into the column [x, x+maxw], `fh` apart from y. When draw==0 it
   only measures (no glyphs). Returns the pen x after the last glyph; *out_y gets
   the last line's y (so height = *out_y - y + fh). */
static int draw_wrapped(const char *s, int x, int y, int maxw, int fh, Color c, int *out_y, int draw) {
  int penx = x, peny = y;
  int spw = r_get_text_width(" ", 1);
  const char *p = s;
  while (*p) {
    if (*p == ' ') { penx += spw; p++; continue; }
    const char *start = p;
    while (*p && *p != ' ') p++;
    int wlen = (int)(p - start);
    int ww = r_get_text_width(start, wlen);
    if (penx > x && penx + ww > x + maxw) { penx = x; peny += fh; }
    char buf[512];
    int bl = wlen < (int)sizeof(buf) - 1 ? wlen : (int)sizeof(buf) - 1;
    memcpy(buf, start, bl); buf[bl] = '\0';
    if (draw) r_draw_text(buf, vec2(penx, peny), c);
    penx += ww;
  }
  if (out_y) *out_y = peny;
  return penx;
}

/* draw_wrapped at the margin-note scale (MN_SCALE of the body font). The quad
   scale is set only around the text pass itself — never while nav geometry
   (nav_line_height, nav_get_wrap_breaks) is measured, which would shrink line
   heights and corrupt the wrap caches. */
static int draw_note_wrapped(const char *s, int x, int y, int maxw, int fh, Color c, int *out_y, int draw) {
  r_set_font_scale(MN_SCALE);
  int penx = draw_wrapped(s, x, y, maxw, fh, c, out_y, draw);
  r_set_font_scale(1.0f);
  return penx;
}

/* Footnote definition text for `id` (into the NUL-terminated def line), or NULL. */
static const char *find_footnote_def(const char *id) {
  char pat[40];
  int pl = snprintf(pat, sizeof(pat), "[^%s]:", id);
  for (int i = 0; i < g_ed.line_count; i++) {
    Line *l = &g_ed.lines[i];
    if (l->len >= pl && strncmp(l->text, pat, pl) == 0) {
      const char *t = l->text + pl;
      while (*t == ' ') t++;
      return t;
    }
  }
  return NULL;
}

static int row_py_for_line(int ln) {
  for (int i = 0; i < g_vs.vis_row_count; i++)
    if (g_vs.vis_rows[i].ln == ln) return g_vs.vis_rows[i].py;
  return -1;
}

/* True if visual row `i` is the one the caret sits on: same logical line, with
   the caret column inside [row_start,row_end) — or at end-of-line on the line's
   last visual row. The single row test shared by the caret-py lookup and both
   do_render caret passes. */
static int is_caret_row(int i) {
  VisRow *vr = &g_vs.vis_rows[i];
  return vr->ln == g_ed.cursor_line && g_ed.cursor_col >= vr->row_start &&
         (g_ed.cursor_col < vr->row_end ||
          (i + 1 >= g_vs.vis_row_count || g_vs.vis_rows[i+1].ln != vr->ln));
}

/* py of the visual row the caret sits on (matches the cursor-draw row test) —
   the wrapped-line-aware row, so the margin input lines up with the marker. */
static int caret_row_py(void) {
  for (int i = 0; i < g_vs.vis_row_count; i++)
    if (is_caret_row(i)) return g_vs.vis_rows[i].py;
  return -1;
}

/* Which margin each footnote sits in, cached by id so notes stay put while
   scrolling. Recomputed only when a footnote id appears that isn't cached yet
   (i.e. a new note was written, or a new document loaded) — never per frame. */
#define FN_SIDE_MAX 256
static struct { char id[24]; int side; } g_fn_sides[FN_SIDE_MAX];   /* side: 0 right, 1 left */
static int g_fn_side_count;

static int fn_side(const char *id) {
  for (int i = 0; i < g_fn_side_count; i++)
    if (strcmp(g_fn_sides[i].id, id) == 0) return g_fn_sides[i].side;
  return 0;
}
static int fn_side_known(const char *id) {
  for (int i = 0; i < g_fn_side_count; i++)
    if (strcmp(g_fn_sides[i].id, id) == 0) return 1;
  return 0;
}

/* Lay every footnote out in document space (scroll-independent): flow down the
   right margin, spilling a note that collides with the one above it to the left.
   Cache the resulting side per id. */
static void recompute_footnote_sides(void) {
  g_fn_side_count = 0;
  int lh  = nav_line_height();              /* body row height — the reference's document y */
  int fh  = mn_line_height();               /* note line height: MN_SCALE, matching the draw */
  int ncx, mw; tv_note_col(0, &ncx, &mw);   /* note-column width (heights must match the draw) */
  int saved = r_get_font_style();
  r_set_font_style(g_vs.typewriter_mode ? FONT_MONO : FONT_REGULAR);
  Color dummy = color(0, 0, 0, 0);
  int right_bottom = -1000000;
  for (int i = 0; i < g_ed.line_count && g_fn_side_count < FN_SIDE_MAX; i++) {
    Line *l = &g_ed.lines[i];
    for (int k = 0; k + 2 < l->len; k++) {
      if (l->text[k] != '[' || l->text[k+1] != '^') continue;
      int j = k + 2; char id[24]; int n = 0;
      while (j < l->len && l->text[j] != ']' && n < (int)sizeof(id) - 1) id[n++] = l->text[j++];
      id[n] = '\0';
      if (j >= l->len || l->text[j] != ']') continue;
      if (j + 1 < l->len && l->text[j+1] == ':') { k = j; continue; }   /* definition */
      const char *def = find_footnote_def(id);
      if (def) {
        int docy = nav_cursor_to_visual(&g_ed, i, k) * lh;
        int ly = docy;
        draw_note_wrapped(def, 0, docy, mw, fh, dummy, &ly, 0);
        int height = ly - docy + fh;
        int side = 0;
        if (docy >= right_bottom) { side = 0; right_bottom = docy + height + 14; }
        else side = 1;
        snprintf(g_fn_sides[g_fn_side_count].id, sizeof(g_fn_sides[0].id), "%s", id);
        g_fn_sides[g_fn_side_count].side = side;
        g_fn_side_count++;
      }
      k = j;
    }
  }
  r_set_font_style(saved);
}

/* Draw the saved footnote definitions in the page margins, each on its cached
   side. A note is drawn whenever ANY part of it is on screen (computed from the
   reference's own row position, not whether that row is in the visible set) — so
   a note doesn't vanish when its marker scrolls off the top while its body is
   still visible. While a new note is being typed, any note within the live
   input's growing extent spills left early. Drawn BEFORE the typewriter fog so
   the frost blurs them with the page. Typewriter mode only. */
static void draw_margin_notes(void) {
  int cy = g_vs.content_y, ch = g_vs.content_h;
  int sx = (int)g_vs.scroll_x, sy = (int)g_vs.scroll_y;
  int gap = 14;
  int rx0, mw, lx0, lw; tv_note_col(0, &rx0, &mw); tv_note_col(1, &lx0, &lw);
  int rmx = rx0 - sx, lmx = lx0 - sx;

  r_set_font_style(g_vs.typewriter_mode ? FONT_MONO : FONT_REGULAR);   /* never r_set_font_size here — it rebuilds the atlases (lag) */
  int lh = nav_line_height();                /* body row height — positions refY against the text */
  int fh = mn_line_height();                 /* the note's own line height: MN_SCALE of the body's */
  Color ink = color(150, 150, 155, 255);

  /* the live input's screen extent, so a note below it spills left as it grows */
  int live_top = -1, live_bottom = -1;
  if (mn_active) {
    int py = caret_row_py();
    if (py < 0) py = row_py_for_line(mn_ref_line);
    if (py >= 0) { int ly = py; draw_note_wrapped(mn_text, rmx, py, mw, fh, ink, &ly, 0);
                   live_top = py; live_bottom = ly + fh + gap; }
  }

  /* one pass over the buffer, tracking the running visual-row index so each
     reference's screen y is known even when its row is above the viewport. */
  int visual = 0;
  for (int i = 0; i < g_ed.line_count; i++) {
    Line *l = &g_ed.lines[i];
    int wc = l->wrap_count > 0 ? l->wrap_count : 1;
    for (int k = 0; k + 2 < l->len; k++) {
      if (l->text[k] != '[' || l->text[k+1] != '^') continue;
      int j = k + 2; char id[24]; int n = 0;
      while (j < l->len && l->text[j] != ']' && n < (int)sizeof(id) - 1) id[n++] = l->text[j++];
      id[n] = '\0';
      if (j >= l->len || l->text[j] != ']') { k = j > k ? j : k; continue; }
      if (j + 1 < l->len && l->text[j+1] == ':') { k = j; continue; }   /* a definition */
      const char *def = find_footnote_def(id);
      if (def) {
        int row = 0;
        if (wc > 1) {   /* which wrapped row holds column k */
          int starts[256], nrows = nav_get_wrap_breaks(l, starts, 256);
          for (int r = nrows - 1; r >= 0; r--) if (k >= starts[r]) { row = r; break; }
        }
        int refY = cy + (visual + row) * lh - sy;
        if (refY <= cy + ch && refY >= cy - 80 * lh) {        /* in range to possibly show */
          int ely = refY; draw_note_wrapped(def, rmx, refY, mw, fh, ink, &ely, 0);
          int height = ely - refY + fh;
          if (refY + height >= cy && refY < cy + ch) {        /* any part on screen */
            if (!fn_side_known(id)) recompute_footnote_sides();
            int side = fn_side(id);
            if (mn_active && live_top >= 0 && refY > live_top && refY < live_bottom) side = 1;
            draw_note_wrapped(def, side ? lmx : rmx, refY, mw, fh, ink, NULL, 1);
          }
        }
      }
      k = j;
    }
    visual += wc;
  }

  r_set_font_style(FONT_REGULAR);
}

/* The live margin-note input (sharp, in the cleared hitzone). Drawn AFTER the fog
   so it isn't frosted. */
static void draw_margin_note_input(void) {
  if (!mn_active) return;
  int sx  = (int)g_vs.scroll_x;
  int rx0, mw; tv_note_col(0, &rx0, &mw);
  int mx  = rx0 - sx;
  r_set_font_style(g_vs.typewriter_mode ? FONT_MONO : FONT_REGULAR);
  int fh = mn_line_height();                 /* note line height: MN_SCALE of the body's */
  int py = caret_row_py();
  if (py < 0) py = row_py_for_line(mn_ref_line);
  if (py < 0) py = g_vs.content_y + 8;
  int endy = py;
  int endx = draw_note_wrapped(mn_text, mx, py, mw, fh, color(210, 210, 215, 255), &endy, 1);
  r_draw_rect(rect(endx + 1, endy, 3, (int)(r_get_text_height() * MN_SCALE + 0.5f)), color(90, 200, 250, 255));  /* same shape as the body caret, note-sized */
  r_set_font_style(FONT_REGULAR);
}

/* The dotted rule + centered "Context" label drawn on the first read-only
   section row, separating the editable page from the auto-generated Context. */
static void draw_context_divider(VisRow *vr) {
  int sepy = vr->py + nav_line_height() / 2;
  int x0 = nav_page_margin() - (int)g_vs.scroll_x;
  int x1 = x0 + nav_page_w();
  Color dotc = color(120, 120, 128, 120);
  const char *label = "Context";
  r_set_font_style(FONT_REGULAR);
  int lw = r_get_text_width(label, 7);
  int th = r_get_text_height();
  int cx = (x0 + x1) / 2;
  int lstart = cx - lw / 2, lend = cx + lw / 2;
  int gap = 14;   /* clear space around the label */
  for (int dx = x0; dx < lstart - gap; dx += 6) r_draw_rect(rect(dx, sepy, 2, 1), dotc);
  for (int dx = lend + gap; dx < x1; dx += 6)    r_draw_rect(rect(dx, sepy, 2, 1), dotc);
  r_draw_text(label, vec2(lstart, sepy - th / 2), color(150, 150, 158, 255));
}

/* The [[ wikilink autocomplete dropdown, anchored under the "[[" query at the
   caret (caret_py = the py of the caret's row). */
static void draw_wikilink_dropdown(int caret_py) {
  r_set_font_style(FONT_REGULAR);
  r_set_clip_rect(rect(0, 0, nav_win_w(), nav_win_h()));
  int fh = r_get_text_height();
  int lh = nav_line_height();
  int item_h = fh + 6;
  int box_w = 0;
  for (int i = 0; i < wl_count; i++) {
    int w = r_get_text_width(wl_matches[i], (int)strlen(wl_matches[i]));
    if (w > box_w) box_w = w;
  }
  box_w += 18;
  if (box_w < 140) box_w = 140;
  int box_h = wl_count * item_h;   /* exact fit — items tile edge to edge */
  int bx = g_vs.cursor_x - r_get_text_width(wl_query, (int)strlen(wl_query))
                      - r_get_text_width("[[", 2);
  if (bx < nav_page_margin()) bx = nav_page_margin();
  int by = caret_py + lh;
  /* border + background */
  r_draw_rect(rect(bx - 1, by - 1, box_w + 2, box_h + 2), color(90, 90, 96, 255));
  r_draw_rect(rect(bx, by, box_w, box_h), color(48, 48, 52, 255));
  for (int i = 0; i < wl_count; i++) {
    int iy = by + i * item_h;
    if (i == wl_sel)
      r_draw_rect(rect(bx, iy, box_w, item_h), color(104, 68, 158, 235));
    r_draw_text(wl_matches[i], vec2(bx + 9, iy + (item_h - fh) / 2),
                color(222, 218, 212, 255));
  }
}

/* The bottom status bar (mono): background + the C-x b candidate list,
   minibuffer / isearch / status message on the left, position + dev badge on
   the right. */
static void draw_status_bar(void) {
  r_set_font_size(g_vs.font_size);
  r_set_font_style(FONT_MONO);
  int bar_h = r_get_text_height() + 16;
  int bar_y = nav_win_h() - bar_h;
  r_set_clip_rect(rect(0, 0, nav_win_w(), nav_win_h()));

  /* background */
  r_draw_rect(rect(0, bar_y, nav_win_w(), bar_h), color(40, 40, 42, 255));
  /* top border */
  r_draw_rect(rect(0, bar_y, nav_win_w(), 1), color(55, 55, 57, 255));

  /* C-x b candidate list, stacked above the status bar (Tab cycles selection) */
  if (bufsw_active && bufsw_listing && bufsw_count > 0) {
    int fh = r_get_text_height();
    int item_h = fh + 6;
    int box_w = 0;
    for (int i = 0; i < bufsw_count; i++) {
      const char *b = path_base(bufsw_cands[i]);
      int w = r_get_text_width(b, strlen(b));
      if (w > box_w) box_w = w;
    }
    box_w += 20; if (box_w < 220) box_w = 220;
    int box_h = bufsw_count * item_h;
    /* align the list under where the input starts (after the prompt) */
    int bx = 10 + r_get_text_width(g_vs.minibuf_prompt, strlen(g_vs.minibuf_prompt));
    if (bx + box_w > nav_win_w() - 10) bx = nav_win_w() - box_w - 10;
    if (bx < 10) bx = 10;
    int by = bar_y - box_h;
    r_draw_rect(rect(bx - 1, by - 1, box_w + 2, box_h + 2), color(90, 90, 96, 255));
    r_draw_rect(rect(bx, by, box_w, box_h), color(48, 48, 52, 255));
    for (int i = 0; i < bufsw_count; i++) {
      int iy = by + i * item_h;
      if (i == bufsw_sel)
        r_draw_rect(rect(bx, iy, box_w, item_h), color(104, 68, 158, 235));
      r_draw_text(path_base(bufsw_cands[i]), vec2(bx + 9, iy + (item_h - fh) / 2),
                  color(222, 218, 212, 255));
    }
  }

  /* left: minibuffer input, isearch, or status message */
  if (g_vs.minibuf_active) {
    r_draw_text(g_vs.minibuf_prompt, vec2(10, bar_y + 5), color(170, 170, 170, 255));
    int lw = r_get_text_width(g_vs.minibuf_prompt, strlen(g_vs.minibuf_prompt));
    r_draw_text(g_vs.minibuf_text, vec2(10 + lw, bar_y + 5), color(204, 200, 195, 255));
    int cx = 10 + lw + r_get_text_width(g_vs.minibuf_text, g_vs.minibuf_len);
    int fh = r_get_text_height();
    /* ghost completion of an existing filename (Tab to accept) */
    if (minibuf_suggest[0] && (int)strlen(minibuf_suggest) > g_vs.minibuf_len) {
      const char *ghost = minibuf_suggest + g_vs.minibuf_len;
      r_draw_text(ghost, vec2(cx, bar_y + 5), color(110, 110, 112, 255));
    }
    r_draw_rect(rect(cx, bar_y + 4, 2, fh), color(90, 200, 250, 255));
  } else if (g_vs.search_active) {
    const char *label = (g_vs.search_direction == 1) ? "I-search: " : "I-search backward: ";
    r_draw_text(label, vec2(10, bar_y + 5), color(170, 170, 170, 255));
    int lw = r_get_text_width(label, strlen(label));
    r_draw_text(g_vs.search_buf, vec2(10 + lw, bar_y + 5), color(204, 200, 195, 255));
    int cx = 10 + lw + r_get_text_width(g_vs.search_buf, g_vs.search_len);
    int fh = r_get_text_height();
    r_draw_rect(rect(cx, bar_y + 4, 2, fh), color(90, 200, 250, 255));
  } else {
    const char *status = nav_status_get(&g_ed, &g_vs);
    if (status[0]) {
      r_draw_text(status, vec2(10, bar_y + 5), color(170, 170, 170, 255));
    }
  }

  /* right: line, col, font size */
  char info[64];
  snprintf(info, sizeof(info), "(%d,%d)  %.0fpt", g_ed.cursor_line + 1, g_ed.cursor_col + 1, g_vs.font_size);
  int info_w = r_get_text_width(info, strlen(info));
  r_draw_text(info, vec2(nav_win_w() - info_w - 10, bar_y + 5), color(120, 120, 120, 255));
#ifdef DEBUG
  /* dev-build marker (Debug config only; never in a shipped Release build) */
  {
    char dev[48];
    snprintf(dev, sizeof(dev), "Dev v%s", kern_app_version());
    int dw = r_get_text_width(dev, strlen(dev));
    r_draw_text(dev, vec2(nav_win_w() - info_w - 10 - dw - 16, bar_y + 5), color(190, 140, 90, 220));
  }
#endif
  r_set_font_size(g_vs.font_size);
}

/* ---- X publish confirmation overlay (drawn in do_render) ------------------ */

typedef struct {
  Rect card;                       /* the whole preview card */
  int  av_x, av_y, av_d;           /* avatar disc: top-left + diameter */
  int  name_x, name_y;             /* display-name / @handle baseline row */
  Rect body;                       /* the (clipped) tweet-text region */
  Rect confirm, cancel;            /* the two action buttons */
} PubLayout;

/* Split `text` into paragraphs on '\n' and lay them out with draw_wrapped,
   returning the total pixel height. With draw=1 it also renders (clipped by the
   caller). Empty lines advance by a paragraph gap. */
static int pub_body_layout(const char *text, int x, int y, int maxw, int fh,
                           Color c, int draw) {
  int py = y;
  const char *p = text;
  char para[1024];
  for (;;) {
    const char *nl = strchr(p, '\n');
    int len = nl ? (int)(nl - p) : (int)strlen(p);
    if (len > (int)sizeof(para) - 1) len = (int)sizeof(para) - 1;
    memcpy(para, p, len); para[len] = '\0';
    if (len == 0) {
      py += fh;                              /* blank line = paragraph gap */
    } else {
      int end_y = py;
      draw_wrapped(para, x, py, maxw, fh, c, &end_y, draw);
      py = end_y + fh;
    }
    if (!nl) break;
    p = nl + 1;
  }
  return py - y;
}

/* Compute the overlay geometry. Deterministic from the window size + the body
   text, so the draw pass and the mouse hit-test agree on every rect. Measures
   in FONT_REGULAR at the active body size (never changes font size — that would
   rebuild the atlases every frame). */
static void pub_layout(PubLayout *L) {
  int ww = nav_win_w(), wh = nav_win_h();
  int fh = r_get_text_height();
  const int P = 22, GAP = 16, BTN_H = 32, BTN_W = 120, BTN_GAP = 12;
  const int BODY_MAX = 400;

  int av_d = 48, av_gap = 12;

  int card_w = 560;
  if (card_w > ww - 80) card_w = ww - 80;
  if (card_w < 300)     card_w = 300;

  /* Twitter layout: the avatar is its own left column; the header row AND the
     body share the content column to its right (the text never flows under the
     avatar). */
  int content_x = P + av_d + av_gap;              /* relative to card left */
  int content_w = card_w - P - content_x;
  int name_h = fh, name_gap = 8;

  int saved = r_get_font_style();
  r_set_font_style(r_ui_font_style());
  int body_h = pub_text[0] ? pub_body_layout(pub_text, 0, 0, content_w, fh,
                                             color(0,0,0,0), 0) : fh;
  r_set_font_style(saved);
  if (body_h > BODY_MAX) body_h = BODY_MAX;

  int col_h = name_h + name_gap + body_h;         /* right-column height */
  int content_h = col_h > av_d ? col_h : av_d;    /* card must fit the avatar too */
  int card_h = P + content_h + GAP + BTN_H + P;

  int card_x = (ww - card_w) / 2;
  int card_y = (wh - card_h) / 2;
  if (card_y < 72) card_y = 72;             /* clear the title bar */

  L->card = rect(card_x, card_y, card_w, card_h);
  L->av_d = av_d;
  L->av_x = card_x + P;
  L->av_y = card_y + P;
  L->name_x = card_x + content_x;
  L->name_y = card_y + P;                          /* header top-aligned with the avatar */
  L->body = rect(card_x + content_x, card_y + P + name_h + name_gap, content_w, body_h);

  int btn_y = card_y + card_h - P - BTN_H;
  L->confirm = rect(card_x + card_w - P - BTN_W, btn_y, BTN_W, BTN_H);
  L->cancel  = rect(card_x + card_w - P - BTN_W - BTN_GAP - BTN_W, btn_y, BTN_W, BTN_H);
}

static int pt_in_rect(int x, int y, Rect r) {
  return x >= r.x && x < r.x + r.w && y >= r.y && y < r.y + r.h;
}

/* Fill a circle centered at (cx,cy) via horizontal scanlines (one r_draw_rect
   per row). Used for the avatar disc / photo clip. */
static void draw_filled_circle(int cx, int cy, int rad, Color c) {
  for (int dy = -rad; dy <= rad; dy++) {
    int dx = (int)(sqrt((double)(rad * rad - dy * dy)) + 0.5);
    r_draw_rect(rect(cx - dx, cy + dy, 2 * dx, 1), c);
  }
}

/* First letters of up to the first two words of `name`, uppercased — the
   avatar fallback when no photo is loaded. */
static void pub_initials(const char *name, char out[3]) {
  int n = 0;
  const char *p = name;
  while (*p && n < 2) {
    while (*p == ' ') p++;
    if (!*p) break;
    char c = *p;
    if (c >= 'a' && c <= 'z') c = (char)(c - 'a' + 'A');
    out[n++] = c;
    while (*p && *p != ' ') p++;
  }
  out[n] = '\0';
}

/* Faux-bold: the system UI font (SF) loads as a single regular weight via
   stb_truetype, so we thicken by over-drawing with a 1px horizontal offset. */
static void draw_ui_bold(const char *s, int x, int y, Color c) {
  r_draw_text(s, vec2(x, y), c);
  r_draw_text(s, vec2(x + 1, y), c);
}

/* Draw the tweet-preview confirmation overlay: a dimmed/blurred backdrop, a
   card with the avatar + display name + @handle + the note (clipped to 400px),
   and Confirm / Cancel buttons. Chrome text uses the native system font. */
static void draw_publish_overlay(void) {
  if (pub_state == PUB_NONE) return;
  int ww = nav_win_w(), wh = nav_win_h();
  int fh = r_get_text_height();
  int uif = r_ui_font_style();   /* native SF (or FONT_REGULAR fallback) */

  /* backdrop: blur + darken the whole editor behind the card */
  r_blur_rect(rect(0, 0, ww, wh), 6);
  r_draw_rect(rect(0, 0, ww, wh), color(0, 0, 0, 150));

  PubLayout L; pub_layout(&L);

  /* card (X dark theme: near-black with a hairline border on all four sides) */
  r_draw_rect(L.card, color(22, 24, 28, 255));
  Color hair = color(60, 63, 70, 255);
  r_draw_rect(rect(L.card.x, L.card.y, L.card.w, 1), hair);                    /* top */
  r_draw_rect(rect(L.card.x, L.card.y + L.card.h - 1, L.card.w, 1), hair);     /* bottom */
  r_draw_rect(rect(L.card.x, L.card.y, 1, L.card.h), hair);                    /* left */
  r_draw_rect(rect(L.card.x + L.card.w - 1, L.card.y, 1, L.card.h), hair);     /* right */

  /* avatar: real photo (clipped to a circle) if downloaded, else initials disc */
  int cx = L.av_x + L.av_d / 2, cy = L.av_y + L.av_d / 2, rad = L.av_d / 2;
  int aw = 0, ah = 0;
  const unsigned char *rgba = kern_x_avatar_rgba(&aw, &ah);
  if (rgba && aw > 0 && ah > 0) {
    r_draw_image_circle(rect(L.av_x, L.av_y, L.av_d, L.av_d), rgba, aw, ah);
  } else {
    draw_filled_circle(cx, cy, rad, color(64, 120, 180, 255));
    const char *dn = kern_x_display_name(); if (!dn) dn = "";
    char ini[3]; pub_initials(dn, ini);
    if (ini[0]) {
      r_set_font_style(uif);
      int iw = r_get_text_width(ini, strlen(ini));
      draw_ui_bold(ini, cx - iw / 2, cy - fh / 2, color(255, 255, 255, 255));
    }
  }

  /* one identity row: display name (bold) then "@handle · <date>" (dim), just
     like X — the middot separates the handle from the post date. All chrome
     text draws in the native system font. */
  const char *name = kern_x_display_name(); if (!name) name = "";
  const char *handle = kern_x_handle(); if (!handle) handle = "";
  char meta[160];
  {
    time_t t = time(NULL); struct tm lt; localtime_r(&t, &lt);
    char mon[8]; strftime(mon, sizeof mon, "%b", &lt);
    snprintf(meta, sizeof meta, "@%s \xC2\xB7 %s %d", handle, mon, lt.tm_mday);
  }
  int saved = r_get_font_style();
  r_set_font_style(uif);
  draw_ui_bold(name, L.name_x, L.name_y, color(231, 233, 234, 255));
  int nw = r_get_text_width(name, strlen(name)) + 1;   /* +1 for the faux-bold overdraw */
  r_draw_text(meta, vec2(L.name_x + nw + 10, L.name_y), color(113, 118, 123, 255));

  /* body: the note, wrapped, clipped to the (max 400px) body region */
  r_set_clip_rect(L.body);
  pub_body_layout(pub_text, L.body.x, L.body.y, L.body.w, fh,
                  color(231, 233, 234, 255), 1);
  r_set_clip_rect(rect(0, 0, ww, wh));

  /* buttons: native macOS look — rounded pills, regular-weight labels. Cancel is
     a gray push button (matching the title-bar Publish button); Confirm is the
     accent-blue default button. While sending it reads "Posting…" and dims. */
  int sending = (pub_state == PUB_SENDING);
  const int RAD = 6;
  /* Cancel */
  r_draw_round_rect(L.cancel, RAD, color(94, 96, 102, 255));
  const char *cl = "Cancel";
  int clw = r_get_text_width(cl, strlen(cl));
  r_draw_text(cl, vec2(L.cancel.x + (L.cancel.w - clw) / 2, L.cancel.y + (L.cancel.h - fh) / 2),
              color(240, 240, 242, 255));
  /* Confirm (default button) */
  Color accent = sending ? color(40, 74, 120, 255) : color(0, 122, 255, 255);   /* macOS blue */
  r_draw_round_rect(L.confirm, RAD, accent);
  const char *cf = sending ? "Posting\xE2\x80\xA6" : "Confirm";
  int cfw = r_get_text_width(cf, strlen(cf));
  r_draw_text(cf, vec2(L.confirm.x + (L.confirm.w - cfw) / 2, L.confirm.y + (L.confirm.h - fh) / 2),
              color(255, 255, 255, 255));
  r_set_font_style(saved);
}

/* Mouse hit-test for the overlay buttons (called from the mouse handler when
   the overlay is open). Returns 1 if the click was consumed. */
static int pub_handle_click(int mx, int my) {
  if (pub_state == PUB_NONE) return 0;
  PubLayout L; pub_layout(&L);
  if (pt_in_rect(mx, my, L.cancel))  { pub_cancel();  return 1; }
  if (pt_in_rect(mx, my, L.confirm)) { pub_confirm(); return 1; }
  /* clicks anywhere else (incl. the dimmed backdrop) are swallowed while modal */
  return 1;
}

static void do_render(void) {
  r_clear(color(30, 30, 32, 255));
  /* Set substitution BEFORE process_frame: it draws the selection/search
     highlights via md_col_x, which must measure the same collapsed/revealed glyph
     widths the text pass draws — otherwise the highlight (literal width) and the
     text (collapsed) disagree and the marked region looks wonky around symbols.
     process_frame sets the per-line reveal range itself, per visual row. */
  sub_set_mask(g_vs.sub_mask);
  process_frame();          /* lays out + draws bg, highlights, scrollbar */
  wikilink_refresh();

  /* draw markdown-formatted text */
  r_set_font_size(g_vs.font_size);
  r_set_font_style(FONT_REGULAR);
  r_set_clip_rect(rect(0, g_vs.content_y, nav_win_w(), g_vs.content_h));
  Color text_color = color(204, 200, 195, 255);
  md_set_syntax_mask(g_vs.syntax_mask);   /* POS coloring (0 = off) for this pass */
  md_set_style_mask(g_vs.style_mask);     /* style-check strikes (0 = off) */
  /* sub_set_mask already set before process_frame (see do_render top) */
  g_vs.cursor_x = -1;
  for (int i = 0; i < g_vs.vis_row_count; i++) {
    VisRow *vr = &g_vs.vis_rows[i];
    Line *L = &g_ed.lines[vr->ln];
    nav_sub_reveal_for_line(&g_ed, vr->ln);   /* reveal caret/selection tokens on this row */
    /* typewriter focus: fade every line except the caret's, crossfading on a
       line change (focus_prev_line → dim, focus_cur_line → full) */
    md_set_text_opacity(g_vs.typewriter_mode
      ? md_focus_opacity(vr->ln, g_vs.focus_cur_line, g_vs.focus_prev_line, g_vs.focus_t)
      : 1.0f);
    /* list hanging indent: continuation rows hang under the item text */
    int indent = md_row_indent(L, vr->row_start);
    /* track cursor if it's on this row */
    int track = is_caret_row(i) ? g_ed.cursor_col : -1;

    /* Divider between the editable page and the read-only Context section: a
       light dotted rule with a centered "Context" label, drawn on the first
       section line (readonly_from), which is also where the caret first turns
       amber. */
    if (g_ed.readonly_from > 0 && vr->ln == g_ed.readonly_from && vr->row_start == 0)
      draw_context_divider(vr);

    int draw_start = vr->row_start;
    if (vr->heading && vr->row_start == 0) {
      int prefix = md_heading_prefix_len(L);
      /* reveal the markers inline when the caret is at the line start so they
         can be edited/removed; otherwise hang them in the left margin */
      int reveal = (vr->ln == g_ed.cursor_line && g_ed.cursor_col <= prefix);
      /* hang markers in the margin only when there's room; otherwise let the
         "## " render inline (draw_start stays at row_start) so it never
         overlaps the text in a narrow window */
      if (!reveal && nav_heading_markers_hang(L)) {
        int hcount = prefix - 1;
        if (hcount > 23) hcount = 23;
        char hashes[24];
        memset(hashes, '#', hcount); hashes[hcount] = '\0';
        r_set_font_style(FONT_BOLD);
        int hw = r_get_text_width(hashes, hcount);
        int gap = r_get_text_width(" ", 1);
        int hx = nav_page_margin() + indent - gap - hw - (int)g_vs.scroll_x;   /* right-aligned in the left margin */
        r_draw_text(hashes, vec2(hx, vr->py), color(110, 110, 115, 255));
        r_set_font_style(FONT_REGULAR);
        if (prefix <= vr->row_end) draw_start = prefix;
      }
    }

    md_draw_text(L, draw_start, vr->row_end,
                 nav_page_margin() + indent - (int)g_vs.scroll_x, vr->py, text_color, vr->heading, track,
                 &g_vs.cursor_x, 1);
    r_set_font_style(FONT_REGULAR);
  }
  md_set_text_opacity(1.0f);   /* don't leak the focus dim past the text pass */
  md_set_syntax_mask(0);       /* status bar etc. draw in their own colors */
  md_set_style_mask(0);
  sub_set_mask(0);             /* status bar / chrome draw literal text */

  /* page furniture + margin notes. Typewriter mode always draws them (the page
     slides to make room); normal mode draws them only when the window margin is
     wide enough to hold the note strip — otherwise notes stay as bottom-of-page
     footnotes (the plain markdown view). */
  int show_furniture = !g_vs.page_furniture_hidden;   /* C-x p hides borders/gutters */
  if (g_vs.typewriter_mode) {
    if (show_furniture) draw_page_furniture();   /* furniture + notes BEFORE the fog so they frost with the page */
    draw_margin_notes();
    draw_typewriter_fog();      /* frosted guards + hitzone */
    draw_margin_note_input();   /* live input AFTER the fog stays sharp in the hitzone */
  } else if (nav_page_margin() >= tv_margin_pad()) {
    if (show_furniture) draw_page_furniture();
    draw_margin_notes();
    draw_margin_note_input();
  }

  /* draw cursor (post-render, uses markdown-aware x position). In typewriter mode
     a caret floating past EOL is shifted right by its virtual columns so it holds
     the strike point over the blank page. */
  int cursor_py = -1;
  if (g_vs.cursor_x >= 0 && !mn_active) {   /* while writing a margin note the only caret is in the margin */
    int font_h = r_get_text_height();
    int caret_x = g_vs.cursor_x + tv_virtual_px();
    /* find the py for the cursor row */
    for (int i = 0; i < g_vs.vis_row_count; i++) {
      VisRow *vr = &g_vs.vis_rows[i];
      if (is_caret_row(i)) {
        /* The caret turns warm amber — the complement of its default cyan — when
           it's inside the read-only Context section, signalling that text there
           can't be edited (only navigated / link-followed). */
        Color caret_col = (g_ed.readonly_from > 0 && g_ed.cursor_line >= g_ed.readonly_from)
                            ? color(250, 165, 70, 255)
                            : color(90, 200, 250, 255);
        r_draw_rect(rect(caret_x, vr->py, 3, font_h), caret_col);
        cursor_py = vr->py;
        break;
      }
    }
  }

  /* wikilink autocomplete dropdown, anchored under the "[[" query */
  if (wl_active && wl_count > 0 && g_vs.cursor_x >= 0 && cursor_py >= 0)
    draw_wikilink_dropdown(cursor_py);

  draw_status_bar();

  draw_publish_overlay();   /* the X-publish confirmation, on top of everything */

  r_present();
}

#ifndef KERN_HEADLESS_TEST
static int resize_event_watcher(void *data, SDL_Event *event) {
  (void)data;
  if (event->type == SDL_WINDOWEVENT &&
      (event->window.event == SDL_WINDOWEVENT_RESIZED ||
       event->window.event == SDL_WINDOWEVENT_SIZE_CHANGED)) {
    /* just update GL viewport so the clear color fills correctly, but don't reflow */
    r_handle_resize();
    r_clear(color(30, 30, 32, 255));
    r_present();
  }
  return 0;
}
#endif /* KERN_HEADLESS_TEST */

/* Called from Swift (via the bridging header) before editor_main to point
   file I/O at the app's sandbox-container Documents directory. */
void editor_set_documents_dir(const char *path) {
  buf_set_documents_dir(path);
}

/* Open (or create) today's daily note "YYYY-MM-DD.md" in the documents folder.
   A brand-new note is seeded with a date heading and written to disk. */
static void load_daily_note(void) {
  time_t t = time(NULL);
  struct tm lt;
  localtime_r(&t, &lt);

  char fname[32];
  strftime(fname, sizeof(fname), "%Y-%m-%d.md", &lt);
  buf_resolve_path(fname, g_ed.filepath, sizeof(g_ed.filepath));
  g_ed.filename = path_base(g_ed.filepath);

  if (buf_load_file(&g_ed, g_ed.filepath) != 0) {
    /* new note: seed a date heading + blank line, then create it on disk */
    buf_init_empty(&g_ed);
    char heading[64];
    strftime(heading, sizeof(heading), "# %A, %B %d, %Y", &lt);
    buf_insert_line_at(&g_ed, 0, heading, (int)strlen(heading));
    g_ed.cursor_line = 1; g_ed.cursor_col = 0; g_ed.cursor_target_col = 0;
    buf_save(&g_ed, g_ed.filepath);
  }
}

/* Cmd-Shift-T: jump to today's daily note. Saves the current buffer first, then
   loads (or seeds) today's note and refreshes the view like any buffer switch. */
static void cmd_open_daily_note(void) {
  save_if_dirty();
  filepos_remember_current();   /* remember where we were before jumping away */
  char prev[1024];              /* the file we're leaving, for the "Opened after" list */
  snprintf(prev, sizeof(prev), "%s", g_ed.filepath);
  load_daily_note();
  opened_after_record(g_ed.filepath, prev);   /* today's note was opened after prev */
  g_vs.scroll_y = g_vs.typewriter_mode ? 0.0f : -nav_top_margin(&g_vs);
  context_refresh();            /* append the read-only Context section */
  buf_invalidate_all_wraps(&g_ed);
  filepos_restore_current();    /* an existing daily note reopens where we left it */
  nav_ensure_cursor_visible(&g_ed, &g_vs);
  r_set_title(g_ed.filename);
  recent_push(g_ed.filepath);
  char msg[256];
  snprintf(msg, sizeof(msg), "Opened %s", g_ed.filename);
  nav_status_set(&g_vs, msg);
}

/* autosave + X-titlebar-sync timers, file-scoped so tv_test_reset() can clear
   them between headless tests (behaviour-neutral for the app). */
static Uint32 g_last_autosave = 0;
static int    g_last_x_conn = -1;
static void session_save(void);   /* defined below editor_handle_event */

/* ---- pumpable main-loop pieces (see editor_loop.h) ---- */

void editor_handle_event(const SDL_Event *ev) {
  SDL_Event e = *ev;
  switch (e.type) {
    case SDL_QUIT:
      save_if_dirty();
      session_save();
      exit(EXIT_SUCCESS); break;
    case SDL_WINDOWEVENT:
      if (e.window.event == SDL_WINDOWEVENT_RESIZED ||
          e.window.event == SDL_WINDOWEVENT_SIZE_CHANGED) {
        r_handle_resize();
        /* reflow only if the page width actually changed (a height-only
           resize or a spurious resize event is now free) */
        nav_maybe_reflow(&g_ed, &g_vs);
      }
      break;
    case SDL_MOUSEMOTION: g_mouse_x = e.motion.x; g_mouse_y = e.motion.y; break;
    case SDL_MOUSEWHEEL:
      g_vs.scroll_y -= e.wheel.y * nav_line_height() * 3;
      g_vs.scroll_target_y = g_vs.scroll_y;   /* don't let the ease fight the wheel */
      break;

    case SDL_TEXTINPUT:
      if (g_vs.suppress_next_text) { g_vs.suppress_next_text = 0; break; }
      if (SDL_GetModState() & (KMOD_CTRL | KMOD_GUI | KMOD_ALT)) break;
      if (pub_state != PUB_NONE) break;      /* overlay swallows typing */
      if (mn_active) {                       /* typing a margin note */
        int tlen = strlen(e.text.text);
        if (mn_len + tlen < (int)sizeof(mn_text) - 1) {
          memcpy(mn_text + mn_len, e.text.text, tlen);
          mn_len += tlen;
          mn_text[mn_len] = '\0';
        }
        break;
      }
      if (g_vs.minibuf_active) {
        int tlen = strlen(e.text.text);
        if (g_vs.minibuf_len + tlen < (int)sizeof(g_vs.minibuf_text) - 1) {
          memcpy(g_vs.minibuf_text + g_vs.minibuf_len, e.text.text, tlen);
          g_vs.minibuf_len += tlen;
          g_vs.minibuf_text[g_vs.minibuf_len] = '\0';
        }
        minibuf_refresh_completion();
        if (bufsw_active) bufsw_filter();
      } else if (g_vs.search_active) {
        int tlen = strlen(e.text.text);
        if (g_vs.search_len + tlen < (int)sizeof(g_vs.search_buf) - 1) {
          memcpy(g_vs.search_buf + g_vs.search_len, e.text.text, tlen);
          g_vs.search_len += tlen;
          g_vs.search_buf[g_vs.search_len] = '\0';
          nav_search_find_current_dir(&g_ed, &g_vs);
        }
      } else {
        /* a literal Tab is indentation (handled on keydown), never inserted */
        if (e.text.text[0] == '\t' && e.text.text[1] == '\0') break;
        /* With a region marked, a markdown emphasis/code char surrounds the
           region instead of being inserted at the caret; the region stays
           active so typing '*' twice turns a selection into **bold**. */
        int is_wrap_char = g_ed.mark_active && e.text.text[1] == '\0' &&
          (e.text.text[0] == '*' || e.text.text[0] == '_' ||
           e.text.text[0] == '`' || e.text.text[0] == '=' ||
           e.text.text[0] == '+');   /* '+' twice → ++underline++ */
        if (is_wrap_char && ed_wrap_region(&g_ed, e.text.text, e.text.text)) {
          nav_ensure_cursor_visible(&g_ed, &g_vs);
          break;
        }
        /* typewriter carriage: when the caret is floating past EOL after a
           vertical move, pad the gap with spaces so the typed char lands under
           the strike point (one insert, so it undoes as a unit). */
        const char *ins = e.text.text;
        char padded[128];
        int vcols = tv_virtual_cols();
        if (vcols > 0) {
          if (vcols > (int)sizeof(padded) - 8) vcols = (int)sizeof(padded) - 8;
          memset(padded, ' ', vcols);
          snprintf(padded + vcols, sizeof(padded) - vcols, "%s", e.text.text);
          ins = padded;
        }
        /* typewriter hard right margin: at the writable edge, block further
           typing (the line no longer wraps onto a new visual row) — only Enter
           starts a new line. */
        if (g_vs.typewriter_mode && nav_at_right_margin(&g_ed, ins)) break;
        ed_insert_char(&g_ed, ins);
        nav_ensure_cursor_visible(&g_ed, &g_vs);
      }
      break;

    case SDL_MOUSEBUTTONDOWN:
    case SDL_MOUSEBUTTONUP: {
      int b = button_map[e.button.button & 0xff];
      if (b && e.type == SDL_MOUSEBUTTONDOWN) {
        g_mouse_x = e.button.x; g_mouse_y = e.button.y;
        g_mouse_down |= b; g_mouse_pressed |= b;
        /* the publish overlay is modal: it consumes left clicks (buttons + the
           dimmed backdrop) before they can reach the text under it */
        if (b == MOUSE_LEFT && pub_handle_click(e.button.x, e.button.y)) break;
        if (b == MOUSE_LEFT && e.button.y > TOP_PADDING && e.button.x < nav_win_w() - 12) {
          nav_click_to_cursor(&g_ed, &g_vs, e.button.x, e.button.y);
        }
      }
      if (b && e.type == SDL_MOUSEBUTTONUP) { g_mouse_down &= ~b; }
      break;
    }

    case SDL_KEYDOWN:
    case SDL_KEYUP: {
      /* suppress macOS system beep for ctrl combos */
      if (e.type == SDL_KEYDOWN && (e.key.keysym.mod & KMOD_CTRL)) {
        SDL_StopTextInput();
      }
      if (e.type == SDL_KEYUP && !(SDL_GetModState() & KMOD_CTRL)) {
        SDL_StartTextInput();
      }

      if (e.type == SDL_KEYDOWN) {
        int ctrl = !!(e.key.keysym.mod & KMOD_CTRL);
        int sym = e.key.keysym.sym;

        /* 1. Modal handlers (consume and break) */
        if (pub_state != PUB_NONE && handle_publish_key(sym)) break;
        if (mn_active && handle_marginnote_key(sym)) break;
        if (g_vs.minibuf_active && handle_minibuf_key(sym, ctrl)) break;
        if (g_vs.search_active && handle_search_key(sym, ctrl)) break;
        if (g_vs.ctrl_x_prefix && handle_cx_prefix_key(sym, ctrl)) break;
        if (g_vs.esc_prefix && handle_esc_prefix_key(sym, !!(e.key.keysym.mod & KMOD_SHIFT))) break;

        /* 1b. Wikilink autocomplete dropdown (Enter/Tab accept, Up/Down,
           Esc dismiss) — only intercepts when the dropdown is showing */
        if (wl_active && handle_wikilink_key(sym, ctrl)) break;

        /* 1c. Wikilink navigation: Cmd-Enter follows the link under the
           cursor; Cmd-Shift-Left/Right go back/forward through history */
        if ((e.key.keysym.mod & KMOD_GUI) && sym == SDLK_RETURN) {
          cmd_follow_wikilink(); break;
        }
        if ((e.key.keysym.mod & KMOD_GUI) && (e.key.keysym.mod & KMOD_SHIFT) &&
            sym == SDLK_LEFT) {
          cmd_nav_back(); break;
        }
        if ((e.key.keysym.mod & KMOD_GUI) && (e.key.keysym.mod & KMOD_SHIFT) &&
            sym == SDLK_RIGHT) {
          cmd_nav_forward(); break;
        }
        /* Cmd-Shift-N: extract the marked region into a new linked note */
        if ((e.key.keysym.mod & KMOD_GUI) && (e.key.keysym.mod & KMOD_SHIFT) &&
            sym == SDLK_n) {
          cmd_extract_region_to_note(); break;
        }
        /* Cmd-Shift-M: margin note (footnote) for the marked region */
        if ((e.key.keysym.mod & KMOD_GUI) && (e.key.keysym.mod & KMOD_SHIFT) &&
            sym == SDLK_m) {
          cmd_margin_note(); break;
        }
        /* Cmd-Shift-H: toggle ==highlight== around the caret's sentence */
        if ((e.key.keysym.mod & KMOD_GUI) && (e.key.keysym.mod & KMOD_SHIFT) &&
            sym == SDLK_h) {
          if (ed_toggle_sentence_highlight(&g_ed)) {
            nav_ensure_cursor_visible(&g_ed, &g_vs);
            nav_status_set(&g_vs, "Toggled sentence highlight");
          }
          break;
        }
        /* Cmd-Shift-U: toggle ++underline++ around the caret's sentence */
        if ((e.key.keysym.mod & KMOD_GUI) && (e.key.keysym.mod & KMOD_SHIFT) &&
            sym == SDLK_u) {
          if (ed_toggle_sentence_underline(&g_ed)) {
            nav_ensure_cursor_visible(&g_ed, &g_vs);
            nav_status_set(&g_vs, "Toggled sentence underline");
          }
          break;
        }
        /* Cmd-Shift-T: open (or create) today's daily note */
        if ((e.key.keysym.mod & KMOD_GUI) && (e.key.keysym.mod & KMOD_SHIFT) &&
            sym == SDLK_t) {
          cmd_open_daily_note(); break;
        }

        /* 1d. Tab / Shift-Tab indent or outdent the current list item.
           The matching '\t' text event is dropped in SDL_TEXTINPUT. */
        if (sym == SDLK_TAB && md_is_list_item(&g_ed.lines[g_ed.cursor_line])) {
          if (e.key.keysym.mod & KMOD_SHIFT) ed_dedent_line(&g_ed);
          else                               ed_indent_line(&g_ed);
          nav_ensure_cursor_visible(&g_ed, &g_vs);
          break;
        }

        /* 2. Prefix starters */
        if (ctrl && sym == SDLK_x) { g_vs.ctrl_x_prefix = 1; break; }
        if (sym == SDLK_ESCAPE && !g_vs.search_active) {
          if (g_ed.mark_active) { buf_mark_clear(&g_ed); nav_status_set(&g_vs, "Quit"); }
          else { g_vs.esc_prefix = 1; SDL_StopTextInput(); }
          break;
        }

        /* 3. C-s / C-r isearch start/continue */
        if (ctrl && sym == SDLK_s) {
          g_vs.search_direction = 1;
          if (!g_vs.search_active) {
            g_vs.search_active = 1;
            g_vs.search_buf[0] = '\0';
            g_vs.search_len = 0;
            g_vs.search_match_line = -1;
          } else {
            nav_search_find_next(&g_ed, &g_vs, g_vs.search_match_line >= 0 ? g_vs.search_match_line : g_ed.cursor_line,
                             g_vs.search_match_col >= 0 ? g_vs.search_match_col : g_ed.cursor_col - 1);
          }
          break;
        }
        if (ctrl && sym == SDLK_r) {
          g_vs.search_direction = -1;
          if (!g_vs.search_active) {
            g_vs.search_active = 1;
            g_vs.search_buf[0] = '\0';
            g_vs.search_len = 0;
            g_vs.search_match_line = -1;
          } else {
            nav_search_find_prev(&g_ed, &g_vs, g_vs.search_match_line >= 0 ? g_vs.search_match_line : g_ed.cursor_line,
                             g_vs.search_match_col >= 0 ? g_vs.search_match_col : g_ed.cursor_col + 1);
          }
          break;
        }

        /* any non-C-k key clears last_kill_was_k */
        if (!(ctrl && sym == SDLK_k)) g_ed.last_kill_was_k = 0;

        /* 4. De-globalized command table (commands.c). */
        if (kern_dispatch_key(&g_ed, &g_vs, e.key.keysym.mod, sym)) break;

        /* 4b. M-g goto-line — lives here (not the commands.c table) because
           it drives the textview-local minibuffer. */
        if ((e.key.keysym.mod & KMOD_ALT) && sym == SDLK_g) {
          cmd_goto_line(); break;
        }

        /* 5. {Alt,Cmd}+Shift+. / +, → end/beginning of buffer
           (need shift check, not in table) */
        {
          int alt = !!(e.key.keysym.mod & KMOD_ALT);
          int cmd = !!(e.key.keysym.mod & KMOD_GUI);
          int shift = !!(e.key.keysym.mod & KMOD_SHIFT);
          if ((alt || cmd) && shift && sym == SDLK_PERIOD) {
            cmd_end_of_buffer_alt(&g_ed, &g_vs);
            /* Option+key can emit a stray text glyph that races past the
               modifier guard, so swallow it; Cmd+key emits no text, and
               suppressing would eat the user's next real keystroke. */
            if (alt) g_vs.suppress_next_text = 1;
            break;
          }
          if ((alt || cmd) && shift && sym == SDLK_COMMA) {
            cmd_beginning_of_buffer_alt(&g_ed, &g_vs);
            if (alt) g_vs.suppress_next_text = 1;
            break;
          }
        }

        /* 6. Arrow keys — delegate the actual movement to the commands.c
           functions; here we only add shift-to-select and ctrl/alt
           word-jump on top. */
        if (sym == SDLK_LEFT || sym == SDLK_RIGHT ||
            sym == SDLK_UP || sym == SDLK_DOWN) {
          int alt = !!(e.key.keysym.mod & KMOD_ALT);
          int shift = !!(e.key.keysym.mod & KMOD_SHIFT);
          if (shift && !g_ed.mark_active) buf_mark_set(&g_ed);
          switch (sym) {
            case SDLK_LEFT:
              if (ctrl || alt) cmd_backward_word(&g_ed, &g_vs);
              else if (shift)  cmd_backward_char(&g_ed, &g_vs);   /* selecting: real text only */
              else             tv_horizontal_move(-1);            /* carriage: float-aware */
              break;
            case SDLK_RIGHT:
              if (ctrl || alt) cmd_forward_word(&g_ed, &g_vs);
              else if (shift)  cmd_forward_char(&g_ed, &g_vs);
              else             tv_horizontal_move(+1);
              break;
            case SDLK_UP:   cmd_previous_line(&g_ed, &g_vs); break;
            case SDLK_DOWN: cmd_next_line(&g_ed, &g_vs);     break;
          }
          break;
        }
      }
      break;
    }
  }
}

/* ---- per-file cursor memory: remember the caret position in each file so
   reopening it later (wikilink, buffer switch, next launch) drops you back where
   you were. Persisted to ".kern_positions" alongside the session. ---- */
#define FILEPOS_MAX 256
typedef struct { char path[1024]; int line, col; } FilePos;
static FilePos g_filepos[FILEPOS_MAX];
static int     g_filepos_count;

static void filepos_remember(const char *path, int line, int col) {
  if (!path || !path[0]) return;
  for (int i = 0; i < g_filepos_count; i++)
    if (strcmp(g_filepos[i].path, path) == 0) { g_filepos[i].line = line; g_filepos[i].col = col; return; }
  int i = g_filepos_count < FILEPOS_MAX ? g_filepos_count++ : FILEPOS_MAX - 1;
  snprintf(g_filepos[i].path, sizeof(g_filepos[i].path), "%s", path);
  g_filepos[i].line = line; g_filepos[i].col = col;
}
static int filepos_lookup(const char *path, int *line, int *col) {
  if (!path) return 0;
  for (int i = 0; i < g_filepos_count; i++)
    if (strcmp(g_filepos[i].path, path) == 0) { *line = g_filepos[i].line; *col = g_filepos[i].col; return 1; }
  return 0;
}
static void filepos_remember_current(void) {
  filepos_remember(g_ed.filepath, g_ed.cursor_line, g_ed.cursor_col);
}
/* Move the caret to the current file's remembered position (clamped), if any. */
static void filepos_restore_current(void) {
  int ln, col;
  if (filepos_lookup(g_ed.filepath, &ln, &col)) {
    g_ed.cursor_line = ln; g_ed.cursor_col = col;
    nav_cursor_clamp(&g_ed);
    g_ed.cursor_target_col = g_ed.cursor_col;
  }
}
static void filepos_save(void) {
#ifndef KERN_HEADLESS_TEST
  if (!buf_get_documents_dir()[0]) return;
  char path[1024];
  buf_resolve_path(".kern_positions", path, sizeof(path));
  FILE *f = fopen(path, "wb");
  if (!f) return;
  for (int i = 0; i < g_filepos_count; i++)   /* path last: it may contain spaces */
    fprintf(f, "%d %d %s\n", g_filepos[i].line, g_filepos[i].col, g_filepos[i].path);
  fclose(f);
#endif
}

/* ---- non-blocking autosave: the disk write runs on a background thread so a
   large save doesn't stall typing. The buffer is snapshotted on the main thread
   (fast memcpy) and handed to the worker, which owns and frees it. ---- */
#ifndef KERN_HEADLESS_TEST
typedef struct { char path[1024]; char *text; int len; } SaveJob;
static volatile int g_saving;   /* 1 while a background save is in flight */
static void *save_worker(void *arg) {
  SaveJob *job = arg;
  buf_save_text(job->path, job->text, job->len);
  free(job->text); free(job);
  g_saving = 0;
  return NULL;
}
#endif

static void autosave_async(void) {
  if (!g_ed.filepath[0]) return;
#ifdef KERN_HEADLESS_TEST
  buf_save(&g_ed, g_ed.filepath);          /* synchronous + deterministic for tests */
#else
  if (g_saving) return;                    /* a previous save still running; retry next tick */
  int off = 0;
  char *text = buffer_dup_all(&g_ed, &off);   /* note text, Context section excluded */
  if (!text) { buf_save(&g_ed, g_ed.filepath); return; }   /* OOM: fall back to sync */
  SaveJob *job = malloc(sizeof *job);
  if (!job) { free(text); buf_save(&g_ed, g_ed.filepath); return; }
  snprintf(job->path, sizeof job->path, "%s", g_ed.filepath);
  job->text = text; job->len = off;
  g_ed.dirty = 0;                          /* optimistic; a later edit re-dirties */
  g_saving = 1;
  pthread_t th;
  if (pthread_create(&th, NULL, save_worker, job) != 0) {   /* couldn't spawn → sync */
    g_saving = 0;
    buf_save_text(job->path, job->text, job->len);
    free(job->text); free(job);
  } else {
    pthread_detach(th);
  }
#endif
}

/* Persist the session — open file, cursor, and view toggles — to ".kern_session"
   in the documents dir, so a relaunch can resume where we left off. App-only
   (no-op headless); written on each autosave tick and on quit. */
static void session_save(void) {
#ifndef KERN_HEADLESS_TEST
  if (!buf_get_documents_dir()[0]) return;          /* only when sandboxed */
  if (!g_ed.filepath[0]) return;                    /* nothing meaningful yet */
  char path[1024];
  buf_resolve_path(".kern_session", path, sizeof(path));
  char out[2048];
  int n = snprintf(out, sizeof(out),
    "file %s\ncursor %d %d\ntypewriter %d\nsyntax %u\nstyle %u\nnoborders %d\n",
    g_ed.filepath, g_ed.cursor_line, g_ed.cursor_col,
    g_vs.typewriter_mode, g_vs.syntax_mask, g_vs.style_mask,
    g_vs.page_furniture_hidden);
  if (n > 0) buf_save_text(path, out, n);
  filepos_remember_current();   /* keep the open file's position fresh, then persist */
  filepos_save();
  opened_after_save();          /* persist the "Opened after" history too */
#endif
}

void editor_tick(void) {
  /* periodic auto-save: write the current file if it changed since the last
     save. Cheap (just a dirty flag); skipped for the unsaved *scratch*
     buffer (no path). */
  {
    const Uint32 AUTOSAVE_INTERVAL_MS = 3000;   /* "every X seconds" */
    Uint32 now = kern_now_ms();
    if (g_last_autosave == 0) g_last_autosave = now;
    if (now - g_last_autosave >= AUTOSAVE_INTERVAL_MS) {
      g_last_autosave = now;
      if (g_ed.dirty && g_ed.filepath[0]) {
        autosave_async();               /* write off the main thread — no typing hitch */
        nav_status_set(&g_vs, "Auto-saved");
      }
      session_save();   /* remember file + cursor + toggles for next launch */
    }
  }

  /* keep the title-bar "Publish to X" button in sync with the connection
     state (the user may link/unlink X via Settings while running). Cheap
     in-memory check; only touches AppKit when the state actually flips. */
  {
    int c = kern_x_is_connected();
    if (c != g_last_x_conn) { g_last_x_conn = c; kern_titlebar_set_x_connected(c); }
  }

  pub_apply_result();   /* apply a pending X-publish result on the main thread */

  do_render();
}

#ifndef KERN_HEADLESS_TEST
/* ---- launch: resume the last session or start today's daily note ---- */

typedef struct {
  char file[1024];
  int  cursor_line, cursor_col;
  int  typewriter;
  unsigned int syntax_mask, style_mask;
  int  page_furniture_hidden;
  int  loaded;
} SessionState;

static SessionState session_load(void) {
  SessionState s; memset(&s, 0, sizeof(s));
  if (!buf_get_documents_dir()[0]) return s;
  char path[1024];
  buf_resolve_path(".kern_session", path, sizeof(path));
  FILE *f = fopen(path, "rb");
  if (!f) return s;
  char line[1200];
  while (fgets(line, sizeof(line), f)) {
    if      (strncmp(line, "file ", 5) == 0)        sscanf(line + 5, "%1023[^\n]", s.file);
    else if (strncmp(line, "cursor ", 7) == 0)      sscanf(line + 7, "%d %d", &s.cursor_line, &s.cursor_col);
    else if (strncmp(line, "typewriter ", 11) == 0) s.typewriter  = atoi(line + 11);
    else if (strncmp(line, "syntax ", 7) == 0)      s.syntax_mask = (unsigned int)strtoul(line + 7, NULL, 10);
    else if (strncmp(line, "style ", 6) == 0)       s.style_mask  = (unsigned int)strtoul(line + 6, NULL, 10);
    else if (strncmp(line, "noborders ", 10) == 0)  s.page_furniture_hidden = atoi(line + 10);
  }
  fclose(f);
  s.loaded = 1;
  return s;
}

/* Absolute path of today's daily note (whether or not it exists). */
static void today_note_path(char *out, int outsz) {
  time_t t = time(NULL);
  struct tm lt; localtime_r(&t, &lt);
  char fname[32];
  strftime(fname, sizeof(fname), "%Y-%m-%d.md", &lt);
  buf_resolve_path(fname, out, outsz);
}

static int file_exists(const char *p) {
  FILE *f = fopen(p, "rb");
  if (f) { fclose(f); return 1; }
  return 0;
}

/* Load the per-file cursor table written by filepos_save (lines "<line> <col>
   <path>", path last since it may contain spaces). */
static void filepos_load(void) {
  if (!buf_get_documents_dir()[0]) return;
  char path[1024];
  buf_resolve_path(".kern_positions", path, sizeof(path));
  FILE *f = fopen(path, "rb");
  if (!f) return;
  char line[1200]; int ln, col; char p[1024];
  while (fgets(line, sizeof(line), f))
    if (sscanf(line, "%d %d %1023[^\n]", &ln, &col, p) == 3) filepos_remember(p, ln, col);
  fclose(f);
}

/* Load the "Opened after" history written by opened_after_save (lines
   "<predecessor-basename>\t<full-path>"). */
static void opened_after_load(void) {
  if (!buf_get_documents_dir()[0]) return;
  char path[1024];
  buf_resolve_path(".kern_opened_after", path, sizeof(path));
  FILE *f = fopen(path, "rb");
  if (!f) return;
  char line[1400];
  while (fgets(line, sizeof(line), f)) {
    char *tab = strchr(line, '\t');
    if (!tab) continue;
    *tab = '\0';
    char *nl = strchr(tab + 1, '\n'); if (nl) *nl = '\0';
    opened_after_add(tab + 1, line);   /* (full path, predecessor basename) */
  }
  fclose(f);
}

/* Drop a wikilink back to `filepath` into a freshly-created daily note (below the
   date heading), then park the cursor on the blank line under it and save. */
static void daily_add_backlink(EditorState *ed, const char *filepath) {
  /* keep the basename verbatim incl. its extension — wikilink follow resolves the
     bracket text as-is (open_or_create_file appends nothing). */
  char link[400];
  snprintf(link, sizeof(link), "Previously open file: [[%s]]", path_base(filepath));
  buf_insert_line_at(ed, 1, link, (int)strlen(link));    /* line 1, just under the heading */
  ed->cursor_line = 2; ed->cursor_col = 0; ed->cursor_target_col = 0;
  buf_save(ed, ed->filepath);
}

int editor_main(int argc, char **argv) {
  SessionState ss = session_load();
  filepos_load();   /* per-file cursor table, consulted below + by open_or_create_file */
  opened_after_load();   /* "Opened after" history, surfaced in the Context section */

  if (argc >= 2 && buf_load_file(&g_ed, argv[1]) == 0) {
    snprintf(g_ed.filepath, sizeof(g_ed.filepath), "%s", argv[1]);
    g_ed.filename = path_base(argv[1]);
    printf("loaded %d lines\n", g_ed.line_count);
    filepos_restore_current();   /* drop to where we last were in this file */
  } else {
    char today[1024];
    today_note_path(today, sizeof(today));
    if (!file_exists(today)) {
      /* new day: create today's note and, if we had a file open last time, link
         back to it from the fresh note — and record it as an "Opened after"
         predecessor, since today's note was opened right after that file (the
         Context section is generated further below). */
      load_daily_note();
      if (ss.loaded && ss.file[0]) {
        daily_add_backlink(&g_ed, ss.file);
        opened_after_record(g_ed.filepath, ss.file);
      }
    } else if (ss.loaded && ss.file[0] && buf_load_file(&g_ed, ss.file) == 0) {
      /* today's note already exists → resume the last session's file, dropping to
         its remembered cursor (session cursor as a fallback) */
      snprintf(g_ed.filepath, sizeof(g_ed.filepath), "%s", ss.file);
      const char *slash = strrchr(g_ed.filepath, '/');
      g_ed.filename = slash ? slash + 1 : g_ed.filepath;
      g_ed.cursor_line = ss.cursor_line; g_ed.cursor_col = ss.cursor_col;
      nav_cursor_clamp(&g_ed);
      g_ed.cursor_target_col = g_ed.cursor_col;
      filepos_restore_current();
    } else {
      load_daily_note();   /* no usable session → just open today's note */
      filepos_restore_current();
    }
  }
  recent_push(g_ed.filepath);   /* seed the MRU with the initially-opened file */

  SDL_Init(SDL_INIT_EVERYTHING);
  g_vs.font_size = 24.0f;
  g_vs.sub_mask = SUB_MASK_ALL;   /* symbol substitution on by default */
  g_vs.search_direction = 1;
  g_vs.search_match_line = -1;
  g_vs.search_match_col = -1;
  g_vs.cursor_x = -1;
  g_vs.goal_line = -1;
  /* restore the view toggles saved last session (settle the focus crossfade on
     the restored line so it doesn't flash on the first frame) */
  if (ss.loaded) {
    g_vs.typewriter_mode = ss.typewriter;
    g_vs.page_furniture_hidden = ss.page_furniture_hidden;
    g_vs.syntax_mask     = ss.syntax_mask;
    g_vs.style_mask      = ss.style_mask;
    g_vs.focus_cur_line  = g_vs.focus_prev_line = g_ed.cursor_line;
    g_vs.focus_t = 1.0f;
  }
  if (!g_ed.filename) g_ed.filename = "*scratch*";
  r_init();
  pos_tagger_warm();   /* pay the POS model-load cost now, before the first frame */
  r_set_font_size(g_vs.font_size);
  r_set_title(g_ed.filename);
  macos_style_window(r_get_window());

  /* Scroll the restored cursor into view on the very first frame. The pin/wrap
     math needs the real content height and the mono flag set first (process_frame
     does both each frame, but it hasn't run yet) — otherwise the caret lands a
     line off. */
  md_set_force_mono(g_vs.typewriter_mode);
  {
    int status_bar_h = r_get_text_height() + 16;
    g_vs.content_y = TOP_PADDING;
    g_vs.content_h = nav_win_h() - TOP_PADDING - status_bar_h;
  }
  context_refresh();   /* append the read-only Context section to the opened file */
  nav_ensure_cursor_visible(&g_ed, &g_vs);
  if (g_vs.typewriter_mode) g_vs.scroll_y = g_vs.scroll_target_y;   /* snap, no launch glide */
  else if (g_vs.scroll_y <= 0) g_vs.scroll_y = -nav_top_margin(&g_vs);  /* show the top page margin */

  SDL_AddEventWatch(resize_event_watcher, NULL);
  for (;;) {
    SDL_Event e;
    /* Block until input arrives (NULL leaves events queued for the drain loop
       below) instead of busy-spinning. The timeout wakes us periodically so
       transient status-bar messages can still clear without input. */
    SDL_WaitEventTimeout(NULL, (g_scroll_animating || g_dim_animating || g_pos_animating) ? 16 : 250);
    while (SDL_PollEvent(&e)) editor_handle_event(&e);
    editor_tick();
  }
  return 0;
}
#endif /* KERN_HEADLESS_TEST */

#ifdef KERN_HEADLESS_TEST
/* ---- test-only seam: reach the singletons + reset all mutable state ---- */
EditorState *tv_test_ed(void) { return &g_ed; }
ViewState   *tv_test_vs(void) { return &g_vs; }

void tv_test_reset(void) {
  /* free everything g_ed owns first so LeakSanitizer stays quiet across the
     many resets a test run performs (mirrors tests/ed_fixture.h ed_teardown) */
  buf_free_all_lines(&g_ed);
  free(g_ed.lines);
  free(g_ed.kill_buf);
  for (int i = 0; i < MAX_UNDO; i++) free(g_ed.undo_stack[i].text);
  memset(&g_ed, 0, sizeof(g_ed));
  memset(&g_vs, 0, sizeof(g_vs));
  g_vs.font_size = 26.0f;
  g_vs.search_direction = 1;
  g_vs.search_match_line = -1;
  g_vs.search_match_col = -1;
  g_vs.cursor_x = -1;
  g_vs.goal_line = -1;   /* no vertical-move goal column yet */
  /* modal file-statics that persist across events */
  minibuf_completing = 0; minibuf_suggest[0] = '\0';
  bufsw_active = bufsw_listing = bufsw_sel = bufsw_count = 0;
  nav_back_count = 0; nav_fwd_count = 0;
  mn_active = mn_len = 0; mn_text[0] = '\0'; g_fn_side_count = 0;
  wl_active = wl_count = wl_sel = 0;
  wl_query[0] = wl_last_query[0] = wl_suppressed[0] = '\0';
  wl_has_suppress = 0;
  g_mouse_x = g_mouse_y = g_mouse_down = g_mouse_pressed = 0;
  g_scroll_animating = g_dim_animating = g_pos_animating = 0;
  g_wip_line = NULL; g_wip_lo = g_wip_hi = 0; g_pos_prev_seq = 0;
  pos_anim_reset();
  g_last_autosave = 0;
  g_last_x_conn = -1;
  g_opened_after_count = 0;   /* clear the "Opened after" history between tests */
  pub_state = PUB_NONE; pub_result = 0; pub_text[0] = '\0'; pub_result_info[0] = '\0';
}

/* Test accessors for the X-publish confirmation overlay. */
int         tv_test_pub_state(void) { return pub_state; }
const char *tv_test_pub_text(void)  { return pub_text; }

/* Test accessor: the recorded predecessor basenames for `path` (raw, unfiltered
   by on-disk existence — that filtering lives in the app-only opened_after_list). */
int tv_test_opened_after(const char *path, char out[][256], int max) {
  OpenedAfter *e = opened_after_find(path, 0);
  if (!e) return 0;
  int n = 0;
  for (int i = 0; i < e->npred && n < max; i++)
    snprintf(out[n++], 256, "%s", e->preds[i]);
  return n;
}
#endif /* KERN_HEADLESS_TEST */

