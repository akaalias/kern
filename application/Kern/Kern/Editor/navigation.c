/* navigation.c — Window metrics, word wrapping, cursor navigation, search */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>  /* strncasecmp */
#include <SDL2/SDL.h>
#include "navigation.h"
#include "renderer.h"
#include "md_render.h"
#include "sub_render.h"
#include "buffer.h"
#include "clock.h"
#include "utf8.h"

/* ---- window / page metrics ---- */

int nav_win_w(void) { int w, h; r_get_size(&w, &h); return w; }
int nav_win_h(void) { int w, h; r_get_size(&w, &h); return h; }

/* Wrap and column math must measure in the body font (FONT_REGULAR) no matter
   which style the renderer was last left in. The frame ends with the status
   bar's FONT_MONO active, so an event-time wrap recompute (e.g. ensure-cursor-
   visible after an edit) would otherwise cache mono-measured wrap counts that
   disagree with the FONT_REGULAR render pass — producing phantom wrap rows and
   making click-to-cursor land in the wrong place. */
static int body_width(const char *s, int len) {
  int body = md_wrap_font_style();   /* FONT_REGULAR, or FONT_MONO in typewriter mode */
  int saved = r_get_font_style();
  if (saved != body) r_set_font_style(body);
  int w = r_get_text_width(s, len);
  if (saved != body) r_set_font_style(saved);
  return w;
}

int nav_page_w(void) {
  int ideal = body_width("n", 1) * CHARS_PER_LINE;
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

/* True if a heading's "### " markers fit in the left margin (so do_render hangs
   them there and draws the body flush at the margin). False in a narrow window,
   where they render inline. Kept here so click/movement geometry can mirror the
   render decision; do_render calls it too. */
int nav_heading_markers_hang(Line *l) {
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

/* ---- word wrapping ---- */

int nav_get_wrap_breaks(Line *l, int *starts, int max_starts) {
  starts[0] = 0;
  if (l->len == 0) return 1;

  /* measure the whole line in the body font (see body_width above) */
  int body_style = md_wrap_font_style();
  int saved_style = r_get_font_style();
  if (saved_style != body_style) r_set_font_style(body_style);

  int count = 1;
  int x = 0;
  int last_space = -1;
  int row_start = 0;
  int i = 0;

  /* available text width shrinks by the row's hanging indent, exactly as the
     render pass draws each row at nav_page_margin() + md_row_indent(): a list
     item's continuation rows hang under the item text, so they fit fewer
     characters. Measuring the full page width here (the old behaviour) put the
     wrap points past the right edge and out of step with what's drawn. */
  int avail = nav_page_w() - md_row_indent(l, row_start);

  /* Wrap measures the *literal* text width, independent of symbol substitution:
     a collapsed glyph (→) is never wider than its source (->), so a literal-width
     wrap point always still fits when drawn collapsed, and — crucially — the line
     does not reflow as the caret enters a token and reveals it (reveal-on-contact
     redraws the literal, which is exactly the width wrap already reserved). */
  while (i < l->len && count < max_starts) {
    int n = utf8_len(l->text + i, l->len - i);   /* step whole codepoints */
    int cw = r_get_text_width(l->text + i, n);

    if (x + cw > avail && i > row_start) {
      int brk = i;
      if (last_space > row_start) brk = last_space + 1;
      starts[count] = brk;
      count++;
      row_start = brk;
      i = brk;
      x = 0;
      last_space = -1;
      avail = nav_page_w() - md_row_indent(l, row_start);
      continue;
    }

    if (l->text[i] == ' ') last_space = i;       /* spaces are ASCII */
    x += cw;
    i += n;
  }

  if (saved_style != body_style) r_set_font_style(saved_style);
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
  Line *l = &ed->lines[ed->cursor_line];
  if (ed->cursor_col > l->len) ed->cursor_col = l->len;
  /* never rest in the middle of a multibyte codepoint (e.g. after a vertical
     move or search jump landed on a target byte): snap back to its start */
  while (ed->cursor_col > 0 && ((unsigned char)l->text[ed->cursor_col] & 0xC0) == 0x80)
    ed->cursor_col--;
}

float nav_pin_target(EditorState *ed, ViewState *vs, float fraction) {
  int lh = nav_line_height();
  int view_h = vs->content_h > 0 ? vs->content_h : nav_win_h();
  float cursor_top = nav_cursor_to_visual(ed, ed->cursor_line, ed->cursor_col) * lh;
  /* May be negative for the first lines: that's virtual whitespace above the top
     so the first line can still pin at the golden height. process_frame clamps
     it to a valid range (0 outside typewriter mode). */
  return cursor_top - (int)((view_h - lh) * fraction);
}

void nav_pin_cursor(EditorState *ed, ViewState *vs, float fraction) {
  float t = nav_pin_target(ed, vs, fraction);   /* recenter floors at the top — no virtual whitespace */
  vs->scroll_y = vs->scroll_target_y = t < 0 ? 0 : t;
}

void nav_ensure_cursor_visible(EditorState *ed, ViewState *vs) {
  /* Typewriter mode only moves the *target*; process_frame eases scroll_y to it
     for a smooth glide instead of snapping. */
  if (vs->typewriter_mode) { vs->scroll_target_y = nav_pin_target(ed, vs, TYPEWRITER_FRACTION); return; }
  int lh = nav_line_height();
  int vis = nav_cursor_to_visual(ed, ed->cursor_line, ed->cursor_col);
  float cursor_top = vis * lh;
  float cursor_bot = cursor_top + lh;
  int view_h = vs->content_h > 0 ? vs->content_h : nav_win_h();
  if (cursor_top < vs->scroll_y) vs->scroll_y = cursor_top;
  if (cursor_bot > vs->scroll_y + view_h) vs->scroll_y = cursor_bot - view_h;
}

/* ---- visual-row geometry (mirrors do_render's per-row layout) ---- */

/* For visual row starting at `row_start` of logical line `ln`, compute the
   draw origin x0, the first drawable column (a heading's hung "### " prefix
   sits in the margin, so its body starts after the prefix), and whether the
   line is a heading. This is exactly the layout do_render uses, so column<->x
   math agrees with what's on screen. */
static void nav_row_geom(EditorState *ed, int ln, int row_start,
                         int *x0, int *draw_start, int *heading) {
  Line *l = &ed->lines[ln];
  int h = md_is_heading(l);
  int indent = md_row_indent(l, row_start);
  int ds = row_start;
  if (h && row_start == 0 && nav_heading_markers_hang(l)) {
    int prefix = md_heading_prefix_len(l);
    if (prefix <= l->len) ds = prefix;   /* markers hung in the left margin */
  }
  *x0 = nav_page_margin() + indent;
  *draw_start = ds;
  *heading = h;
}

/* Publish the symbol reveal range for logical line `ln`: the caret point (when
   the caret is on it), unioned with the active selection's extent on that line, so
   render and measurement collapse/reveal the same tokens and a selected symbol
   stays expanded. Call before any md_draw_text / md_col_x / md_x_to_col for `ln`. */
void nav_sub_reveal_for_line(EditorState *ed, int ln) {
  int has = 0, lo = 0, hi = 0;
  if (ln == ed->cursor_line) { lo = hi = ed->cursor_col; has = 1; }
  if (ed->mark_active) {
    int sl, sc, el, ec;
    buf_region_ordered(ed, &sl, &sc, &el, &ec);
    if (ln >= sl && ln <= el) {
      int rlo = (ln == sl) ? sc : 0;
      int rhi = (ln == el) ? ec : ed->lines[ln].len;
      if (!has) { lo = rlo; hi = rhi; has = 1; }
      else { if (rlo < lo) lo = rlo; if (rhi > hi) hi = rhi; }
    }
  }
  sub_set_reveal(has ? &ed->lines[ln] : NULL, lo, hi);
}

int nav_cursor_x(EditorState *ed, int line, int col) {
  nav_sub_reveal_for_line(ed, line);   /* reveal the caret/selection tokens on this line */
  Line *l = &ed->lines[line];
  int starts[256];
  int nrows = nav_get_wrap_breaks(l, starts, 256);
  int row = 0;
  for (int r = nrows - 1; r >= 0; r--) { if (col >= starts[r]) { row = r; break; } }
  int row_start = starts[row];
  int row_end = (row + 1 < nrows) ? starts[row + 1] : l->len;
  int x0, draw_start, heading;
  nav_row_geom(ed, line, row_start, &x0, &draw_start, &heading);
  int ms = draw_start > row_start ? draw_start : row_start;
  return md_col_x(l, ms, row_end, x0, heading, col);
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

  int x0, draw_start, heading;
  nav_row_geom(ed, ln, row_start, &x0, &draw_start, &heading);
  if (draw_start > row_start) row_start = draw_start;
  /* measure with the same per-span / hanging-indent metrics the row is drawn
     with — body_width(FONT_REGULAR) from the bare page margin used to ignore
     both, so clicks landed offset on list/heading/bold lines. */
  nav_sub_reveal_for_line(ed, ln);   /* match the clicked line's collapsed/revealed state */
  /* the text pane is translated by -scroll_x in typewriter mode (caret pinned at
     page center); x0 is the natural (unshifted) origin, so map against the click
     x shifted back into natural coords. */
  int col = md_x_to_col(&ed->lines[ln], row_start, row_end, x0, heading, mx + (int)vs->scroll_x);

  ed->cursor_line = ln;
  ed->cursor_col = col;
  nav_cursor_clamp(ed);
  ed->cursor_target_col = ed->cursor_col;
  vs->goal_line = -1;   /* horizontal motion: drop any vertical goal column */
}

int nav_at_right_margin(EditorState *ed, const char *add) {
  Line *l = &ed->lines[ed->cursor_line];
  int saved = r_get_font_style();
  r_set_font_style(md_wrap_font_style());          /* measure like the wrap metric */
  int line_w = r_get_text_width(l->text, l->len);
  int add_w  = add ? r_get_text_width(add, (int)strlen(add)) : 0;
  r_set_font_style(saved);
  int avail = nav_page_w() - md_row_indent(l, 0);
  return line_w + add_w > avail;
}

/* ---- vertical movement by visual row ---- */

void nav_visual_move(EditorState *ed, ViewState *vs, int dir) {
  int total = nav_total_visual_lines(ed);
  int cur_vis = nav_cursor_to_visual(ed, ed->cursor_line, ed->cursor_col);
  int target_vis = cur_vis + dir;

  /* (re)establish the goal x unless the caret is still exactly where the last
     vertical move left it — i.e. this is a run of consecutive C-n/C-p, so the
     original goal column should persist across short rows. */
  if (ed->cursor_line != vs->goal_line || ed->cursor_col != vs->goal_col)
    vs->goal_x = nav_cursor_x(ed, ed->cursor_line, ed->cursor_col);

  if (target_vis < 0) {                       /* above first row → buffer start */
    ed->cursor_line = 0;
    ed->cursor_col = 0;
  } else if (target_vis >= total) {           /* below last row → buffer end */
    ed->cursor_line = ed->line_count - 1;
    ed->cursor_col = ed->lines[ed->cursor_line].len;
  } else {
    int wrap_off;
    int ln = nav_visual_to_logical(ed, target_vis, &wrap_off);
    int starts[256];
    int nrows = nav_get_wrap_breaks(&ed->lines[ln], starts, 256);
    int row_start = starts[wrap_off];
    int row_end = (wrap_off + 1 < nrows) ? starts[wrap_off + 1] : ed->lines[ln].len;
    int x0, draw_start, heading;
    nav_row_geom(ed, ln, row_start, &x0, &draw_start, &heading);
    int ms = draw_start > row_start ? draw_start : row_start;
    nav_sub_reveal_for_line(ed, ln);   /* match the target line's collapsed/revealed state */
    ed->cursor_line = ln;
    ed->cursor_col = md_x_to_col(&ed->lines[ln], ms, row_end, x0, heading, vs->goal_x);
  }

  nav_cursor_clamp(ed);
  ed->cursor_target_col = ed->cursor_col;
  vs->goal_line = ed->cursor_line;   /* remember where we landed so a follow-up */
  vs->goal_col = ed->cursor_col;     /* C-n/C-p keeps the same goal_x */
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
