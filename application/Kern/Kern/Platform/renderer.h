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
void r_draw_rect(mu_Rect rect, mu_Color color);
void r_draw_text(const char *text, mu_Vec2 pos, mu_Color color);
 int r_get_text_width(const char *text, int len);
 int r_get_text_height(void);
void r_set_clip_rect(mu_Rect rect);
void r_clear(mu_Color color);
void r_present(void);
void r_set_font_size(float size);
void r_set_font_style(int style);
 int r_get_font_style(void);
void r_set_title(const char *title);
SDL_Window* r_get_window(void);
void r_handle_resize(void);
void r_get_size(int *w, int *h);

#endif
