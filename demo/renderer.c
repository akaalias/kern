#include <SDL2/SDL.h>
#include <SDL2/SDL_opengl.h>
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <math.h>
#include "renderer.h"

/* stb_truetype */
#define STB_TRUETYPE_IMPLEMENTATION
#include "stb_truetype.h"

/* original atlas for icons + white pixel */
#include "atlas.inl"

#define BUFFER_SIZE 16384

static GLfloat   tex_buf[BUFFER_SIZE *  8];
static GLfloat  vert_buf[BUFFER_SIZE *  8];
static GLubyte color_buf[BUFFER_SIZE * 16];
static GLuint  index_buf[BUFFER_SIZE *  6];

static int width  = 800;
static int height = 600;
static int draw_width, draw_height;  /* actual pixel size (retina) */
static float dpi_scale = 1.0f;
static int buf_idx;

static SDL_Window *window;

static GLuint icon_tex;
static GLuint current_tex;

/* ---- TTF fonts (multi-style) ---- */
#define FONT_ATLAS_W 2048
#define FONT_ATLAS_H 2048

typedef struct {
  int x0, y0, x1, y1;
  float xoff, yoff;
  float xadvance;
  float bw, bh;
} GlyphInfo;

typedef struct {
  stbtt_fontinfo info;
  unsigned char *file_buf;
  GlyphInfo glyphs[128];
  unsigned char atlas[FONT_ATLAS_W * FONT_ATLAS_H];
  GLuint tex;
  float baseline;
  int loaded;
} FontData;

static FontData fonts[FONT_COUNT];
static int current_font = FONT_REGULAR;
static float font_size = 16.0f;

static void load_ttf(int style, const char *path) {
  FontData *fd = &fonts[style];
  FILE *f = fopen(path, "rb");
  if (!f) { fprintf(stderr, "cannot open font: %s\n", path); exit(1); }
  fseek(f, 0, SEEK_END);
  long sz = ftell(f);
  fseek(f, 0, SEEK_SET);
  fd->file_buf = malloc(sz);
  fread(fd->file_buf, 1, sz, f);
  fclose(f);

  if (!stbtt_InitFont(&fd->info, fd->file_buf, 0)) {
    fprintf(stderr, "stbtt_InitFont failed for %s\n", path);
    exit(1);
  }
  fd->loaded = 1;
}

static void rebuild_font_atlas_for(FontData *fd) {
  memset(fd->atlas, 0, sizeof(fd->atlas));

  float render_size = font_size * dpi_scale;
  float scale = stbtt_ScaleForPixelHeight(&fd->info, render_size);

  int asc, desc, lg;
  stbtt_GetFontVMetrics(&fd->info, &asc, &desc, &lg);
  fd->baseline = (asc * scale) / dpi_scale;

  int pen_x = 1, pen_y = 1, row_h = 0;

  for (int c = 32; c < 128; c++) {
    int w, h, xoff, yoff;
    unsigned char *bmp = stbtt_GetCodepointBitmap(&fd->info, 0, scale, c, &w, &h, &xoff, &yoff);

    if (pen_x + w + 1 >= FONT_ATLAS_W) {
      pen_x = 1;
      pen_y += row_h + 1;
      row_h = 0;
    }

    if (pen_y + h + 1 >= FONT_ATLAS_H) {
      if (bmp) stbtt_FreeBitmap(bmp, NULL);
      break;
    }

    if (bmp) {
      for (int r = 0; r < h; r++) {
        memcpy(&fd->atlas[(pen_y + r) * FONT_ATLAS_W + pen_x], &bmp[r * w], w);
      }
      stbtt_FreeBitmap(bmp, NULL);
    }

    fd->glyphs[c].x0 = pen_x;
    fd->glyphs[c].y0 = pen_y;
    fd->glyphs[c].x1 = pen_x + w;
    fd->glyphs[c].y1 = pen_y + h;
    fd->glyphs[c].xoff = xoff / dpi_scale;
    fd->glyphs[c].yoff = yoff / dpi_scale;
    fd->glyphs[c].bw = w / dpi_scale;
    fd->glyphs[c].bh = h / dpi_scale;

    int advance, lsb;
    stbtt_GetCodepointHMetrics(&fd->info, c, &advance, &lsb);
    fd->glyphs[c].xadvance = (advance * scale) / dpi_scale;

    pen_x += w + 1;
    if (h > row_h) row_h = h;
  }

  glBindTexture(GL_TEXTURE_2D, fd->tex);
  glTexImage2D(GL_TEXTURE_2D, 0, GL_ALPHA, FONT_ATLAS_W, FONT_ATLAS_H, 0,
    GL_ALPHA, GL_UNSIGNED_BYTE, fd->atlas);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
}

static void rebuild_all_font_atlases(void) {
  for (int i = 0; i < FONT_COUNT; i++) {
    if (fonts[i].loaded) rebuild_font_atlas_for(&fonts[i]);
  }
}


/* ---- rendering ---- */

static void do_flush(void) {
  if (buf_idx == 0) return;

  glBindTexture(GL_TEXTURE_2D, current_tex);
  glViewport(0, 0, draw_width, draw_height);
  glMatrixMode(GL_PROJECTION);
  glPushMatrix();
  glLoadIdentity();
  glOrtho(0.0f, width, height, 0.0f, -1.0f, +1.0f);
  glMatrixMode(GL_MODELVIEW);
  glPushMatrix();
  glLoadIdentity();

  glTexCoordPointer(2, GL_FLOAT, 0, tex_buf);
  glVertexPointer(2, GL_FLOAT, 0, vert_buf);
  glColorPointer(4, GL_UNSIGNED_BYTE, 0, color_buf);
  glDrawElements(GL_TRIANGLES, buf_idx * 6, GL_UNSIGNED_INT, index_buf);

  glMatrixMode(GL_MODELVIEW);
  glPopMatrix();
  glMatrixMode(GL_PROJECTION);
  glPopMatrix();

  buf_idx = 0;
}

static void flush(void) {
  do_flush();
}

static void switch_texture(GLuint tex) {
  if (tex != current_tex) {
    do_flush();
    current_tex = tex;
  }
}


static void push_quad_uv(float dst_x, float dst_y, float dst_w, float dst_h,
                          float u0, float v0, float u1, float v1,
                          mu_Color color) {
  if (buf_idx == BUFFER_SIZE) { flush(); }

  int texvert_idx = buf_idx *  8;
  int   color_idx = buf_idx * 16;
  int element_idx = buf_idx *  4;
  int   index_idx = buf_idx *  6;
  buf_idx++;

  tex_buf[texvert_idx + 0] = u0;
  tex_buf[texvert_idx + 1] = v0;
  tex_buf[texvert_idx + 2] = u1;
  tex_buf[texvert_idx + 3] = v0;
  tex_buf[texvert_idx + 4] = u0;
  tex_buf[texvert_idx + 5] = v1;
  tex_buf[texvert_idx + 6] = u1;
  tex_buf[texvert_idx + 7] = v1;

  vert_buf[texvert_idx + 0] = dst_x;
  vert_buf[texvert_idx + 1] = dst_y;
  vert_buf[texvert_idx + 2] = dst_x + dst_w;
  vert_buf[texvert_idx + 3] = dst_y;
  vert_buf[texvert_idx + 4] = dst_x;
  vert_buf[texvert_idx + 5] = dst_y + dst_h;
  vert_buf[texvert_idx + 6] = dst_x + dst_w;
  vert_buf[texvert_idx + 7] = dst_y + dst_h;

  memcpy(color_buf + color_idx +  0, &color, 4);
  memcpy(color_buf + color_idx +  4, &color, 4);
  memcpy(color_buf + color_idx +  8, &color, 4);
  memcpy(color_buf + color_idx + 12, &color, 4);

  index_buf[index_idx + 0] = element_idx + 0;
  index_buf[index_idx + 1] = element_idx + 1;
  index_buf[index_idx + 2] = element_idx + 2;
  index_buf[index_idx + 3] = element_idx + 2;
  index_buf[index_idx + 4] = element_idx + 3;
  index_buf[index_idx + 5] = element_idx + 1;
}


static void push_quad_icon(mu_Rect dst, mu_Rect src, mu_Color color) {
  float u0 = src.x / (float) ATLAS_WIDTH;
  float v0 = src.y / (float) ATLAS_HEIGHT;
  float u1 = (src.x + src.w) / (float) ATLAS_WIDTH;
  float v1 = (src.y + src.h) / (float) ATLAS_HEIGHT;
  push_quad_uv(dst.x, dst.y, dst.w, dst.h, u0, v0, u1, v1, color);
}


void r_init(void) {
  /* use usable screen area on launch (excludes menu bar / dock) */
  SDL_Rect usable = {SDL_WINDOWPOS_UNDEFINED, SDL_WINDOWPOS_UNDEFINED, width, height};
  SDL_GetDisplayUsableBounds(0, &usable);
  int pad = 25;
  width = usable.w - 2 * pad;
  height = usable.h - 2 * pad;
  window = SDL_CreateWindow(
    NULL, usable.x + pad, usable.y + pad,
    width, height, SDL_WINDOW_OPENGL | SDL_WINDOW_ALLOW_HIGHDPI | SDL_WINDOW_RESIZABLE);
  SDL_GL_CreateContext(window);

  /* detect retina scale */
  SDL_GL_GetDrawableSize(window, &draw_width, &draw_height);
  dpi_scale = (float)draw_width / (float)width;

  glEnable(GL_BLEND);
  glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
  glDisable(GL_CULL_FACE);
  glDisable(GL_DEPTH_TEST);
  glEnable(GL_SCISSOR_TEST);
  glEnable(GL_TEXTURE_2D);
  glEnableClientState(GL_VERTEX_ARRAY);
  glEnableClientState(GL_TEXTURE_COORD_ARRAY);
  glEnableClientState(GL_COLOR_ARRAY);

  /* icon atlas texture */
  glGenTextures(1, &icon_tex);
  glBindTexture(GL_TEXTURE_2D, icon_tex);
  glTexImage2D(GL_TEXTURE_2D, 0, GL_ALPHA, ATLAS_WIDTH, ATLAS_HEIGHT, 0,
    GL_ALPHA, GL_UNSIGNED_BYTE, atlas_texture);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);

  /* font textures */
  for (int i = 0; i < FONT_COUNT; i++) {
    glGenTextures(1, &fonts[i].tex);
    fonts[i].loaded = 0;
  }

  load_ttf(FONT_REGULAR, "iAWriterQuattroS-Regular.ttf");
  load_ttf(FONT_BOLD,    "iAWriterQuattroS-Bold.ttf");
  load_ttf(FONT_ITALIC,  "iAWriterQuattroS-Italic.ttf");
  load_ttf(FONT_MONO,    "iAWriterMonoS-Regular.ttf");
  rebuild_all_font_atlases();

  current_font = FONT_REGULAR;
  current_tex = fonts[FONT_REGULAR].tex;
  assert(glGetError() == 0);
}


void r_draw_rect(mu_Rect rect, mu_Color color) {
  switch_texture(icon_tex);
  push_quad_icon(rect, atlas[ATLAS_WHITE], color);
}


void r_draw_text(const char *text, mu_Vec2 pos, mu_Color color) {
  FontData *fd = &fonts[current_font];
  switch_texture(fd->tex);
  float x = pos.x;
  float y = pos.y;
  for (const char *p = text; *p; p++) {
    if ((*p & 0xc0) == 0x80) { continue; }
    int c = (unsigned char)*p;
    if (c < 32 || c > 127) c = '?';

    GlyphInfo *g = &fd->glyphs[c];

    if (g->bw > 0 && g->bh > 0) {
      float u0 = g->x0 / (float)FONT_ATLAS_W;
      float v0 = g->y0 / (float)FONT_ATLAS_H;
      float u1 = g->x1 / (float)FONT_ATLAS_W;
      float v1 = g->y1 / (float)FONT_ATLAS_H;

      float dx = x + g->xoff;
      float dy = y + fd->baseline + g->yoff;

      push_quad_uv(dx, dy, g->bw, g->bh, u0, v0, u1, v1, color);
    }

    x += g->xadvance;
  }
}


void r_draw_icon(int id, mu_Rect rect, mu_Color color) {
  switch_texture(icon_tex);
  mu_Rect src = atlas[id];
  int x = rect.x + (rect.w - src.w) / 2;
  int y = rect.y + (rect.h - src.h) / 2;
  push_quad_icon(mu_rect(x, y, src.w, src.h), src, color);
}


int r_get_text_width(const char *text, int len) {
  FontData *fd = &fonts[current_font];
  float w = 0;
  for (const char *p = text; *p && len--; p++) {
    if ((*p & 0xc0) == 0x80) { continue; }
    int c = (unsigned char)*p;
    if (c < 32 || c > 127) c = '?';
    w += fd->glyphs[c].xadvance;
  }
  return (int)(w + 0.5f);
}


int r_get_text_height(void) {
  return (int)(font_size + 0.5f);
}


void r_set_font_size(float size) {
  if (fabsf(size - font_size) < 0.5f) return;
  font_size = size;
  flush();
  rebuild_all_font_atlases();
}

void r_set_font_style(int style) {
  if (style >= 0 && style < FONT_COUNT && fonts[style].loaded) {
    current_font = style;
  }
}

int r_get_font_style(void) {
  return current_font;
}


void r_set_clip_rect(mu_Rect rect) {
  flush();
  glScissor(rect.x * dpi_scale, draw_height - (rect.y + rect.h) * dpi_scale,
            rect.w * dpi_scale, rect.h * dpi_scale);
}


void r_clear(mu_Color clr) {
  flush();
  glClearColor(clr.r / 255., clr.g / 255., clr.b / 255., clr.a / 255.);
  glClear(GL_COLOR_BUFFER_BIT);
}


void r_set_title(const char *title) {
  SDL_SetWindowTitle(window, title);
}

SDL_Window* r_get_window(void) {
  return window;
}

void r_handle_resize(void) {
  SDL_GetWindowSize(window, &width, &height);
  SDL_GL_GetDrawableSize(window, &draw_width, &draw_height);
  dpi_scale = (float)draw_width / (float)width;
}

void r_get_size(int *w, int *h) {
  *w = width;
  *h = height;
}

void r_present(void) {
  flush();
  SDL_GL_SwapWindow(window);
}
