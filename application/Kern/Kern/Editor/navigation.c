/* navigation.c — Window metrics, word wrapping, cursor navigation, search */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>  /* strncasecmp */
#include <SDL2/SDL.h>
#include "navigation.h"
#include "renderer.h"
#include "buffer.h"
#include "clock.h"

/* ---- window / page metrics ---- */

int nav_win_w(void) { int w, h; r_get_size(&w, &h); return w; }
int nav_win_h(void) { int w, h; r_get_size(&w, &h); return h; }

int nav_page_w(void) {
  int ideal = r_get_text_width("n", 1) * CHARS_PER_LINE;
  int max_w = nav_win_w() - 2 * MIN_MARGIN;
  return ideal < max_w ? ideal : max_w;
}

int nav_page_margin(void) {
  int pw = nav_page_w();
  int m = (nav_win_w() - pw) / 2;
  return m > MIN_MARGIN ? m : MIN_MARGIN;
}

int nav_line_height(void) {
  return (int)(r_get_text_height() * LINE_HEIGHT_MULT + 0.5f);
}

/* ---- word wrapping ---- */

int nav_get_wrap_breaks(Line *l, int *starts, int max_starts) {
  starts[0] = 0;
  if (l->len == 0) return 1;

  int count = 1;
  int x = 0;
  int last_space = -1;
  int row_start = 0;
  int i = 0;

  while (i < l->len && count < max_starts) {
    int cw = r_get_text_width(l->text + i, 1);

    if (x + cw > nav_page_w() && i > row_start) {
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

int nav_count_wraps(Line *l) {
  if (l->wrap_count >= 0) return l->wrap_count;
  int starts[256];
  l->wrap_count = nav_get_wrap_breaks(l, starts, 256);
  return l->wrap_count;
}

int nav_total_visual_lines(EditorState *ed) {
  int total = 0;
  for (int i = 0; i < ed->line_count; i++) {
    total += nav_count_wraps(&ed->lines[i]);
  }
  return total;
}

int nav_maybe_reflow(EditorState *ed, ViewState *vs) {
  int pw = nav_page_w();
  if (pw == vs->wrap_page_w) return 0;     /* width unchanged → wraps still valid */
  buf_invalidate_all_wraps(ed);
  vs->wrap_page_w = pw;
  return 1;
}

/* ---- coordinate mapping ---- */

int nav_visual_to_logical(EditorState *ed, int visual_line, int *visual_offset) {
  int accum = 0;
  for (int i = 0; i < ed->line_count; i++) {
    int wc = nav_count_wraps(&ed->lines[i]);
    if (accum + wc > visual_line) {
      *visual_offset = visual_line - accum;
      return i;
    }
    accum += wc;
  }
  *visual_offset = 0;
  return ed->line_count - 1;
}

int nav_logical_to_visual(EditorState *ed, int logical_line) {
  int accum = 0;
  for (int i = 0; i < logical_line && i < ed->line_count; i++) {
    accum += nav_count_wraps(&ed->lines[i]);
  }
  return accum;
}

int nav_cursor_to_visual(EditorState *ed, int cline, int ccol) {
  int base = nav_logical_to_visual(ed, cline);
  int starts[256];
  int nrows = nav_get_wrap_breaks(&ed->lines[cline], starts, 256);
  for (int r = nrows - 1; r >= 0; r--) {
    if (ccol >= starts[r]) return base + r;
  }
  return base;
}

/* ---- cursor ---- */

void nav_cursor_clamp(EditorState *ed) {
  if (ed->cursor_line < 0) ed->cursor_line = 0;
  if (ed->cursor_line >= ed->line_count) ed->cursor_line = ed->line_count - 1;
  if (ed->cursor_col < 0) ed->cursor_col = 0;
  if (ed->cursor_col > ed->lines[ed->cursor_line].len) ed->cursor_col = ed->lines[ed->cursor_line].len;
}

void nav_ensure_cursor_visible(EditorState *ed, ViewState *vs) {
  int lh = nav_line_height();
  int vis = nav_cursor_to_visual(ed, ed->cursor_line, ed->cursor_col);
  float cursor_top = vis * lh;
  float cursor_bot = cursor_top + lh;
  int view_h = vs->content_h > 0 ? vs->content_h : nav_win_h();
  if (cursor_top < vs->scroll_y) vs->scroll_y = cursor_top;
  if (cursor_bot > vs->scroll_y + view_h) vs->scroll_y = cursor_bot - view_h;
}

/* ---- click to cursor ---- */

void nav_click_to_cursor(EditorState *ed, ViewState *vs, int mx, int my) {
  int lh = nav_line_height();
  int rel_y = my - vs->content_y + (int)vs->scroll_y;
  int vis_line = rel_y / lh;
  if (vis_line < 0) vis_line = 0;

  int total = nav_total_visual_lines(ed);
  if (vis_line >= total) vis_line = total - 1;

  int wrap_offset;
  int ln = nav_visual_to_logical(ed, vis_line, &wrap_offset);

  int starts[256];
  int nrows = nav_get_wrap_breaks(&ed->lines[ln], starts, 256);
  int row_start = starts[wrap_offset];
  int row_end = (wrap_offset + 1 < nrows) ? starts[wrap_offset + 1] : ed->lines[ln].len;

  int col = row_start;
  int pm = nav_page_margin();
  if (mx > pm) {
    int px = pm;
    for (int i = row_start; i < row_end; i++) {
      int cw = r_get_text_width(ed->lines[ln].text + i, 1);
      if (px + cw / 2 > mx) break;
      px += cw;
      col = i + 1;
    }
  }

  ed->cursor_line = ln;
  ed->cursor_col = col;
  nav_cursor_clamp(ed);
  ed->cursor_target_col = ed->cursor_col;
}

/* ---- search ---- */

void nav_search_find_next(EditorState *ed, ViewState *vs, int from_line, int from_col) {
  if (vs->search_len == 0) { vs->search_match_line = -1; return; }
  for (int pass = 0; pass < 2; pass++) {
    int start_ln = (pass == 0) ? from_line : 0;
    int end_ln   = (pass == 0) ? ed->line_count : from_line + 1;
    for (int ln = start_ln; ln < end_ln; ln++) {
      Line *l = &ed->lines[ln];
      int start_col = (pass == 0 && ln == from_line) ? from_col + 1 : 0;
      if (l->len < vs->search_len) continue;
      for (int c = start_col; c <= l->len - vs->search_len; c++) {
        if (strncasecmp(l->text + c, vs->search_buf, vs->search_len) == 0) {
          vs->search_match_line = ln;
          vs->search_match_col = c;
          ed->cursor_line = ln;
          ed->cursor_col = c;
          ed->cursor_target_col = c;
          nav_ensure_cursor_visible(ed, vs);
          return;
        }
      }
    }
  }
  vs->search_match_line = -1;
}

void nav_search_find_prev(EditorState *ed, ViewState *vs, int from_line, int from_col) {
  if (vs->search_len == 0) { vs->search_match_line = -1; return; }
  for (int pass = 0; pass < 2; pass++) {
    int start_ln = (pass == 0) ? from_line : ed->line_count - 1;
    int end_ln   = (pass == 0) ? -1 : from_line - 1;
    for (int ln = start_ln; ln > end_ln; ln--) {
      Line *l = &ed->lines[ln];
      if (l->len < vs->search_len) continue;
      int max_col = l->len - vs->search_len;
      int start_col = (pass == 0 && ln == from_line) ? from_col - 1 : max_col;
      if (start_col > max_col) start_col = max_col;
      for (int c = start_col; c >= 0; c--) {
        if (strncasecmp(l->text + c, vs->search_buf, vs->search_len) == 0) {
          vs->search_match_line = ln;
          vs->search_match_col = c;
          ed->cursor_line = ln;
          ed->cursor_col = c;
          ed->cursor_target_col = c;
          nav_ensure_cursor_visible(ed, vs);
          return;
        }
      }
    }
  }
  vs->search_match_line = -1;
}

void nav_search_find_first(EditorState *ed, ViewState *vs) {
  if (vs->search_direction == 1)
    nav_search_find_next(ed, vs, 0, -1);
  else
    nav_search_find_prev(ed, vs, ed->line_count - 1, ed->lines[ed->line_count - 1].len);
}

void nav_search_find_current_dir(EditorState *ed, ViewState *vs) {
  if (vs->search_direction == 1) {
    if (vs->search_match_line >= 0)
      nav_search_find_next(ed, vs, vs->search_match_line, vs->search_match_col);
    else
      nav_search_find_next(ed, vs, ed->cursor_line, ed->cursor_col - 1);
  } else {
    if (vs->search_match_line >= 0)
      nav_search_find_prev(ed, vs, vs->search_match_line, vs->search_match_col);
    else
      nav_search_find_prev(ed, vs, ed->cursor_line, ed->cursor_col + 1);
  }
}

/* ---- status ---- */

void nav_status_set(ViewState *vs, const char *msg) {
  snprintf(vs->status_msg, sizeof(vs->status_msg), "%s", msg);
  vs->status_time = kern_now_ms();
}

const char *nav_status_get(EditorState *ed, ViewState *vs) {
  if (vs->minibuf_active) return vs->minibuf_prompt;
  if (vs->esc_prefix) return "ESC-";
  if (vs->ctrl_x_prefix) return "C-x -";
  if (ed->mark_active) return "Mark active";
  if (vs->search_active) return (vs->search_direction == 1) ? "I-search:" : "I-search backward:";
  if (vs->status_msg[0] && (kern_now_ms() - vs->status_time) < STATUS_DURATION) return vs->status_msg;
  return "";
}
