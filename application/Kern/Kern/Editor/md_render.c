/* md_render.c — Markdown-aware text rendering */

#include <stdlib.h>
#include <string.h>
#include "md_render.h"
#include "renderer.h"
#include "utf8.h"

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

/* a cached inline span: kind + the boundaries from md_detect_span.
   [open,content) and [close,end) are the delimiter runs; [content,close) is the
   styled inner text (the whole token for links). */
struct MdSpan { int kind, open, content, close, end; };

/* Scan a whole line into its inline spans in one pass — detection resumes after
   each span (no nesting), matching md_draw_text's draw order. With out==NULL
   this only counts, so callers can size an exact allocation. */
static int md_scan_spans(const char *t, int len, struct MdSpan *out) {
  int n = 0, i = 0;
  while (i < len) {
    int content, close, end;
    int kind = md_detect_span(t, i, len, &content, &close, &end);
    if (kind) {
      if (out) {
        out[n].kind = kind; out[n].open = i; out[n].content = content;
        out[n].close = close; out[n].end = end;
      }
      n++;
      i = end;
    } else {
      i++;
    }
  }
  return n;
}

/* Lazily compute and cache the line's span map (recomputed when md_span_count is
   -1, which line_dirty sets on every edit). Returns the count; *out gets the
   array. This replaces re-parsing the line for each wrapped visual row. */
static int md_line_spans(Line *l, const struct MdSpan **out) {
  if (l->md_span_count < 0) {
    free(l->md_spans);
    l->md_spans = NULL;
    int n = md_scan_spans(l->text, l->len, NULL);
    if (n > 0) {
      l->md_spans = malloc((size_t)n * sizeof(struct MdSpan));
      if (l->md_spans) md_scan_spans(l->text, l->len, l->md_spans);
      else n = 0;
    }
    l->md_span_count = n;
  }
  *out = l->md_spans;
  return l->md_span_count;
}

/* Draw (or, when draw==0, just measure) the visual-row window [start,end) of a
   line, using its cached inline-span map so formatting carries across wrapped
   rows. Renders one character at a time.
     track_cursor_col: if in [start,end] the pen x at that column is written to
     *out_cursor_x. Returns the pen x after the window. */
/* Global focus dim (typewriter mode): scales the alpha of everything drawn so
   non-focused lines fade toward the background. 1.0 = full opacity (default).
   A module global rather than a parameter so the measure-only and test callers
   don't have to thread it through. */
static float g_text_opacity = 1.0f;
void md_set_text_opacity(float o) { g_text_opacity = o; }

static Color md_fade(Color c, float o) {
  return color(c.r, c.g, c.b, (int)(c.a * o + 0.5f));
}

float md_focus_opacity(int line, int cur, int prev, float t) {
  /* newly focused line fades dim→full; the line just left fades full→dim;
     every other line stays dim. t in [0,1] is the crossfade progress. */
  if (line == cur)  return FOCUS_DIM_OPACITY + (1.0f - FOCUS_DIM_OPACITY) * t;
  if (line == prev) return 1.0f - (1.0f - FOCUS_DIM_OPACITY) * t;
  return FOCUS_DIM_OPACITY;
}

float md_draw_text(Line *l, int start, int end,
                   float x, float y, Color base_color, int heading,
                   int track_cursor_col, int *out_cursor_x, int draw) {
  const char *text = l->text;
  float op = g_text_opacity;
  int saved_style = r_get_font_style();
  int base_style  = heading ? FONT_BOLD : FONT_REGULAR;
  int font_h      = r_get_text_height();

  Color dim     = color(80, 80, 80, 255);
  Color link_fg = color(180, 160, 220, 255);
  Color link_bg = color(80, 50, 120, 255);
  Color code_fg = color(180, 140, 100, 255);
  Color hl_bg   = color(240, 214, 92, 70);   /* soft highlighter yellow */
  static const int wave[4] = { 0, 1, 2, 1 };       /* hand-drawn wobble */

  const struct MdSpan *spans;
  int span_count = md_line_spans(l, &spans);
  int si = 0;
  while (si < span_count && spans[si].end <= start) si++;   /* first span over `start` */

  float px = x;
  for (int i = start; i < end; ) {
    while (si < span_count && i >= spans[si].end) si++;

    /* attributes for the character at i */
    int style = base_style;
    Color fg = base_color;
    int bg = 0;   /* 0 none, 1 link, 2 highlight */
    if (si < span_count && i >= spans[si].open && i < spans[si].end) {
      const struct MdSpan *s = &spans[si];
      if (s->kind == SP_LINK || s->kind == SP_WIKI) {
        fg = link_fg; bg = 1;
      } else if (i < s->content || i >= s->close) {
        fg = dim;   /* the delimiter characters themselves */
      } else {
        switch (s->kind) {
          case SP_BOLD:   style = FONT_BOLD; break;
          case SP_ITALIC: style = heading ? FONT_BOLD : FONT_ITALIC; break;
          case SP_MONO:   style = FONT_MONO; fg = code_fg; break;
          case SP_HL:     bg = 2; break;
        }
      }
    }

    if (i == track_cursor_col) *out_cursor_x = (int)px;

    int n = utf8_len(text + i, end - i);          /* draw a whole codepoint */
    char ch[5];
    memcpy(ch, text + i, n);
    ch[n] = '\0';
    r_set_font_style(style);
    int w = r_get_text_width(ch, n);
    if (draw) {
      if (bg == 1) {
        r_draw_rect(rect((int)px, (int)y, w, font_h), md_fade(link_bg, op));
      } else if (bg == 2) {
        int woff = wave[i & 3];
        r_draw_rect(rect((int)px, (int)y + woff, w, font_h - 1), md_fade(hl_bg, op));
      }
      r_draw_text(ch, vec2((int)px, (int)y), md_fade(fg, op));
    }
    px += w;
    i += n;
  }

  if (track_cursor_col >= end) *out_cursor_x = (int)px;

  r_set_font_style(saved_style);
  return px;
}

int md_col_x(Line *l, int start, int end, int x0, int heading, int col) {
  int out = x0;
  md_draw_text(l, start, end, (float)x0, 0.0f, color(0, 0, 0, 0),
               heading, col, &out, 0 /* measure only */);
  return out;
}
