/* md_render.c — Markdown-aware text rendering */

#include <stdlib.h>
#include <string.h>
#include "md_render.h"
#include "pos_render.h"
#include "style_check.h"
#include "sub_render.h"
#include "renderer.h"
#include "utf8.h"

/* If `l` is a list item, return the byte index just past its marker — leading
   whitespace plus "- " or "N. " — else 0. The single marker scan that the three
   list helpers below share. */
static int md_list_marker_end(Line *l) {
  int i = 0;
  while (i < l->len && (l->text[i] == ' ' || l->text[i] == '\t')) i++;
  if (i + 1 < l->len && l->text[i] == '-' && l->text[i+1] == ' ') return i + 2;
  int d = i;
  while (d < l->len && l->text[d] >= '0' && l->text[d] <= '9') d++;
  if (d > i && d + 1 < l->len && l->text[d] == '.' && l->text[d+1] == ' ') return d + 2;
  return 0;
}

/* base indent shared by every list level; nesting is added on top by the line's
   own leading whitespace, which is drawn as literal text */
int md_list_indent(Line *l) {
  return md_list_marker_end(l) ? r_get_text_width("    ", 4) : 0;
}

/* pixel width of a list item's marker — including any leading indentation
   whitespace, so wrapped continuation rows hang under the item text. 0 if not
   a list item. */
int md_list_marker_width(Line *l) {
  int end = md_list_marker_end(l);
  return end ? r_get_text_width(l->text, end) : 0;
}

int md_is_list_item(Line *l) {
  return md_list_marker_end(l) != 0;
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
enum { SP_NONE = 0, SP_BOLD, SP_ITALIC, SP_MONO, SP_HL, SP_LINK, SP_WIKI, SP_UNDERLINE };

/* max chars to scan forward for a closing delimiter (bounds O(n²) on
   pathological input) */
#define MD_MAX_SCAN 1024

/* If an inline span opens at position i, return its kind and fill the span's
   content/close/end boundaries; otherwise SP_NONE. Closing delimiters are
   searched up to line_len, so a span may cross visual-row boundaries.
   content..close is the styled inner text; [i,content) and [close,end) are the
   delimiter runs. For links the whole token is "content" (no inner markers). */
/* A doubled symmetric delimiter (** == ++) opening at i: scan for its closer and
   fill the boundaries, returning `kind`; SP_NONE if none opens here. */
static int md_detect_pair(const char *t, int i, int line_len, int lim,
                          char d, int kind, int *content, int *close, int *end) {
  if (i + 1 < line_len && t[i] == d && t[i+1] == d) {
    for (int j = i + 2; j + 1 < lim; j++)
      if (t[j] == d && t[j+1] == d) {
        *content = i + 2; *close = j; *end = j + 2; return kind;
      }
  }
  return SP_NONE;
}

static int md_detect_span(const char *t, int i, int line_len,
                          int *content, int *close, int *end) {
  int lim = i + MD_MAX_SCAN;
  if (lim > line_len) lim = line_len;

  int kind;
  if ((kind = md_detect_pair(t, i, line_len, lim, '*', SP_BOLD, content, close, end))) return kind;
  if ((kind = md_detect_pair(t, i, line_len, lim, '=', SP_HL, content, close, end))) return kind;
  if ((kind = md_detect_pair(t, i, line_len, lim, '+', SP_UNDERLINE, content, close, end))) return kind;

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

/* Nesting-aware decoration scan: mark, per byte in [lo,hi), whether it sits
   inside a highlight's content (hl) and/or an underline's content (ul),
   recursing into each span's content so an ==highlight== inside ++underline++
   (and vice versa) shows BOTH rules. Only these two decorations nest — they're
   zero-width overlays, so the click/wrap/width walks (which never look at them)
   stay flat and unchanged. `content > i` skips whole-token link/wiki (content==
   open) so recursion can't spin on them. Bounded by md_detect_span's MD_MAX_SCAN. */
static void md_decor_recurse(const char *t, int lo, int hi,
                             unsigned char *hl, unsigned char *ul) {
  int i = lo;
  while (i < hi) {
    int content, close, end;
    int kind = md_detect_span(t, i, hi, &content, &close, &end);
    if (kind && content > i) {
      if (kind == SP_HL)             { for (int d = content; d < close; d++) hl[d] = 1; }
      else if (kind == SP_UNDERLINE) { for (int d = content; d < close; d++) ul[d] = 1; }
      md_decor_recurse(t, content, close, hl, ul);   /* nested HL/UL inside this span */
      i = end;
    } else {
      i++;
    }
  }
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

/* Active syntax-highlight mask (bit per PosClass; 0 = off). Like g_text_opacity,
   a module global so the many md_draw_text callers don't thread it through. The
   render pass sets it from ViewState.syntax_mask each frame. */
static unsigned int g_syntax_mask = 0;
void md_set_syntax_mask(unsigned int m) { g_syntax_mask = m; }

/* When set (typewriter mode), every glyph renders and measures in FONT_MONO,
   overriding the base/markdown styles. Wrap, click and render all read this so
   the geometry stays in lockstep. md_wrap_font_style() is the body font the
   measurement helpers (navigation's body_width etc.) should use. */
static int g_force_mono = 0;
void md_set_force_mono(int on) { g_force_mono = on; }
int  md_wrap_font_style(void)  { return g_force_mono ? FONT_MONO : FONT_REGULAR; }

/* Active style-check mask (bit per StyleCategory; 0 = off). Set per frame from
   ViewState.style_mask; cuttable words within it are greyed and struck through. */
static unsigned int g_style_mask = 0;
void md_set_style_mask(unsigned int m) { g_style_mask = m; }

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

/* The width-affecting font style for the byte at `i`: base, unless `i` is inside
   the content of a bold/italic/mono span (links and the delimiter runs keep the
   base width). `si` must already cover `i`. g_force_mono forces FONT_MONO. This
   is the one place the draw walk (md_draw_text) and the click walk (md_x_to_col)
   agree on per-token width, so they can't drift. */
static int md_span_style(const struct MdSpan *spans, int span_count, int si,
                         int i, int base_style, int heading) {
  int style = base_style;
  if (si < span_count && i >= spans[si].open && i < spans[si].end) {
    const struct MdSpan *s = &spans[si];
    if (s->kind != SP_LINK && s->kind != SP_WIKI && i >= s->content && i < s->close) {
      switch (s->kind) {
        case SP_BOLD:   style = FONT_BOLD; break;
        case SP_ITALIC: style = heading ? FONT_BOLD : FONT_ITALIC; break;
        case SP_MONO:   style = FONT_MONO; break;
        default: break;   /* SP_HL / SP_UNDERLINE don't change width */
      }
    }
  }
  if (g_force_mono) style = FONT_MONO;
  return style;
}

/* Resolve the token beginning at byte `i`, advancing the substitution cursor
   `*xi`. Returns the source byte advance `n`; *sub_out is the collapsed token to
   draw (a single glyph), or NULL when none begins here, or it is revealed/masked
   (draw the literal source). Shared by the draw and click walks so width and
   reveal state stay in lockstep. */
static int md_token_advance(Line *l, const SubSpan *subs, int sub_count, int *xi,
                            unsigned int sub_mask, int i, int end, const SubSpan **sub_out) {
  const SubSpan *sub = NULL;
  if (sub_count) {
    while (*xi < sub_count && subs[*xi].start + subs[*xi].len <= i) (*xi)++;
    if (*xi < sub_count && subs[*xi].start == i &&
        subs[*xi].start + subs[*xi].len <= end &&
        (sub_mask & SUB_BIT(subs[*xi].category)))
      sub = &subs[*xi];
  }
  if (sub && sub_token_revealed(l, sub->start, sub->len)) sub = NULL;
  *sub_out = sub;
  return sub ? sub->len : utf8_len(l->text + i, end - i);
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
  Color hl_line = color(240, 214, 92, 235);   /* highlighter yellow — now a rule, not a fill */
  Color strike_fg = color(120, 116, 111, 255); /* greyed cuttable text */
  int strike_thick = font_h >= 24 ? 2 : 1;
  static const int wave[4] = { 0, 1, 2, 1 };       /* hand-drawn wobble */

  const struct MdSpan *spans;
  int span_count = md_line_spans(l, &spans);
  int si = 0;
  while (si < span_count && spans[si].end <= start) si++;   /* first span over `start` */

  /* Nesting-aware highlight/underline flags (draw-only; these overlays don't
     affect width, so the measure/click walks skip this). A glyph can be both
     highlighted and underlined when the two spans nest. */
  unsigned char *dec_hl = NULL, *dec_ul = NULL;
  if (draw && l->len > 0) {
    dec_hl = calloc((size_t)l->len, 1);
    dec_ul = calloc((size_t)l->len, 1);
    if (dec_hl && dec_ul) md_decor_recurse(text, 0, l->len, dec_hl, dec_ul);
  }

  /* text→symbol substitution: a source token (->, --, a straight quote) draws as
     one replacement glyph. Streamed with a forward cursor alongside the md spans;
     wrap breaks are token-aligned, so a token never crosses [start,end). */
  unsigned int sub_mask = sub_active_mask();
  const SubSpan *subs = NULL;
  int sub_count = sub_mask ? sub_line_spans(l, &subs) : 0;
  int xi = 0;

  float px = x;
  for (int i = start; i < end; ) {
    while (si < span_count && i >= spans[si].end) si++;

    /* width style is shared with the click walk; color is draw-only and derives
       from the same (flat) span. Highlight/underline are separate nesting-aware
       overlays (dec_hl/dec_ul), so they aren't set here. */
    int style = md_span_style(spans, span_count, si, i, base_style, heading);
    Color fg = base_color;
    int bg = 0;   /* 0 none, 1 link */
    if (si < span_count && i >= spans[si].open && i < spans[si].end) {
      const struct MdSpan *s = &spans[si];
      if (s->kind == SP_LINK || s->kind == SP_WIKI) {
        fg = link_fg; bg = 1;
      } else if (i < s->content || i >= s->close) {
        fg = dim;   /* the delimiter characters themselves */
      } else if (s->kind == SP_MONO) {
        fg = code_fg;                             /* width set by md_span_style */
      }
    }
    int hl = dec_hl ? dec_hl[i] : 0;              /* nesting-aware highlight */
    int ul = dec_ul ? dec_ul[i] : 0;              /* nesting-aware underline  */

    /* Part-of-speech coloring layers on top of markdown: it recolors only runs
       markdown left at the base color, so links/code/delimiters keep their hue,
       while bold/italic words keep their weight and take the POS color. Highlight
       is an *isolate*, not a hide: a shown class takes its ramp value; everything
       else (toggled-off classes, untagged words) drops to the muted ground so the
       shown classes pop. */
    if (g_syntax_mask &&
        fg.r == base_color.r && fg.g == base_color.g &&
        fg.b == base_color.b && fg.a == base_color.a) {
      /* pos_resolve_color also holds the word being typed at base_color and
         fades a just-finished word from base_color up to its POS color. */
      fg = pos_resolve_color(l, g_syntax_mask, i, base_color);
    }

    /* Style check: cuttable text (fillers, the redundant word) is greyed and
       struck — "delete me"; a cliché gets a wavy underline with its color kept,
       since it wants rewriting, not deletion. */
    StyleDecor decor = g_style_mask ? style_decor_at(l, g_style_mask, i)
                                    : STYLE_DECOR_NONE;
    if (decor == STYLE_DECOR_STRIKE) fg = strike_fg;   /* only the filler strike greys */

    /* does a substituted token begin exactly here? (reveal-on-contact draws the
       literal source instead) */
    const SubSpan *sub;
    int n = md_token_advance(l, subs, sub_count, &xi, sub_mask, i, end, &sub);
    /* the caret anywhere within a collapsed token renders at its left edge */
    if (track_cursor_col >= i && track_cursor_col < i + n) *out_cursor_x = (int)px;

    char ch[5];
    const char *glyph;
    int glyph_n;
    if (sub) {
      glyph = sub->glyph; glyph_n = sub->glyph_len;
    } else {
      memcpy(ch, text + i, n);
      ch[n] = '\0';
      glyph = ch; glyph_n = n;
    }
    r_set_font_style(style);
    int w = r_get_text_width(glyph, glyph_n);
    if (draw) {
      if (bg == 1) {
        r_draw_rect(rect((int)px, (int)y, w, font_h), md_fade(link_bg, op));
      }
      /* ==highlight==: a yellow rule ABOVE and BELOW the text (was a full-height
         fill). The underline sits at font_h — a clear row below the ++ underline
         (font_h*0.92) — so a run that is both highlighted and underlined shows two
         separated lines, not one stuck to the other. hl is nesting-aware, so an
         ==hl== inside ++ul++ (and vice versa) both draw. */
      if (hl) {
        r_draw_rect(rect((int)px, (int)y + 1, w, 1), md_fade(hl_line, op));          /* overline  */
        r_draw_rect(rect((int)px, (int)y + font_h, w, 1), md_fade(hl_line, op));     /* underline */
      }
      r_draw_text(glyph, vec2((int)px, (int)y), md_fade(fg, op));
      /* ++underline++: a solid, uniform rule on the baseline. Use base_color, not
         the per-glyph fg, so syntax/POS coloring doesn't break it into a
         multicolor dashed-looking line — it's one continuous straight underline. */
      if (ul) {
        int dy = (int)y + (int)(font_h * 0.92f);
        r_draw_rect(rect((int)px, dy, w, 1), md_fade(base_color, op));
      }
      /* Each style category gets its own line texture, keyed to absolute x so
         the line stays continuous across characters:
           - filler     STRIKE:           light wobble through the x-height;
           - redundancy STRIKE_WAVY:       taller wave through the x-height;
           - cliché     UNDERLINE_DOTTED:  straight dotted line on the baseline. */
      if (decor == STYLE_DECOR_STRIKE) {
        int dy = (int)y + (int)(font_h * 0.45f);
        for (int sx = (int)px; sx < (int)px + w; sx += 3) {
          int seg = (int)px + w - sx; if (seg > 3) seg = 3;
          int woff = wave[(sx / 3) & 3] - 1;   /* {0,1,2,1} -> -1,0,1,0 */
          r_draw_rect(rect(sx, dy + woff, seg, strike_thick), md_fade(strike_fg, op));
        }
      } else if (decor == STYLE_DECOR_STRIKE_WAVY) {
        /* the same hand-drawn wavy line, ridden on the baseline as an underline */
        int dy = (int)y + (int)(font_h * 0.92f);
        for (int sx = (int)px; sx < (int)px + w; sx += 3) {
          int seg = (int)px + w - sx; if (seg > 3) seg = 3;
          int woff = wave[(sx / 3) & 3] - 1;   /* {0,1,2,1} -> -1,0,1,0 */
          r_draw_rect(rect(sx, dy + woff, seg, strike_thick), md_fade(strike_fg, op));
        }
      } else if (decor == STYLE_DECOR_UNDERLINE_DOTTED) {
        int dy = (int)y + (int)(font_h * 0.92f);
        for (int sx = (int)px; sx < (int)px + w; sx += 4) {   /* 2px dash, 2px gap */
          int seg = (int)px + w - sx; if (seg > 2) seg = 2;
          r_draw_rect(rect(sx, dy, seg, strike_thick), md_fade(strike_fg, op));
        }
      }
    }
    px += w;
    i += n;
  }

  if (track_cursor_col >= end) *out_cursor_x = (int)px;

  free(dec_hl);
  free(dec_ul);
  r_set_font_style(saved_style);
  return px;
}

int md_col_x(Line *l, int start, int end, int x0, int heading, int col) {
  int out = x0;
  md_draw_text(l, start, end, (float)x0, 0.0f, color(0, 0, 0, 0),
               heading, col, &out, 0 /* measure only */);
  return out;
}

int md_row_indent(Line *l, int row_start) {
  int indent = md_list_indent(l);
  /* continuation rows hang under the item text, not the marker */
  if (row_start > 0) indent += md_list_marker_width(l);
  return indent;
}

/* Inverse of md_col_x: the column in [start,end] whose rendered position is
   nearest target_x, measured with the same per-span font metrics md_draw_text
   uses (so bold/italic/mono runs map correctly). x0 is the row's draw origin.
   Shares md_span_style + md_token_advance with the draw walk, so the two can't
   drift on token width or reveal state. */
int md_x_to_col(Line *l, int start, int end, int x0, int heading, int target_x) {
  const char *text = l->text;
  int base_style = heading ? FONT_BOLD : FONT_REGULAR;
  int saved_style = r_get_font_style();

  const struct MdSpan *spans;
  int span_count = md_line_spans(l, &spans);
  int si = 0;
  while (si < span_count && spans[si].end <= start) si++;

  /* mirror md_draw_text's substitution so a click lands on the same column the
     caret would: a token resolves to its left or right edge, never its interior */
  unsigned int sub_mask = sub_active_mask();
  const SubSpan *subs = NULL;
  int sub_count = sub_mask ? sub_line_spans(l, &subs) : 0;
  int xi = 0;

  float px = (float)x0;
  int col = start;
  for (int i = start; i < end; ) {
    while (si < span_count && i >= spans[si].end) si++;

    /* same width style + token advance the draw walk uses, so a click maps to the
       column the caret would render at */
    int style = md_span_style(spans, span_count, si, i, base_style, heading);
    const SubSpan *sub;
    int n = md_token_advance(l, subs, sub_count, &xi, sub_mask, i, end, &sub);
    r_set_font_style(style);
    int w = sub ? r_get_text_width(sub->glyph, sub->glyph_len)
                : r_get_text_width(text + i, n);
    if (px + w / 2.0f > (float)target_x) { col = i; r_set_font_style(saved_style); return col; }
    px += w;
    i += n;
    col = i;
  }

  r_set_font_style(saved_style);
  return col;
}
