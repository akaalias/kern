#include <SDL2/SDL.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "renderer.h"
#include "microui.h"
#include "macos_style.h"

/* ---- constants ---- */
#define TOP_PADDING  82
#define LINE_HEIGHT_MULT 1.5f
#define CHARS_PER_LINE 70

static int win_w(void) { int w, h; r_get_size(&w, &h); return w; }
static int win_h(void) { int w, h; r_get_size(&w, &h); return h; }

#define MIN_MARGIN 20

static int page_w(void) {
  int ideal = r_get_text_width("n", 1) * CHARS_PER_LINE;
  int max_w = win_w() - 2 * MIN_MARGIN;
  return ideal < max_w ? ideal : max_w;
}

static int page_margin(void) {
  int pw = page_w();
  int m = (win_w() - pw) / 2;
  return m > MIN_MARGIN ? m : MIN_MARGIN;
}

static int line_height(void) {
  return (int)(r_get_text_height() * LINE_HEIGHT_MULT + 0.5f);
}

/* ---- line buffer (mutable) ---- */
typedef struct {
  char *text;
  int   len;
  int   cap;
  int   wrap_count;   /* cached visual line count, -1 = dirty */
} Line;

static Line  *lines     = NULL;
static int    line_count = 0;
static int    line_cap   = 0;
static float  font_size  = 20.0f;
static const char *g_filename = "";

static void line_ensure_cap(Line *l, int need) {
  if (need + 1 > l->cap) {
    l->cap = (need + 1) * 2;
    l->text = realloc(l->text, l->cap);
  }
}

static void line_init(Line *l, const char *s, int len) {
  l->cap = len + 16;
  l->text = malloc(l->cap);
  memcpy(l->text, s, len);
  l->text[len] = '\0';
  l->len = len;
  l->wrap_count = -1;
}

static void line_dirty(Line *l) { l->wrap_count = -1; }

static void ensure_lines_cap(int need) {
  if (need > line_cap) {
    line_cap = need * 2;
    lines = realloc(lines, line_cap * sizeof(Line));
  }
}

static void insert_line_at(int idx, const char *s, int len) {
  ensure_lines_cap(line_count + 1);
  memmove(&lines[idx + 1], &lines[idx], (line_count - idx) * sizeof(Line));
  line_init(&lines[idx], s, len);
  line_count++;
}

static void delete_line_at(int idx) {
  free(lines[idx].text);
  memmove(&lines[idx], &lines[idx + 1], (line_count - idx - 1) * sizeof(Line));
  line_count--;
}

static void load_file(const char *path) {
  FILE *f = fopen(path, "rb");
  if (!f) { fprintf(stderr, "cannot open %s\n", path); exit(1); }
  fseek(f, 0, SEEK_END);
  long sz = ftell(f);
  fseek(f, 0, SEEK_SET);
  char *buf = malloc(sz + 1);
  fread(buf, 1, sz, f);
  buf[sz] = '\0';
  fclose(f);

  line_cap = 4096;
  lines = malloc(line_cap * sizeof(Line));
  line_count = 0;

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
    ensure_lines_cap(line_count + 1);
    line_init(&lines[line_count], p, len);
    line_count++;
    if (nl) p = nl + 1; else break;
  }

  if (line_count == 0) {
    ensure_lines_cap(1);
    line_init(&lines[0], "", 0);
    line_count = 1;
  }

  free(buf);
}

static void invalidate_all_wraps(void) {
  for (int i = 0; i < line_count; i++) lines[i].wrap_count = -1;
}

/* ---- word wrapping ---- */

/* forward declaration */
static int get_wrap_breaks(Line *l, int *starts, int max_starts);

/* count how many visual lines a logical line produces */
static int count_wraps(Line *l) {
  if (l->wrap_count >= 0) return l->wrap_count;
  int starts[256];
  l->wrap_count = get_wrap_breaks(l, starts, 256);
  return l->wrap_count;
}

/* get visual rows for a line: fills starts[] and returns count.
   starts[k] = column index where visual row k begins. */
static int get_wrap_breaks(Line *l, int *starts, int max_starts) {
  starts[0] = 0;
  if (l->len == 0) return 1;

  int count = 1;
  int x = 0;
  int last_space = -1;
  int row_start = 0;
  int i = 0;

  while (i < l->len && count < max_starts) {
    int cw = r_get_text_width(l->text + i, 1);

    if (x + cw > page_w() && i > row_start) {
      int brk = i;
      if (last_space > row_start) brk = last_space + 1;
      starts[count] = brk;
      count++;
      row_start = brk;
      i = brk;
      x = 0;
      last_space = -1;
      continue;
    }

    if (l->text[i] == ' ') last_space = i;
    x += cw;
    i++;
  }

  return count;
}

/* total visual lines for all logical lines */
static int total_visual_lines(void) {
  int total = 0;
  for (int i = 0; i < line_count; i++) {
    total += count_wraps(&lines[i]);
  }
  return total;
}

/* find which logical line corresponds to a visual line offset.
   returns logical line, sets *visual_offset to visual line within that logical line. */
static int visual_to_logical(int visual_line, int *visual_offset) {
  int accum = 0;
  for (int i = 0; i < line_count; i++) {
    int wc = count_wraps(&lines[i]);
    if (accum + wc > visual_line) {
      *visual_offset = visual_line - accum;
      return i;
    }
    accum += wc;
  }
  *visual_offset = 0;
  return line_count - 1;
}

/* get the visual line index of logical line start */
static int logical_to_visual(int logical_line) {
  int accum = 0;
  for (int i = 0; i < logical_line && i < line_count; i++) {
    accum += count_wraps(&lines[i]);
  }
  return accum;
}

/* get the visual line index of a cursor position (line, col) */
static int cursor_to_visual(int cline, int ccol) {
  int base = logical_to_visual(cline);
  int starts[256];
  int nrows = get_wrap_breaks(&lines[cline], starts, 256);
  for (int r = nrows - 1; r >= 0; r--) {
    if (ccol >= starts[r]) return base + r;
  }
  return base;
}

/* ---- cursor ---- */
static int cursor_line = 0;
static int cursor_col  = 0;
static int cursor_target_col = 0;

static void cursor_clamp(void) {
  if (cursor_line < 0) cursor_line = 0;
  if (cursor_line >= line_count) cursor_line = line_count - 1;
  if (cursor_col < 0) cursor_col = 0;
  if (cursor_col > lines[cursor_line].len) cursor_col = lines[cursor_line].len;
}

/* ---- editing operations ---- */
static void editor_insert_char(const char *text) {
  int tlen = strlen(text);
  Line *l = &lines[cursor_line];
  line_ensure_cap(l, l->len + tlen);
  memmove(l->text + cursor_col + tlen, l->text + cursor_col, l->len - cursor_col + 1);
  memcpy(l->text + cursor_col, text, tlen);
  l->len += tlen;
  cursor_col += tlen;
  cursor_target_col = cursor_col;
  line_dirty(l);
}

static void editor_backspace(void) {
  if (cursor_col > 0) {
    Line *l = &lines[cursor_line];
    memmove(l->text + cursor_col - 1, l->text + cursor_col, l->len - cursor_col + 1);
    l->len--;
    cursor_col--;
    line_dirty(l);
  } else if (cursor_line > 0) {
    int prev = cursor_line - 1;
    int new_col = lines[prev].len;
    line_ensure_cap(&lines[prev], lines[prev].len + lines[cursor_line].len);
    memcpy(lines[prev].text + lines[prev].len, lines[cursor_line].text, lines[cursor_line].len + 1);
    lines[prev].len += lines[cursor_line].len;
    delete_line_at(cursor_line);
    cursor_line = prev;
    cursor_col = new_col;
    line_dirty(&lines[prev]);
  }
  cursor_target_col = cursor_col;
}

static void editor_delete(void) {
  Line *l = &lines[cursor_line];
  if (cursor_col < l->len) {
    memmove(l->text + cursor_col, l->text + cursor_col + 1, l->len - cursor_col);
    l->len--;
    line_dirty(l);
  } else if (cursor_line < line_count - 1) {
    int next = cursor_line + 1;
    line_ensure_cap(l, l->len + lines[next].len);
    memcpy(l->text + l->len, lines[next].text, lines[next].len + 1);
    l->len += lines[next].len;
    delete_line_at(next);
    line_dirty(l);
  }
}

static void editor_enter(void) {
  Line *l = &lines[cursor_line];
  int rest_len = l->len - cursor_col;
  insert_line_at(cursor_line + 1, l->text + cursor_col, rest_len);
  l = &lines[cursor_line];
  l->len = cursor_col;
  l->text[l->len] = '\0';
  line_dirty(l);
  cursor_line++;
  cursor_col = 0;
  cursor_target_col = 0;
}

static void ensure_cursor_visible(void);

/* ---- kill buffer ---- */
static char *kill_buf = NULL;
static int   kill_len = 0;
static int   kill_cap = 0;

static void kill_set(const char *text, int len) {
  if (len + 1 > kill_cap) {
    kill_cap = (len + 1) * 2;
    kill_buf = realloc(kill_buf, kill_cap);
  }
  memcpy(kill_buf, text, len);
  kill_buf[len] = '\0';
  kill_len = len;
}

static void kill_append(const char *text, int len) {
  if (kill_len + len + 1 > kill_cap) {
    kill_cap = (kill_len + len + 1) * 2;
    kill_buf = realloc(kill_buf, kill_cap);
  }
  memcpy(kill_buf + kill_len, text, len);
  kill_len += len;
  kill_buf[kill_len] = '\0';
}

/* ---- mark / region ---- */
static int mark_active = 0;
static int mark_line = 0;
static int mark_col  = 0;

static void mark_set(void) {
  mark_active = 1;
  mark_line = cursor_line;
  mark_col = cursor_col;
}

static void mark_clear(void) {
  mark_active = 0;
}

/* get region as (start_line, start_col, end_line, end_col) ordered */
static void region_ordered(int *sl, int *sc, int *el, int *ec) {
  if (mark_line < cursor_line || (mark_line == cursor_line && mark_col <= cursor_col)) {
    *sl = mark_line; *sc = mark_col; *el = cursor_line; *ec = cursor_col;
  } else {
    *sl = cursor_line; *sc = cursor_col; *el = mark_line; *ec = mark_col;
  }
}

/* ---- undo (snapshot-based) ---- */
#define MAX_UNDO 64

typedef struct {
  char **lines_text;  /* array of line texts */
  int   *lines_len;
  int    line_count;
  int    cursor_line, cursor_col;
} UndoSnapshot;

static UndoSnapshot undo_stack[MAX_UNDO];
static int undo_top = 0;
static int undo_count = 0;

static void undo_free_snapshot(UndoSnapshot *s) {
  if (s->lines_text) {
    for (int i = 0; i < s->line_count; i++) free(s->lines_text[i]);
    free(s->lines_text);
    free(s->lines_len);
  }
  s->lines_text = NULL;
  s->lines_len = NULL;
  s->line_count = 0;
}

static void undo_push(void) {
  UndoSnapshot *s = &undo_stack[undo_top % MAX_UNDO];
  undo_free_snapshot(s);
  s->line_count = line_count;
  s->lines_text = malloc(line_count * sizeof(char*));
  s->lines_len  = malloc(line_count * sizeof(int));
  for (int i = 0; i < line_count; i++) {
    s->lines_text[i] = malloc(lines[i].len + 1);
    memcpy(s->lines_text[i], lines[i].text, lines[i].len + 1);
    s->lines_len[i] = lines[i].len;
  }
  s->cursor_line = cursor_line;
  s->cursor_col = cursor_col;
  undo_top++;
  if (undo_top > MAX_UNDO) undo_top = MAX_UNDO;
  undo_count = undo_top;
}

static void editor_undo(void) {
  if (undo_top == 0) return;
  undo_top--;
  UndoSnapshot *s = &undo_stack[undo_top % MAX_UNDO];
  /* free current lines */
  for (int i = 0; i < line_count; i++) free(lines[i].text);
  /* restore from snapshot */
  ensure_lines_cap(s->line_count);
  line_count = s->line_count;
  for (int i = 0; i < line_count; i++) {
    lines[i].len = s->lines_len[i];
    lines[i].cap = s->lines_len[i] + 16;
    lines[i].text = malloc(lines[i].cap);
    memcpy(lines[i].text, s->lines_text[i], s->lines_len[i] + 1);
    lines[i].wrap_count = -1;
  }
  cursor_line = s->cursor_line;
  cursor_col = s->cursor_col;
  cursor_clamp();
}

/* ---- emacs commands ---- */

/* ctrl-k: kill to end of line */
static int last_kill_was_k = 0;

static void emacs_kill_line(void) {
  Line *l = &lines[cursor_line];
  if (cursor_col < l->len) {
    /* kill from cursor to end of line */
    int kill_count = l->len - cursor_col;
    if (last_kill_was_k) {
      kill_append(l->text + cursor_col, kill_count);
    } else {
      kill_set(l->text + cursor_col, kill_count);
    }
    l->len = cursor_col;
    l->text[l->len] = '\0';
    line_dirty(l);
  } else if (cursor_line < line_count - 1) {
    /* at end of line: kill the newline (join with next line) */
    if (last_kill_was_k) {
      kill_append("\n", 1);
    } else {
      kill_set("\n", 1);
    }
    Line *next = &lines[cursor_line + 1];
    line_ensure_cap(l, l->len + next->len);
    memcpy(l->text + l->len, next->text, next->len + 1);
    l->len += next->len;
    delete_line_at(cursor_line + 1);
    line_dirty(l);
  }
  last_kill_was_k = 1;
}

/* ctrl-y: yank */
static void emacs_yank(void) {
  if (!kill_buf || kill_len == 0) return;
  for (int i = 0; i < kill_len; i++) {
    if (kill_buf[i] == '\n') {
      editor_enter();
    } else {
      char ch[2] = { kill_buf[i], 0 };
      editor_insert_char(ch);
    }
  }
}

/* ctrl-w: kill region */
static void emacs_kill_region(void) {
  if (!mark_active) return;
  int sl, sc, el, ec;
  region_ordered(&sl, &sc, &el, &ec);

  /* collect region text */
  int total = 0;
  for (int ln = sl; ln <= el; ln++) {
    int cs = (ln == sl) ? sc : 0;
    int ce = (ln == el) ? ec : lines[ln].len;
    total += ce - cs;
    if (ln < el) total++; /* newline */
  }
  char *region = malloc(total + 1);
  int pos = 0;
  for (int ln = sl; ln <= el; ln++) {
    int cs = (ln == sl) ? sc : 0;
    int ce = (ln == el) ? ec : lines[ln].len;
    memcpy(region + pos, lines[ln].text + cs, ce - cs);
    pos += ce - cs;
    if (ln < el) region[pos++] = '\n';
  }
  region[pos] = '\0';
  kill_set(region, total);
  free(region);

  /* delete region: position cursor at start, delete forward */
  cursor_line = sl; cursor_col = sc;
  /* delete from end backwards to start */
  if (sl == el) {
    /* single line */
    Line *l = &lines[sl];
    memmove(l->text + sc, l->text + ec, l->len - ec + 1);
    l->len -= (ec - sc);
    line_dirty(l);
  } else {
    /* multi-line: keep start of first line + end of last line */
    Line *first = &lines[sl];
    Line *last  = &lines[el];
    int new_len = sc + (last->len - ec);
    line_ensure_cap(first, new_len);
    memmove(first->text + sc, last->text + ec, last->len - ec + 1);
    first->len = new_len;
    line_dirty(first);
    for (int i = el; i > sl; i--) delete_line_at(i);
  }

  cursor_clamp();
  mark_clear();
}

/* alt-f: forward word */
static void emacs_forward_word(void) {
  Line *l = &lines[cursor_line];
  /* skip non-space, then skip space */
  while (cursor_col < l->len && l->text[cursor_col] != ' ') cursor_col++;
  while (cursor_col < l->len && l->text[cursor_col] == ' ') cursor_col++;
  if (cursor_col >= l->len && cursor_line < line_count - 1) {
    cursor_line++; cursor_col = 0;
  }
  cursor_target_col = cursor_col;
}

/* alt-b: backward word */
static void emacs_backward_word(void) {
  if (cursor_col == 0 && cursor_line > 0) {
    cursor_line--;
    cursor_col = lines[cursor_line].len;
  }
  Line *l = &lines[cursor_line];
  /* skip space backwards, then skip non-space */
  while (cursor_col > 0 && l->text[cursor_col - 1] == ' ') cursor_col--;
  while (cursor_col > 0 && l->text[cursor_col - 1] != ' ') cursor_col--;
  cursor_target_col = cursor_col;
}

/* ---- search state ---- */
static int   search_active = 0;
static char  search_buf[256] = "";
static int   search_len = 0;
static int   search_match_line = -1;  /* current match logical line */
static int   search_match_col = -1;   /* current match column */

static void search_find_next(int from_line, int from_col) {
  if (search_len == 0) { search_match_line = -1; return; }
  /* search from (from_line, from_col+1) forward, wrapping around */
  for (int pass = 0; pass < 2; pass++) {
    int start_ln = (pass == 0) ? from_line : 0;
    int end_ln   = (pass == 0) ? line_count : from_line + 1;
    for (int ln = start_ln; ln < end_ln; ln++) {
      Line *l = &lines[ln];
      int start_col = (pass == 0 && ln == from_line) ? from_col + 1 : 0;
      for (int c = start_col; c <= l->len - search_len; c++) {
        if (strncasecmp(l->text + c, search_buf, search_len) == 0) {
          search_match_line = ln;
          search_match_col = c;
          cursor_line = ln;
          cursor_col = c;
          cursor_target_col = c;
          ensure_cursor_visible();
          return;
        }
      }
    }
  }
  search_match_line = -1;
}

static void search_find_first(void) {
  search_find_next(0, -1);
}

/* ---- scroll state ---- */
static float scroll_y = 0;
static int   scrollbar_dragging = 0;
static float drag_offset = 0;

static int g_content_y;   /* top of content area */
static int g_content_h;   /* height of content area */

static void ensure_cursor_visible(void) {
  int lh = line_height();
  int vis = cursor_to_visual(cursor_line, cursor_col);
  float cursor_top = vis * lh;
  float cursor_bot = cursor_top + lh;
  int view_h = g_content_h > 0 ? g_content_h : (win_h());
  if (cursor_top < scroll_y) scroll_y = cursor_top;
  if (cursor_bot > scroll_y + view_h) scroll_y = cursor_bot - view_h;
}

/* ---- click to position cursor ---- */
static void click_to_cursor(int mx, int my) {
  int lh = line_height();
  int rel_y = my - g_content_y + (int)scroll_y;
  int vis_line = rel_y / lh;
  if (vis_line < 0) vis_line = 0;

  int total = total_visual_lines();
  if (vis_line >= total) vis_line = total - 1;

  /* find logical line and wrap row */
  int wrap_offset;
  int ln = visual_to_logical(vis_line, &wrap_offset);

  /* get wrap breaks for this line */
  int starts[256];
  int nrows = get_wrap_breaks(&lines[ln], starts, 256);
  int row_start = starts[wrap_offset];
  int row_end = (wrap_offset + 1 < nrows) ? starts[wrap_offset + 1] : lines[ln].len;

  /* find column within this visual row */
  int col = row_start;
  if (mx > page_margin()) {
    int px = page_margin();
    for (int i = row_start; i < row_end; i++) {
      int cw = r_get_text_width(lines[ln].text + i, 1);
      if (px + cw / 2 > mx) break;
      px += cw;
      col = i + 1;
    }
  }

  cursor_line = ln;
  cursor_col = col;
  cursor_clamp();
  cursor_target_col = cursor_col;
}

/* ---- frame ---- */
static void process_frame(mu_Context *ctx) {
  mu_begin(ctx);

  if (mu_begin_window_ex(ctx, "Writer",
        mu_rect(0, 0, win_w(), win_h()),
        MU_OPT_NOTITLE | MU_OPT_NORESIZE | MU_OPT_NOCLOSE | MU_OPT_NOSCROLL)) {
    mu_Container *win = mu_get_current_container(ctx);
    win->rect = mu_rect(0, 0, win_w(), win_h());

    int lh = line_height();
    g_content_y = TOP_PADDING;
    g_content_h = win_h() - TOP_PADDING;

    /* content area */
    int total_vis = total_visual_lines();
    float max_scroll = (total_vis * lh) - g_content_h;
    if (max_scroll < 0) max_scroll = 0;
    if (scroll_y < 0) scroll_y = 0;
    if (scroll_y > max_scroll) scroll_y = max_scroll;

    int first_vis = (int)(scroll_y / lh);
    float y_offset = scroll_y - (first_vis * lh);

    /* how many visual lines fit on screen */
    int vis_on_screen = g_content_h / lh + 4;

    /* clip to content area */
    mu_push_clip_rect(ctx, mu_rect(0, g_content_y, win_w(), g_content_h));

    /* render visible visual lines */
    for (int vi = 0; vi < vis_on_screen; vi++) {
      int vis_idx = first_vis + vi;
      if (vis_idx >= total_vis) break;

      int py = g_content_y + (vi * lh) - (int)y_offset;
      if (py + lh < g_content_y || py > g_content_y + g_content_h) continue;

      /* map visual line to logical line + wrap row */
      int wrap_off;
      int ln = visual_to_logical(vis_idx, &wrap_off);
      Line *l = &lines[ln];

      int starts[256];
      int nrows = get_wrap_breaks(l, starts, 256);
      int row_start = starts[wrap_off];
      int row_end = (wrap_off + 1 < nrows) ? starts[wrap_off + 1] : l->len;

      /* draw mark region highlight */
      if (mark_active && row_end > row_start) {
        int sl, sc, el, ec;
        region_ordered(&sl, &sc, &el, &ec);
        if (ln >= sl && ln <= el) {
          int hl_start = (ln == sl) ? sc : 0;
          int hl_end   = (ln == el) ? ec : lines[ln].len;
          /* clamp to this visual row */
          if (hl_start < row_end && hl_end > row_start) {
            int hs = hl_start < row_start ? row_start : hl_start;
            int he = hl_end > row_end ? row_end : hl_end;
            if (he > hs) {
              int hx = page_margin() + r_get_text_width(l->text + row_start, hs - row_start);
              int hw = r_get_text_width(l->text + hs, he - hs);
              int font_h = r_get_text_height();
              mu_draw_rect(ctx, mu_rect(hx, py, hw, font_h),
                           mu_color(60, 100, 160, 180));
            }
          }
        }
      }

      /* draw text for this visual row */
      if (row_end > row_start) {
        char display[1024];
        int dlen = row_end - row_start;
        if (dlen > (int)sizeof(display) - 1) dlen = (int)sizeof(display) - 1;
        memcpy(display, l->text + row_start, dlen);
        display[dlen] = '\0';
        mu_draw_text(ctx, NULL, display, -1,
                     mu_vec2(page_margin(), py),
                     ctx->style->colors[MU_COLOR_TEXT]);
      }

      /* draw search highlights on this visual row */
      if (search_active && search_len > 0 && row_end > row_start) {
        for (int sc = row_start; sc <= row_end - search_len; sc++) {
          if (strncasecmp(l->text + sc, search_buf, search_len) == 0) {
            int hx = page_margin() + r_get_text_width(l->text + row_start, sc - row_start);
            int hw = r_get_text_width(l->text + sc, search_len);
            int font_h = r_get_text_height();
            /* current match gets brighter highlight */
            if (ln == search_match_line && sc == search_match_col) {
              mu_draw_rect(ctx, mu_rect(hx, py, hw, font_h),
                           mu_color(200, 150, 0, 180));
            } else {
              mu_draw_rect(ctx, mu_rect(hx, py, hw, font_h),
                           mu_color(120, 90, 0, 120));
            }
          }
        }
      }

      /* draw cursor if it falls on this visual row */
      if (ln == cursor_line && cursor_col >= row_start && cursor_col <= row_end) {
        /* only draw on the correct row (cursor at row_end belongs to next row unless it's the last) */
        if (cursor_col >= row_start && (cursor_col < row_end || wrap_off == nrows - 1)) {
          int blink = (SDL_GetTicks() / 500) % 2;
          if (blink == 0) {
            int cx = page_margin();
            if (cursor_col > row_start) {
              cx += r_get_text_width(l->text + row_start, cursor_col - row_start);
            }
            int font_h = r_get_text_height();
            mu_draw_rect(ctx, mu_rect(cx, py, 2, font_h),
                         mu_color(90, 200, 250, 255));
          }
        }
      }
    }

    mu_pop_clip_rect(ctx);

    /* scrollbar */
    if (max_scroll > 0) {
      int sb_x = win_w() - 8;
      int sb_w = 6;
      int sb_h = g_content_h;
      float thumb_ratio = (float)g_content_h / (total_vis * lh);
      int thumb_h = (int)(sb_h * thumb_ratio);
      if (thumb_h < 20) thumb_h = 20;
      int thumb_y = g_content_y + (int)((scroll_y / max_scroll) * (sb_h - thumb_h));

      int mx = ctx->mouse_pos.x, my = ctx->mouse_pos.y;
      int mouse_in_track = (mx >= sb_x - 4 && mx < sb_x + sb_w + 4 && my >= g_content_y && my < g_content_y + sb_h);

      if (scrollbar_dragging) {
        if (ctx->mouse_down & MU_MOUSE_LEFT) {
          float ratio = (my - drag_offset - g_content_y) / (float)(sb_h - thumb_h);
          if (ratio < 0) ratio = 0;
          if (ratio > 1) ratio = 1;
          scroll_y = ratio * max_scroll;
        } else {
          scrollbar_dragging = 0;
        }
      } else if (mouse_in_track && (ctx->mouse_pressed & MU_MOUSE_LEFT)) {
        if (my >= thumb_y && my < thumb_y + thumb_h) {
          scrollbar_dragging = 1;
          drag_offset = my - thumb_y;
        } else {
          float ratio = (my - g_content_y - thumb_h / 2.0f) / (float)(sb_h - thumb_h);
          if (ratio < 0) ratio = 0;
          if (ratio > 1) ratio = 1;
          scroll_y = ratio * max_scroll;
          scrollbar_dragging = 1;
          drag_offset = thumb_h / 2.0f;
        }
      }

      mu_Color thumb_color = scrollbar_dragging ? mu_color(140, 140, 140, 255) :
                             mouse_in_track     ? mu_color(120, 120, 120, 255) :
                                                  mu_color(80, 80, 80, 255);
      mu_draw_rect(ctx, mu_rect(sb_x, thumb_y, sb_w, thumb_h), thumb_color);
    }

    mu_end_window(ctx);
  }

  mu_end(ctx);
}


/* ---- SDL boilerplate ---- */

static const char button_map[256] = {
  [ SDL_BUTTON_LEFT   & 0xff ] =  MU_MOUSE_LEFT,
  [ SDL_BUTTON_RIGHT  & 0xff ] =  MU_MOUSE_RIGHT,
  [ SDL_BUTTON_MIDDLE & 0xff ] =  MU_MOUSE_MIDDLE,
};

static const char key_map[256] = {
  [ SDLK_LSHIFT       & 0xff ] = MU_KEY_SHIFT,
  [ SDLK_RSHIFT       & 0xff ] = MU_KEY_SHIFT,
  [ SDLK_LCTRL        & 0xff ] = MU_KEY_CTRL,
  [ SDLK_RCTRL        & 0xff ] = MU_KEY_CTRL,
  [ SDLK_LALT         & 0xff ] = MU_KEY_ALT,
  [ SDLK_RALT         & 0xff ] = MU_KEY_ALT,
  [ SDLK_RETURN       & 0xff ] = MU_KEY_RETURN,
  [ SDLK_BACKSPACE    & 0xff ] = MU_KEY_BACKSPACE,
};

static int text_width(mu_Font font, const char *text, int len) {
  if (len == -1) { len = strlen(text); }
  return r_get_text_width(text, len);
}

static int text_height(mu_Font font) {
  return r_get_text_height();
}


/* ---- render pass (callable from main loop and resize watcher) ---- */
static mu_Context *g_ctx = NULL;

static void do_render(void) {
  mu_Context *ctx = g_ctx;
  if (!ctx) return;

  process_frame(ctx);

  r_clear(mu_color(30, 30, 32, 255));
  mu_Command *cmd = NULL;
  while (mu_next_command(ctx, &cmd)) {
    switch (cmd->type) {
      case MU_COMMAND_TEXT: r_draw_text(cmd->text.str, cmd->text.pos, cmd->text.color); break;
      case MU_COMMAND_RECT: r_draw_rect(cmd->rect.rect, cmd->rect.color); break;
      case MU_COMMAND_ICON: r_draw_icon(cmd->icon.id, cmd->icon.rect, cmd->icon.color); break;
      case MU_COMMAND_CLIP: r_set_clip_rect(cmd->clip.rect); break;
    }
  }

  /* search bar */
  if (search_active) {
    r_set_font_size(14.0f);
    int bar_h = 28;
    int bar_y = TOP_PADDING - bar_h - 2;
    /* background */
    r_set_clip_rect(mu_rect(0, 0, win_w(), win_h()));
    r_draw_rect(mu_rect(0, bar_y, win_w(), bar_h), mu_color(45, 45, 47, 255));
    /* bottom border */
    r_draw_rect(mu_rect(0, bar_y + bar_h, win_w(), 1), mu_color(60, 60, 62, 255));
    /* search icon / label */
    r_draw_text("Find:", mu_vec2(12, bar_y + 6), mu_color(120, 120, 120, 255));
    /* search text */
    int label_w = r_get_text_width("Find: ", 6);
    r_draw_text(search_buf, mu_vec2(12 + label_w, bar_y + 6), mu_color(204, 200, 195, 255));
    /* blinking cursor in search box */
    if ((SDL_GetTicks() / 500) % 2 == 0) {
      int cx = 12 + label_w + r_get_text_width(search_buf, search_len);
      r_draw_rect(mu_rect(cx, bar_y + 5, 1, 16), mu_color(90, 200, 250, 255));
    }
    /* match count */
    if (search_len > 0) {
      int match_count = 0;
      for (int i = 0; i < line_count; i++) {
        Line *l = &lines[i];
        for (int c = 0; c <= l->len - search_len; c++) {
          if (strncasecmp(l->text + c, search_buf, search_len) == 0) match_count++;
        }
      }
      char count_buf[32];
      snprintf(count_buf, sizeof(count_buf), "%d matches", match_count);
      int cw = r_get_text_width(count_buf, strlen(count_buf));
      r_draw_text(count_buf, mu_vec2(win_w() - cw - 12, bar_y + 6), mu_color(120, 120, 120, 255));
    }
    r_set_font_size(font_size);
  }

  /* info bar bottom-right */
  r_set_font_size(13.0f);
  char info[64];
  snprintf(info, sizeof(info), "Ln %d, Col %d  |  %.0fpt", cursor_line + 1, cursor_col + 1, font_size);
  int info_w = r_get_text_width(info, strlen(info));
  r_draw_text(info, mu_vec2(win_w() - info_w - 12, win_h() - 18), mu_color(80, 80, 80, 255));
  r_set_font_size(font_size);

  r_present();
}

static int resize_event_watcher(void *data, SDL_Event *event) {
  (void)data;
  if (event->type == SDL_WINDOWEVENT &&
      (event->window.event == SDL_WINDOWEVENT_RESIZED ||
       event->window.event == SDL_WINDOWEVENT_SIZE_CHANGED)) {
    /* just update GL viewport so the clear color fills correctly, but don't reflow */
    r_handle_resize();
    r_clear(mu_color(30, 30, 32, 255));
    r_present();
  }
  return 0;
}

int main(int argc, char **argv) {
  if (argc < 2) {
    fprintf(stderr, "usage: textview <file>\n");
    return 1;
  }

  load_file(argv[1]);
  /* extract filename from path */
  g_filename = argv[1];
  const char *slash = strrchr(argv[1], '/');
  if (slash) g_filename = slash + 1;

  printf("loaded %d lines\n", line_count);

  SDL_Init(SDL_INIT_EVERYTHING);
  r_init();
  r_set_title(g_filename);
  macos_style_window(r_get_window());

  mu_Context *ctx = malloc(sizeof(mu_Context));
  mu_init(ctx);
  ctx->text_width = text_width;
  ctx->text_height = text_height;
  ctx->style->colors[MU_COLOR_TEXT] = mu_color(204, 200, 195, 255);

  /* store ctx globally so the resize watcher can trigger a re-render */
  g_ctx = ctx;
  SDL_AddEventWatch(resize_event_watcher, NULL);

  for (;;) {
    SDL_Event e;
    while (SDL_PollEvent(&e)) {
      switch (e.type) {
        case SDL_QUIT: exit(EXIT_SUCCESS); break;
        case SDL_WINDOWEVENT:
          if (e.window.event == SDL_WINDOWEVENT_RESIZED ||
              e.window.event == SDL_WINDOWEVENT_SIZE_CHANGED) {
            r_handle_resize();
            invalidate_all_wraps();
          }
          break;
        case SDL_MOUSEMOTION: mu_input_mousemove(ctx, e.motion.x, e.motion.y); break;
        case SDL_MOUSEWHEEL: scroll_y -= e.wheel.y * line_height() * 3; break;

        case SDL_TEXTINPUT:
          if (SDL_GetModState() & (KMOD_CTRL | KMOD_GUI | KMOD_ALT)) break;
          if (search_active) {
            int tlen = strlen(e.text.text);
            if (search_len + tlen < (int)sizeof(search_buf) - 1) {
              memcpy(search_buf + search_len, e.text.text, tlen);
              search_len += tlen;
              search_buf[search_len] = '\0';
              search_find_first();
            }
          } else {
            undo_push();
            editor_insert_char(e.text.text);
            ensure_cursor_visible();
          }
          break;

        case SDL_MOUSEBUTTONDOWN:
        case SDL_MOUSEBUTTONUP: {
          int b = button_map[e.button.button & 0xff];
          if (b && e.type == SDL_MOUSEBUTTONDOWN) {
            mu_input_mousedown(ctx, e.button.x, e.button.y, b);
            if (b == MU_MOUSE_LEFT && e.button.y > TOP_PADDING && e.button.x < win_w() - 12) {
              click_to_cursor(e.button.x, e.button.y);
            }
          }
          if (b && e.type == SDL_MOUSEBUTTONUP) { mu_input_mouseup(ctx, e.button.x, e.button.y, b); }
          break;
        }

        case SDL_KEYDOWN:
        case SDL_KEYUP: {
          /* suppress macOS system beep for ctrl/alt combos */
          if (e.type == SDL_KEYDOWN && (e.key.keysym.mod & (KMOD_CTRL | KMOD_ALT))) {
            SDL_StopTextInput();
          } else if (e.type == SDL_KEYUP) {
            SDL_StartTextInput();
          }

          int c = key_map[e.key.keysym.sym & 0xff];
          if (c && e.type == SDL_KEYDOWN) { mu_input_keydown(ctx, c); }
          if (c && e.type ==   SDL_KEYUP) { mu_input_keyup(ctx, c);   }

          if (e.type == SDL_KEYDOWN) {
            int ctrl = !!(e.key.keysym.mod & KMOD_CTRL);
            int cmd  = !!(e.key.keysym.mod & KMOD_GUI);

            /* Cmd+F: toggle search */
            if (cmd && e.key.keysym.sym == SDLK_f) {
              search_active = !search_active;
              if (search_active) {
                search_buf[0] = '\0';
                search_len = 0;
                search_match_line = -1;
              }
              break;
            }

            /* search mode key handling */
            if (search_active) {
              if (e.key.keysym.sym == SDLK_ESCAPE) {
                search_active = 0;
                search_match_line = -1;
              }
              else if (e.key.keysym.sym == SDLK_RETURN) {
                /* jump to next match */
                if (search_match_line >= 0) {
                  search_find_next(search_match_line, search_match_col);
                } else {
                  search_find_first();
                }
              }
              else if (e.key.keysym.sym == SDLK_BACKSPACE) {
                if (search_len > 0) {
                  search_len--;
                  search_buf[search_len] = '\0';
                  if (search_len > 0) search_find_first();
                  else search_match_line = -1;
                }
              }
              break;
            }
            int alt  = !!(e.key.keysym.mod & KMOD_ALT);
            int sym = e.key.keysym.sym;

            /* any non-ctrl/alt key clears last_kill_was_k */
            if (!(ctrl && sym == SDLK_k)) last_kill_was_k = 0;

            /* --- emacs ctrl bindings --- */
            if (ctrl && sym == SDLK_a) {
              /* beginning of line */
              cursor_col = 0;
              cursor_target_col = 0;
              ensure_cursor_visible();
            }
            else if (ctrl && sym == SDLK_e) {
              /* end of line */
              cursor_col = lines[cursor_line].len;
              cursor_target_col = cursor_col;
              ensure_cursor_visible();
            }
            else if (ctrl && sym == SDLK_f) {
              /* forward char */
              if (cursor_col < lines[cursor_line].len) {
                cursor_col++;
              } else if (cursor_line < line_count - 1) {
                cursor_line++; cursor_col = 0;
              }
              cursor_target_col = cursor_col;
              ensure_cursor_visible();
            }
            else if (ctrl && sym == SDLK_b) {
              /* backward char */
              if (cursor_col > 0) {
                cursor_col--;
              } else if (cursor_line > 0) {
                cursor_line--;
                cursor_col = lines[cursor_line].len;
              }
              cursor_target_col = cursor_col;
              ensure_cursor_visible();
            }
            else if (ctrl && sym == SDLK_n) {
              /* next line */
              if (cursor_line < line_count - 1) {
                cursor_line++;
                cursor_col = cursor_target_col;
                cursor_clamp();
              }
              ensure_cursor_visible();
            }
            else if (ctrl && sym == SDLK_p) {
              /* previous line */
              if (cursor_line > 0) {
                cursor_line--;
                cursor_col = cursor_target_col;
                cursor_clamp();
              }
              ensure_cursor_visible();
            }
            else if (ctrl && sym == SDLK_k) {
              /* kill line */
              mark_clear();
              undo_push();
              emacs_kill_line();
              ensure_cursor_visible();
            }
            else if (ctrl && sym == SDLK_y) {
              /* yank */
              mark_clear();
              undo_push();
              emacs_yank();
              ensure_cursor_visible();
            }
            else if (ctrl && sym == SDLK_w) {
              /* kill region */
              undo_push();
              emacs_kill_region();
              ensure_cursor_visible();
            }
            else if (ctrl && sym == SDLK_SLASH) {
              /* undo */
              mark_clear();
              editor_undo();
              ensure_cursor_visible();
            }
            else if (ctrl && sym == SDLK_SPACE) {
              /* set mark */
              mark_set();
            }
            else if (ctrl && sym == SDLK_g) {
              /* cancel: clear mark */
              mark_clear();
            }
            /* --- alt bindings --- */
            else if (alt && sym == SDLK_f) {
              emacs_forward_word();
              ensure_cursor_visible();
            }
            else if (alt && sym == SDLK_b) {
              emacs_backward_word();
              ensure_cursor_visible();
            }
            /* --- arrow keys (still work) --- */
            else if (sym == SDLK_LEFT) {
              if (cursor_col > 0) cursor_col--;
              else if (cursor_line > 0) { cursor_line--; cursor_col = lines[cursor_line].len; }
              cursor_target_col = cursor_col;
              ensure_cursor_visible();
            }
            else if (sym == SDLK_RIGHT) {
              if (cursor_col < lines[cursor_line].len) cursor_col++;
              else if (cursor_line < line_count - 1) { cursor_line++; cursor_col = 0; }
              cursor_target_col = cursor_col;
              ensure_cursor_visible();
            }
            else if (sym == SDLK_UP) {
              if (cursor_line > 0) { cursor_line--; cursor_col = cursor_target_col; cursor_clamp(); }
              ensure_cursor_visible();
            }
            else if (sym == SDLK_DOWN) {
              if (cursor_line < line_count - 1) { cursor_line++; cursor_col = cursor_target_col; cursor_clamp(); }
              ensure_cursor_visible();
            }
            /* --- cmd bindings (macOS) --- */
            else if (cmd && sym == SDLK_EQUALS) {
              font_size += 2.0f;
              if (font_size > 72.0f) font_size = 72.0f;
              r_set_font_size(font_size);
              invalidate_all_wraps();
              ensure_cursor_visible();
            }
            else if (cmd && sym == SDLK_MINUS) {
              font_size -= 2.0f;
              if (font_size < 8.0f) font_size = 8.0f;
              r_set_font_size(font_size);
              invalidate_all_wraps();
              ensure_cursor_visible();
            }
            else if (ctrl && sym == SDLK_s) {
              if (argc >= 2) {
                FILE *f = fopen(argv[1], "wb");
                if (f) {
                  for (int i = 0; i < line_count; i++) {
                    fwrite(lines[i].text, 1, lines[i].len, f);
                    if (i < line_count - 1) fwrite("\n", 1, 1, f);
                  }
                  fclose(f);
                  printf("saved %s (%d lines)\n", argv[1], line_count);
                }
              }
            }
            /* --- basic editing keys --- */
            else if (sym == SDLK_BACKSPACE) {
              mark_clear();
              undo_push();
              editor_backspace();
              ensure_cursor_visible();
            }
            else if (sym == SDLK_DELETE) {
              mark_clear();
              undo_push();
              editor_delete();
            }
            else if (sym == SDLK_RETURN) {
              mark_clear();
              undo_push();
              editor_enter();
              ensure_cursor_visible();
            }
            else if (sym == SDLK_ESCAPE) {
              mark_clear();
            }
          }
          break;
        }
      }
    }

    do_render();
  }

  return 0;
}
