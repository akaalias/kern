/* We provide our own entry point (Swift @main calls editor_main), so tell SDL
   not to redefine main / expect SDL_main. */
#define SDL_MAIN_HANDLED
#include <SDL2/SDL.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
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

/* show/hide the title-bar "Publish to X" button (macos_style.m) */
extern void kern_titlebar_set_x_connected(int connected);

/* Swift calls this (possibly off the main thread) to report a publish result.
   We only stash a string + timestamp; the 250ms event-loop tick repaints it,
   so no synthetic SDL event is needed. */
void kern_x_set_status(const char *msg) { nav_status_set(&g_vs, msg); }


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

static void open_or_create_file(const char *path) {
  /* resolve the typed name to a path inside the sandbox documents dir */
  buf_resolve_path(path, g_ed.filepath, sizeof(g_ed.filepath));
  const char *slash = strrchr(g_ed.filepath, '/');
  g_ed.filename = slash ? slash + 1 : g_ed.filepath;

  buf_free_all_lines(&g_ed);
  int existed = (buf_load_file(&g_ed, g_ed.filepath) == 0);
  if (!existed) buf_init_empty(&g_ed);

  g_ed.cursor_line = 0;
  g_ed.cursor_col = 0;
  g_ed.cursor_target_col = 0;
  g_vs.scroll_y = 0;
  buf_invalidate_all_wraps(&g_ed);
  r_set_title(g_ed.filename);
  char msg[256];
  snprintf(msg, sizeof(msg), existed ? "Opened %s" : "New file %s", g_ed.filename);
  nav_status_set(&g_vs, msg);
  recent_push(g_ed.filepath);
}

static void save_to_path(const char *path) {
  buf_resolve_path(path, g_ed.filepath, sizeof(g_ed.filepath));
  const char *slash = strrchr(g_ed.filepath, '/');
  g_ed.filename = slash ? slash + 1 : g_ed.filepath;
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

static void cmd_follow_wikilink(void) {   /* Cmd-Enter */
  char target[1024];
  if (!wikilink_at_cursor(target, sizeof(target))) {
    nav_status_set(&g_vs, "No wikilink at cursor");
    return;
  }
  if (g_ed.dirty && g_ed.filepath[0]) buf_save(&g_ed, g_ed.filepath);
  nav_stack_push(nav_back, &nav_back_count, g_ed.filepath, g_ed.cursor_line, g_ed.cursor_col);
  nav_fwd_count = 0;   /* a new jump starts a fresh branch — drop forward history */
  open_or_create_file(target);
}

static void cmd_nav_back(void) {          /* Cmd-Shift-Left */
  if (nav_back_count == 0) { nav_status_set(&g_vs, "No previous note"); return; }
  if (g_ed.dirty && g_ed.filepath[0]) buf_save(&g_ed, g_ed.filepath);
  nav_stack_push(nav_fwd, &nav_fwd_count, g_ed.filepath, g_ed.cursor_line, g_ed.cursor_col);
  NavEntry e = nav_back[--nav_back_count];
  nav_goto(&e);
}

static void cmd_nav_forward(void) {       /* Cmd-Shift-Right */
  if (nav_fwd_count == 0) { nav_status_set(&g_vs, "No next note"); return; }
  if (g_ed.dirty && g_ed.filepath[0]) buf_save(&g_ed, g_ed.filepath);
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

/* Join the whole buffer into one malloc'd, NUL-terminated string, lines
   separated by '\n'. Caller frees; *len_out gets the byte length. */
static char *buffer_dup_all(EditorState *ed, int *len_out) {
  int total = 1;  /* room for the trailing NUL even on an empty buffer */
  for (int i = 0; i < ed->line_count; i++) total += ed->lines[i].len + 1;
  char *out = malloc(total);
  if (!out) { if (len_out) *len_out = 0; return NULL; }
  int p = 0;
  for (int i = 0; i < ed->line_count; i++) {
    memcpy(out + p, ed->lines[i].text, ed->lines[i].len);
    p += ed->lines[i].len;
    if (i + 1 < ed->line_count) out[p++] = '\n';
  }
  out[p] = '\0';
  if (len_out) *len_out = p;
  return out;
}

/* Publish the current note to X (triggered by the title-bar button). If a region
   is marked, only that is posted; otherwise the whole note goes. The text is
   handed to the Swift layer, which owns OAuth, the HTTP call, and reporting back
   via kern_x_set_status(). */
static void cmd_publish_to_x(void) {
  if (!kern_x_is_connected()) {
    nav_status_set(&g_vs, "Connect your X account first (Settings \xE2\x80\xBA X)");
    return;
  }

  int len = 0;
  char *text = g_ed.mark_active ? ed_region_dup(&g_ed, &len)
                                : buffer_dup_all(&g_ed, &len);
  if (!text || len == 0) { free(text); nav_status_set(&g_vs, "Nothing to publish"); return; }

  nav_status_set(&g_vs, "Publishing to X\xE2\x80\xA6");
  kern_x_publish(text);   /* async; the result arrives via kern_x_set_status */
  free(text);
}

/* Called from the title-bar "Publish to X" button (macos_style.m, main thread). */
void kern_publish_to_x(void) { cmd_publish_to_x(); }

/* word wrapping, cursor navigation, editing, and markdown rendering
   now in navigation.c, editing.c, md_render.c (accessed via shims above) */

/* ---- undo is now in undo.c (operation-based) ---- */

/* True if a heading's "### " markers fit in the left margin (so they can hang
   there). False in a narrow window, where they'd overlap the text and should
   render inline instead. */
static int heading_markers_hang(Line *l) {
  if (!md_is_heading(l)) return 0;
  int hcount = md_heading_prefix_len(l) - 1;
  if (hcount > 23) hcount = 23;
  char hashes[24];
  memset(hashes, '#', hcount); hashes[hcount] = '\0';
  int saved = r_get_font_style();
  r_set_font_style(FONT_BOLD);
  int hw = r_get_text_width(hashes, hcount);
  int gap = r_get_text_width(" ", 1);
  r_set_font_style(saved);
  return (nav_page_margin() - gap - hw) >= 2;   /* headings carry no list indent */
}

/* ---- frame ---- */

/* mouse state, tracked from SDL events; only the scrollbar consumes it.
   g_mouse_pressed is the press edge for this frame. */
static int g_mouse_x, g_mouse_y, g_mouse_down, g_mouse_pressed;

/* Set by process_frame while scroll_y is gliding toward scroll_target_y (g_scroll_animating)
   or a focus crossfade is in progress (g_dim_animating); either makes the event loop
   poll faster (≈60fps) so the animation runs. */
static int g_scroll_animating;
static int g_dim_animating;

/* Lay out and immediately draw the frame's chrome: window background, content
   clip, selection/search highlights and the scrollbar. The markdown text is
   drawn afterward in do_render. */
static void process_frame(void) {
  g_vs.vis_row_count = 0;

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

    /* content area */
    int total_vis = nav_total_visual_lines(&g_ed);
    float max_scroll = (total_vis * lh) - g_vs.content_h;
    /* typewriter mode adds virtual whitespace below the last line so it can
       still pin at the golden line near EOF (matches iA Writer). */
    if (g_vs.typewriter_mode)
      max_scroll += (int)((1.0f - TYPEWRITER_FRACTION) * (g_vs.content_h - lh));
    if (max_scroll < 0) max_scroll = 0;
    /* typewriter mode also adds virtual whitespace ABOVE the first line so it,
       too, can pin at the golden height (negative scroll). */
    float min_scroll = 0;
    if (g_vs.typewriter_mode)
      min_scroll = -(int)(TYPEWRITER_FRACTION * (g_vs.content_h - lh));

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
        if (!reveal && heading_markers_hang(l)) dstart = hpre;
      }
      /* match the text's indent so highlights align (incl. list hanging indent) */
      int row_indent = md_list_indent(l) + (row_start > 0 ? md_list_marker_width(l) : 0);

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
              int x0 = nav_page_margin() + row_indent;
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
            int x0 = nav_page_margin() + row_indent;
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

    /* scrollbar */
    if (max_scroll > 0) {
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
static void cmd_goto_line(void) {       /* M-g */
  g_vs.minibuf_active = 1; minibuf_completing = 0; minibuf_suggest[0] = '\0';
  snprintf(g_vs.minibuf_prompt, sizeof(g_vs.minibuf_prompt), "Goto line: ");
  g_vs.minibuf_text[0] = '\0'; g_vs.minibuf_len = 0; g_vs.minibuf_callback = goto_line_cb;
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
  if (g_ed.dirty && g_ed.filepath[0]) buf_save(&g_ed, g_ed.filepath);   /* save current */
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
  g_vs.minibuf_active = 1; minibuf_completing = 0; minibuf_suggest[0] = '\0';
  g_vs.minibuf_text[0] = '\0'; g_vs.minibuf_len = 0; g_vs.minibuf_callback = NULL;
  bufsw_active = 1; bufsw_listing = 0; bufsw_sel = 0; bufsw_count = 0;
  if (bufsw_default[0])
    snprintf(g_vs.minibuf_prompt, sizeof(g_vs.minibuf_prompt),
             "Switch to buffer (default %s): ", path_base(bufsw_default));
  else
    snprintf(g_vs.minibuf_prompt, sizeof(g_vs.minibuf_prompt), "Switch to buffer: ");
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
    g_ed.cursor_line = g_ed.line_count - 1;
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
      g_vs.minibuf_active = 1;
      minibuf_completing = 1;       /* hint existing filenames (Tab to accept) */
      minibuf_suggest[0] = '\0';
      snprintf(g_vs.minibuf_prompt, sizeof(g_vs.minibuf_prompt), "Write file (Documents): ");
      g_vs.minibuf_text[0] = '\0';
      g_vs.minibuf_len = 0;
      g_vs.minibuf_callback = save_to_path;
    }
    return 1;
  }
  if (ctrl && sym == SDLK_c) {
    exit(EXIT_SUCCESS);
    return 1;
  }
  if (ctrl && sym == SDLK_f) {
    g_vs.minibuf_active = 1;
    minibuf_completing = 1;       /* enable ghost filename completion */
    minibuf_suggest[0] = '\0';
    snprintf(g_vs.minibuf_prompt, sizeof(g_vs.minibuf_prompt), "Find file (Documents): ");
    g_vs.minibuf_text[0] = '\0';
    g_vs.minibuf_len = 0;
    g_vs.minibuf_callback = open_or_create_file;
    return 1;
  }
  if (ctrl && sym == SDLK_w) {   /* C-x C-w: write-file (save as) */
    g_vs.minibuf_active = 1; minibuf_completing = 1; minibuf_suggest[0] = '\0';
    snprintf(g_vs.minibuf_prompt, sizeof(g_vs.minibuf_prompt), "Write file (Documents): ");
    g_vs.minibuf_text[0] = '\0'; g_vs.minibuf_len = 0;
    g_vs.minibuf_callback = save_to_path;
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
    g_vs.syntax_mask = g_vs.syntax_mask ? 0 : SYNTAX_MASK_ALL;
    nav_status_set(&g_vs, g_vs.syntax_mask ? "Syntax highlight on"
                                           : "Syntax highlight off");
    g_vs.suppress_next_text = 1;   /* swallow the "y" text event */
    return 1;
  }
  if (!ctrl && sym == SDLK_s) {                                       /* C-x s */
    g_vs.style_mask = g_vs.style_mask ? 0 : STYLE_MASK_ALL;
    nav_status_set(&g_vs, g_vs.style_mask ? "Style check on"
                                          : "Style check off");
    g_vs.suppress_next_text = 1;   /* swallow the "s" text event */
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
#define WL_MAX 6
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

static void do_render(void) {
  r_clear(color(30, 30, 32, 255));
  process_frame();          /* lays out + draws bg, highlights, scrollbar */
  wikilink_refresh();

  /* draw markdown-formatted text */
  r_set_font_size(g_vs.font_size);
  r_set_font_style(FONT_REGULAR);
  r_set_clip_rect(rect(0, g_vs.content_y, nav_win_w(), g_vs.content_h));
  Color text_color = color(204, 200, 195, 255);
  md_set_syntax_mask(g_vs.syntax_mask);   /* POS coloring (0 = off) for this pass */
  md_set_style_mask(g_vs.style_mask);     /* style-check strikes (0 = off) */
  g_vs.cursor_x = -1;
  for (int i = 0; i < g_vs.vis_row_count; i++) {
    VisRow *vr = &g_vs.vis_rows[i];
    Line *L = &g_ed.lines[vr->ln];
    /* typewriter focus: fade every line except the caret's, crossfading on a
       line change (focus_prev_line → dim, focus_cur_line → full) */
    md_set_text_opacity(g_vs.typewriter_mode
      ? md_focus_opacity(vr->ln, g_vs.focus_cur_line, g_vs.focus_prev_line, g_vs.focus_t)
      : 1.0f);
    int indent = md_list_indent(L);
    /* hang wrapped list text under the item text, not the marker */
    if (vr->row_start > 0) indent += md_list_marker_width(L);
    /* track cursor if it's on this row */
    int track = -1;
    if (vr->ln == g_ed.cursor_line && g_ed.cursor_col >= vr->row_start &&
        (g_ed.cursor_col < vr->row_end || (i + 1 >= g_vs.vis_row_count || g_vs.vis_rows[i+1].ln != vr->ln))) {
      track = g_ed.cursor_col;
    }

    int draw_start = vr->row_start;
    if (vr->heading && vr->row_start == 0) {
      int prefix = md_heading_prefix_len(L);
      /* reveal the markers inline when the caret is at the line start so they
         can be edited/removed; otherwise hang them in the left margin */
      int reveal = (vr->ln == g_ed.cursor_line && g_ed.cursor_col <= prefix);
      /* hang markers in the margin only when there's room; otherwise let the
         "## " render inline (draw_start stays at row_start) so it never
         overlaps the text in a narrow window */
      if (!reveal && heading_markers_hang(L)) {
        int hcount = prefix - 1;
        if (hcount > 23) hcount = 23;
        char hashes[24];
        memset(hashes, '#', hcount); hashes[hcount] = '\0';
        r_set_font_style(FONT_BOLD);
        int hw = r_get_text_width(hashes, hcount);
        int gap = r_get_text_width(" ", 1);
        int hx = nav_page_margin() + indent - gap - hw;   /* right-aligned in the left margin */
        r_draw_text(hashes, vec2(hx, vr->py), color(110, 110, 115, 255));
        r_set_font_style(FONT_REGULAR);
        if (prefix <= vr->row_end) draw_start = prefix;
      }
    }

    md_draw_text(L, draw_start, vr->row_end,
                 nav_page_margin() + indent, vr->py, text_color, vr->heading, track,
                 &g_vs.cursor_x, 1);
    r_set_font_style(FONT_REGULAR);
  }
  md_set_text_opacity(1.0f);   /* don't leak the focus dim past the text pass */
  md_set_syntax_mask(0);       /* status bar etc. draw in their own colors */
  md_set_style_mask(0);

  /* draw cursor (post-render, uses markdown-aware x position) */
  int cursor_py = -1;
  if (g_vs.cursor_x >= 0) {
    int font_h = r_get_text_height();
    /* find the py for the cursor row */
    for (int i = 0; i < g_vs.vis_row_count; i++) {
      VisRow *vr = &g_vs.vis_rows[i];
      if (vr->ln == g_ed.cursor_line && g_ed.cursor_col >= vr->row_start &&
          (g_ed.cursor_col < vr->row_end || (i + 1 >= g_vs.vis_row_count || g_vs.vis_rows[i+1].ln != vr->ln))) {
        r_draw_rect(rect(g_vs.cursor_x, vr->py, 3, font_h),
                    color(90, 200, 250, 255));
        cursor_py = vr->py;
        break;
      }
    }
  }

  /* wikilink autocomplete dropdown, anchored under the "[[" query */
  if (wl_active && wl_count > 0 && g_vs.cursor_x >= 0 && cursor_py >= 0) {
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
    int by = cursor_py + lh;
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

  /* emacs-style status bar (monospace) */
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
  r_set_font_size(g_vs.font_size);

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
  const char *slash = strrchr(g_ed.filepath, '/');
  g_ed.filename = slash ? slash + 1 : g_ed.filepath;

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
  if (g_ed.dirty && g_ed.filepath[0]) buf_save(&g_ed, g_ed.filepath);
  load_daily_note();
  g_vs.scroll_y = 0;
  buf_invalidate_all_wraps(&g_ed);
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

/* ---- pumpable main-loop pieces (see editor_loop.h) ---- */

void editor_handle_event(const SDL_Event *ev) {
  SDL_Event e = *ev;
  switch (e.type) {
    case SDL_QUIT: exit(EXIT_SUCCESS); break;
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
        ed_insert_char(&g_ed, e.text.text);
        nav_ensure_cursor_visible(&g_ed, &g_vs);
      }
      break;

    case SDL_MOUSEBUTTONDOWN:
    case SDL_MOUSEBUTTONUP: {
      int b = button_map[e.button.button & 0xff];
      if (b && e.type == SDL_MOUSEBUTTONDOWN) {
        g_mouse_x = e.button.x; g_mouse_y = e.button.y;
        g_mouse_down |= b; g_mouse_pressed |= b;
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
          if ((e.key.keysym.mod & KMOD_SHIFT) && !g_ed.mark_active)
            buf_mark_set(&g_ed);
          switch (sym) {
            case SDLK_LEFT:
              if (ctrl || alt) cmd_backward_word(&g_ed, &g_vs);
              else             cmd_backward_char(&g_ed, &g_vs);
              break;
            case SDLK_RIGHT:
              if (ctrl || alt) cmd_forward_word(&g_ed, &g_vs);
              else             cmd_forward_char(&g_ed, &g_vs);
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
      if (g_ed.dirty && g_ed.filepath[0] && buf_save(&g_ed, g_ed.filepath) == 0) {
        nav_status_set(&g_vs, "Auto-saved");
      }
    }
  }

  /* keep the title-bar "Publish to X" button in sync with the connection
     state (the user may link/unlink X via Settings while running). Cheap
     in-memory check; only touches AppKit when the state actually flips. */
  {
    int c = kern_x_is_connected();
    if (c != g_last_x_conn) { g_last_x_conn = c; kern_titlebar_set_x_connected(c); }
  }

  do_render();
}

#ifndef KERN_HEADLESS_TEST
int editor_main(int argc, char **argv) {
  if (argc >= 2 && buf_load_file(&g_ed, argv[1]) == 0) {
    snprintf(g_ed.filepath, sizeof(g_ed.filepath), "%s", argv[1]);
    g_ed.filename = argv[1];
    const char *slash = strrchr(argv[1], '/');
    if (slash) g_ed.filename = slash + 1;
    printf("loaded %d lines\n", g_ed.line_count);
  } else {
    /* no file given: open today's daily note instead of an empty scratch buffer */
    load_daily_note();
  }
  recent_push(g_ed.filepath);   /* seed the MRU with the initially-opened file */

  SDL_Init(SDL_INIT_EVERYTHING);
  g_vs.font_size = 26.0f;
  g_vs.search_direction = 1;
  g_vs.search_match_line = -1;
  g_vs.search_match_col = -1;
  g_vs.cursor_x = -1;
  if (!g_ed.filename) g_ed.filename = "*scratch*";
  r_init();
  pos_tagger_warm();   /* pay the POS model-load cost now, before the first frame */
  r_set_font_size(g_vs.font_size);
  r_set_title(g_ed.filename);
  macos_style_window(r_get_window());

  SDL_AddEventWatch(resize_event_watcher, NULL);
  for (;;) {
    SDL_Event e;
    /* Block until input arrives (NULL leaves events queued for the drain loop
       below) instead of busy-spinning. The timeout wakes us periodically so
       transient status-bar messages can still clear without input. */
    SDL_WaitEventTimeout(NULL, (g_scroll_animating || g_dim_animating) ? 16 : 250);
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
  /* modal file-statics that persist across events */
  minibuf_completing = 0; minibuf_suggest[0] = '\0';
  bufsw_active = bufsw_listing = bufsw_sel = bufsw_count = 0;
  nav_back_count = 0; nav_fwd_count = 0;
  wl_active = wl_count = wl_sel = 0;
  wl_query[0] = wl_last_query[0] = wl_suppressed[0] = '\0';
  wl_has_suppress = 0;
  g_mouse_x = g_mouse_y = g_mouse_down = g_mouse_pressed = 0;
  g_scroll_animating = g_dim_animating = 0;
  g_last_autosave = 0;
  g_last_x_conn = -1;
}
#endif /* KERN_HEADLESS_TEST */

