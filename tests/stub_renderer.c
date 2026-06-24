/* stub_renderer.c — see stub_renderer.h. Implements the renderer.h interface
 * with deterministic metrics + draw-op capture, plus a no-op SDL_GetTicks so
 * navigation.c links without libSDL2. */
#include <stdio.h>
#include <string.h>
#include "stub_renderer.h"

StubText stub_texts[STUB_MAX];
int      stub_text_count;
StubRect stub_rects[STUB_MAX];
int      stub_rect_count;
StubOp   stub_ops[STUB_MAX];
int      stub_op_count;

static int s_cell_w = 10, s_cell_h = 20, s_win_w = 800, s_win_h = 600;
static int s_style = FONT_REGULAR;
/* per-style extra width per char (default 0 = style-independent, matching real
   life closely enough for most tests). A test can widen e.g. FONT_MONO to model
   the real renderer, where measuring in the wrong font corrupts wrap caches. */
static int s_style_extra[FONT_COUNT];

void stub_reset(void) {
  stub_text_count = 0;
  stub_rect_count = 0;
  stub_op_count = 0;
  s_style = FONT_REGULAR;
  for (int i = 0; i < FONT_COUNT; i++) s_style_extra[i] = 0;
}

void stub_set_metrics(int cell_w, int cell_h, int win_w, int win_h) {
  s_cell_w = cell_w; s_cell_h = cell_h; s_win_w = win_w; s_win_h = win_h;
}

void stub_set_style_extra(int style, int extra) {
  if (style >= 0 && style < FONT_COUNT) s_style_extra[style] = extra;
}

/* ---- metrics ---- */
int  r_get_text_width(const char *text, int len) {
  (void)text;
  return len * (s_cell_w + s_style_extra[s_style]);
}
int  r_get_text_height(void)                     { return s_cell_h; }
void r_get_size(int *w, int *h)                  { if (w) *w = s_win_w; if (h) *h = s_win_h; }

/* ---- font style ---- */
void r_set_font_style(int style) { s_style = style; }
int  r_get_font_style(void)      { return s_style; }
void r_set_font_size(float size) { (void)size; }

/* ---- draw capture ---- */
void r_draw_text(const char *text, mu_Vec2 pos, mu_Color color) {
  const char *s = text ? text : "";
  if (stub_text_count < STUB_MAX) {
    StubText *t = &stub_texts[stub_text_count++];
    snprintf(t->ch, sizeof t->ch, "%s", s);
    t->x = pos.x; t->y = pos.y; t->style = s_style; t->color = color;
  }
  if (stub_op_count < STUB_MAX) {
    StubOp *o = &stub_ops[stub_op_count++];
    o->kind = STUB_OP_TEXT;
    snprintf(o->ch, sizeof o->ch, "%s", s);
    o->rect = mu_rect(pos.x, pos.y, 0, 0);
    o->style = s_style; o->color = color;
  }
}

void r_draw_rect(mu_Rect rect, mu_Color color) {
  if (stub_rect_count < STUB_MAX) {
    StubRect *r = &stub_rects[stub_rect_count++];
    r->rect = rect; r->color = color;
  }
  if (stub_op_count < STUB_MAX) {
    StubOp *o = &stub_ops[stub_op_count++];
    o->kind = STUB_OP_RECT;
    o->ch[0] = '\0';
    o->rect = rect; o->style = 0; o->color = color;
  }
}

/* ---- unused by the headless layout paths: no-ops ---- */
void r_init(void)                              {}
void r_draw_icon(int id, mu_Rect r, mu_Color c){ (void)id; (void)r; (void)c; }
void r_set_clip_rect(mu_Rect rect)             { (void)rect; }
void r_clear(mu_Color color)                   { (void)color; }
void r_present(void)                           {}
void r_set_title(const char *title)            { (void)title; }
SDL_Window *r_get_window(void)                 { return NULL; }
void r_handle_resize(void)                     {}
