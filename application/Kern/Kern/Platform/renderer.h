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
  FONT_UI,        /* native system font (San Francisco); chrome/overlay only */
  FONT_COUNT
};

void r_init(void);
void r_draw_rect(Rect rect, Color color);
/* Filled rounded rectangle with anti-aliased corners (`radius` in logical px) —
   used for native-looking buttons in the GL-drawn overlay. The headless stub
   draws a plain rect (the AA is app-only). */
void r_draw_round_rect(Rect rect, int radius, Color color);
/* Lightly blur whatever is already drawn under `rect` (in place); `radius` is the
   smear in logical px. App-only effect; the headless stub is a no-op. */
void r_blur_rect(Rect rect, int radius);
/* Stencil clip mask: begin → draw mask shape → use → draw clipped content → end.
   App-only; the headless stubs are no-ops. */
void r_clip_mask_begin(void);
void r_clip_mask_use(void);
void r_clip_mask_end(void);
/* Draw a tightly-packed RGBA image (iw×ih) scaled into `rect`, clipped to a
   circle inscribed in the rect. Used for the X-publish preview avatar. The
   texture is (re)uploaded when the pixel pointer changes. App-only; the headless
   stub is a no-op. */
void r_draw_image_circle(Rect rect, const unsigned char *rgba, int iw, int ih);
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
/* Scale factor applied to text drawing + measuring (r_draw_text /
   r_get_text_width / r_get_text_height). Glyphs are drawn from the existing
   atlas, scaled at the quad — unlike r_set_font_size this never rebuilds the
   atlases, so it's safe per frame. Used by margin notes (0.5). Restore to 1.0f
   after use. */
void r_set_font_scale(float scale);
void r_set_font_style(int style);
 int r_get_font_style(void);
/* FONT_UI if the native system font loaded, else FONT_REGULAR — the font style
   chrome (the publish overlay) should draw in. */
 int r_ui_font_style(void);
void r_set_title(const char *title);
SDL_Window* r_get_window(void);
void r_handle_resize(void);
void r_get_size(int *w, int *h);

#endif
