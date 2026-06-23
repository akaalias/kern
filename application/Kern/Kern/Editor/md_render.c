/* md_render.c — Markdown-aware text rendering */

#include <string.h>
#include "md_render.h"
#include "renderer.h"

/* base indent shared by every list level; nesting is added on top by the line's
   own leading whitespace, which is drawn as literal text */
int md_list_indent(Line *l) {
  int i = 0;
  while (i < l->len && (l->text[i] == ' ' || l->text[i] == '\t')) i++;
  if (i + 1 < l->len && l->text[i] == '-' && l->text[i+1] == ' ') {
    return r_get_text_width("    ", 4);
  }
  int d = i;
  while (d < l->len && l->text[d] >= '0' && l->text[d] <= '9') d++;
  if (d > i && d + 1 < l->len && l->text[d] == '.' && l->text[d+1] == ' ') {
    return r_get_text_width("    ", 4);
  }
  return 0;
}

/* pixel width of a list item's marker — including any leading indentation
   whitespace, so wrapped continuation rows hang under the item text. 0 if not
   a list item. */
int md_list_marker_width(Line *l) {
  int i = 0;
  while (i < l->len && (l->text[i] == ' ' || l->text[i] == '\t')) i++;
  if (i + 1 < l->len && l->text[i] == '-' && l->text[i+1] == ' ') {
    return r_get_text_width(l->text, i + 2);  /* leading ws + "- " */
  }
  int d = i;
  while (d < l->len && l->text[d] >= '0' && l->text[d] <= '9') d++;
  if (d > i && d + 1 < l->len && l->text[d] == '.' && l->text[d+1] == ' ') {
    return r_get_text_width(l->text, d + 2);  /* leading ws + digits + ". " */
  }
  return 0;
}

int md_is_list_item(Line *l) {
  int i = 0;
  while (i < l->len && (l->text[i] == ' ' || l->text[i] == '\t')) i++;
  if (i + 1 < l->len && l->text[i] == '-' && l->text[i+1] == ' ') return 1;
  int d = i;
  while (d < l->len && l->text[d] >= '0' && l->text[d] <= '9') d++;
  if (d > i && d + 1 < l->len && l->text[d] == '.' && l->text[d+1] == ' ') return 1;
  return 0;
}

int md_is_heading(Line *l) {
  if (l->len < 2) return 0;
  int i = 0;
  while (i < l->len && l->text[i] == '#') i++;
  return (i > 0 && i < l->len && l->text[i] == ' ');
}

int md_heading_prefix_len(Line *l) {
  if (!md_is_heading(l)) return 0;
  int i = 0;
  while (i < l->len && l->text[i] == '#') i++;
  return i + 1;  /* the hashes plus the single space after them */
}

/* ---- inline markdown formatting ---- */

/* inline span kinds */
enum { SP_NONE = 0, SP_BOLD, SP_ITALIC, SP_MONO, SP_HL, SP_LINK, SP_WIKI };

/* max chars to scan forward for a closing delimiter (bounds O(n²) on
   pathological input) */
#define MD_MAX_SCAN 1024

/* If an inline span opens at position i, return its kind and fill the span's
   content/close/end boundaries; otherwise SP_NONE. Closing delimiters are
   searched up to line_len, so a span may cross visual-row boundaries.
   content..close is the styled inner text; [i,content) and [close,end) are the
   delimiter runs. For links the whole token is "content" (no inner markers). */
static int md_detect_span(const char *t, int i, int line_len,
                          int *content, int *close, int *end) {
  int lim = i + MD_MAX_SCAN;
  if (lim > line_len) lim = line_len;

  /* **bold** */
  if (i + 1 < line_len && t[i] == '*' && t[i+1] == '*') {
    for (int j = i + 2; j + 1 < lim; j++)
      if (t[j] == '*' && t[j+1] == '*') {
        *content = i + 2; *close = j; *end = j + 2; return SP_BOLD;
      }
  }
  /* ==highlight== */
  if (i + 1 < line_len && t[i] == '=' && t[i+1] == '=') {
    for (int j = i + 2; j + 1 < lim; j++)
      if (t[j] == '=' && t[j+1] == '=') {
        *content = i + 2; *close = j; *end = j + 2; return SP_HL;
      }
  }
  /* `code` */
  if (t[i] == '`') {
    for (int j = i + 1; j < lim; j++)
      if (t[j] == '`') { *content = i + 1; *close = j; *end = j + 1; return SP_MONO; }
  }
  /* _italic_ (non-empty, not opening on a space) */
  if (t[i] == '_' && i + 1 < line_len && t[i+1] != ' ') {
    for (int j = i + 1; j < lim; j++)
      if (t[j] == '_') {
        if (j > i + 1) { *content = i + 1; *close = j; *end = j + 1; return SP_ITALIC; }
        break;
      }
  }
  /* [[wikilink]] */
  if (t[i] == '[' && i + 1 < line_len && t[i+1] == '[') {
    for (int j = i + 2; j + 1 < lim; j++)
      if (t[j] == ']' && t[j+1] == ']') {
        *content = i; *close = j + 2; *end = j + 2; return SP_WIKI;
      }
  }
  /* [text](url) */
  if (t[i] == '[') {
    int br = -1;
    for (int j = i + 1; j < lim; j++) if (t[j] == ']') { br = j; break; }
    if (br > 0 && br + 1 < line_len && t[br+1] == '(') {
      int lim2 = br + 2 + MD_MAX_SCAN;
      if (lim2 > line_len) lim2 = line_len;
      for (int j = br + 2; j < lim2; j++)
        if (t[j] == ')') { *content = i; *close = j + 1; *end = j + 1; return SP_LINK; }
    }
  }
  return SP_NONE;
}

/* Draw (or, when draw==0, just measure) the visual-row window [start,end) of a
   logical line. Inline spans are parsed from the logical-line start (0..line_len)
   so formatting that wraps across rows still applies. Renders one character at a
   time, so the active font carries across the row boundary.
     track_cursor_col: if in [start,end] the pen x at that column is written to
     *out_cursor_x. Returns the pen x after the window. */
float md_draw_text(const char *text, int start, int end, int line_len,
                   float x, float y, mu_Color base_color, int heading,
                   int track_cursor_col, int *out_cursor_x, int draw) {
  int saved_style = r_get_font_style();
  int base_style  = heading ? FONT_BOLD : FONT_REGULAR;
  int font_h      = r_get_text_height();

  mu_Color dim     = mu_color(80, 80, 80, 255);
  mu_Color link_fg = mu_color(180, 160, 220, 255);
  mu_Color link_bg = mu_color(80, 50, 120, 255);
  mu_Color code_fg = mu_color(180, 140, 100, 255);
  mu_Color hl_bg   = mu_color(240, 214, 92, 70);   /* soft highlighter yellow */
  static const int wave[4] = { 0, 1, 2, 1 };       /* hand-drawn wobble */

  int mode = SP_NONE, content = 0, close = 0, span_end = 0;
  float px = x;

  for (int i = 0; i < end; i++) {
    if (mode == SP_NONE) {
      int sp = md_detect_span(text, i, line_len, &content, &close, &span_end);
      if (sp) mode = sp;
    }

    /* attributes for the character at i */
    int style = base_style;
    mu_Color fg = base_color;
    int bg = 0;   /* 0 none, 1 link, 2 highlight */
    if (mode != SP_NONE) {
      if (mode == SP_LINK || mode == SP_WIKI) {
        fg = link_fg; bg = 1;
      } else if (i < content || i >= close) {
        fg = dim;   /* the delimiter characters themselves */
      } else {
        switch (mode) {
          case SP_BOLD:   style = FONT_BOLD; break;
          case SP_ITALIC: style = heading ? FONT_BOLD : FONT_ITALIC; break;
          case SP_MONO:   style = FONT_MONO; fg = code_fg; break;
          case SP_HL:     bg = 2; break;
        }
      }
    }

    if (i == track_cursor_col && i >= start) *out_cursor_x = (int)px;

    if (i >= start) {
      r_set_font_style(style);
      char ch[2] = { text[i], '\0' };
      int w = r_get_text_width(ch, 1);
      if (draw) {
        if (bg == 1) {
          r_draw_rect(mu_rect((int)px, (int)y, w, font_h), link_bg);
        } else if (bg == 2) {
          int woff = wave[i & 3];
          r_draw_rect(mu_rect((int)px, (int)y + woff, w, font_h - 1), hl_bg);
        }
        r_draw_text(ch, mu_vec2((int)px, (int)y), fg);
      }
      px += w;
    }

    if (mode != SP_NONE && i + 1 >= span_end) mode = SP_NONE;
  }

  if (track_cursor_col >= end) *out_cursor_x = (int)px;

  r_set_font_style(saved_style);
  return px;
}

int md_col_x(const char *text, int start, int end, int line_len,
             int x0, int heading, int col) {
  int out = x0;
  md_draw_text(text, start, end, line_len, (float)x0, 0.0f, mu_color(0, 0, 0, 0),
               heading, col, &out, 0 /* measure only */);
  return out;
}
