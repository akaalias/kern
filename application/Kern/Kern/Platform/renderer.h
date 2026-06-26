#ifndef RENDERER_H
#define RENDERER_H

#include <SDL2/SDL.h>
#include "gfx.h"

/* font styles */
enum {
  FONT_REGULAR = 0,
  FONT_BOLD,
  FONT_ITALIC,
  FONT_MONO,
  FONT_COUNT
};

void r_init(void);
void r_draw_rect(Rect rect, Color color);
/* Lightly blur whatever is already drawn under `rect` (in place); `radius` is the
   smear in logical px. App-only effect; the headless stub is a no-op. */
void r_blur_rect(Rect rect, int radius);
/* Stencil clip mask: begin → draw mask shape → use → draw clipped content → end.
   App-only; the headless stubs are no-ops. */
void r_clip_mask_begin(void);
void r_clip_mask_use(void);
void r_clip_mask_end(void);
void r_draw_text(const char *text, Vec2 pos, Color color);
 int r_get_text_width(const char *text, int len);
 int r_get_text_height(void);
/* 1 if the body font can draw the first codepoint of `utf8`, else 0. Lets the
   symbol-substitution layer fall back to literal text for glyphs the bundled font
   lacks (e.g. ⇒ ∀ ∇ Ω) instead of rendering tofu boxes. */
 int r_has_glyph(const char *utf8, int byte_len);
void r_set_clip_rect(Rect rect);
void r_clear(Color color);
void r_present(void);
void r_set_font_size(float size);
void r_set_font_style(int style);
 int r_get_font_style(void);
void r_set_title(const char *title);
SDL_Window* r_get_window(void);
void r_handle_resize(void);
void r_get_size(int *w, int *h);

#endif
