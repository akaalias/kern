#include <SDL2/SDL.h>
#include <SDL2/SDL_opengl.h>
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <math.h>
#include "renderer.h"
#include "utf8.h"

/* stb_truetype */
#define STB_TRUETYPE_IMPLEMENTATION
#include "stb_truetype.h"

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

static GLuint white_tex;   /* 1x1 white texel; solid rects sample it */
static GLuint current_tex;

/* ---- TTF fonts (multi-style) ---- */
#define FONT_ATLAS_W 2048
#define FONT_ATLAS_H 2048
#define DYN_CAP      2048   /* open-addressing cache slots for codepoints >= 128 */

typedef struct {
  int x0, y0, x1, y1;
  float xoff, yoff;
  float xadvance;
  float bw, bh;
} GlyphInfo;

typedef struct {
  stbtt_fontinfo info;
  unsigned char *file_buf;
  GlyphInfo glyphs[128];           /* ASCII fast path, indexed by codepoint */
  /* on-demand cache for codepoints >= 128 (smart quotes, dashes, accents, …);
     dyn_cp[i]==0 means empty. Reset on a font-size rebuild. */
  int       dyn_cp[DYN_CAP];
  GlyphInfo dyn_glyph[DYN_CAP];
  int       pen_x, pen_y, row_h;   /* persistent atlas packer position */
  float     scale;                 /* px scale for on-demand rasterization */
  unsigned char atlas[FONT_ATLAS_W * FONT_ATLAS_H];
  GLuint tex;
  float baseline;
  int loaded;
} FontData;

static FontData fonts[FONT_COUNT];
static int current_font = FONT_REGULAR;
static float font_size = 16.0f;

static void load_ttf(int style, const char *filename) {
  FontData *fd = &fonts[style];

  /* Resolve the font relative to the app's base path so it works both as a
     bundled .app (base path = Contents/Resources/) and as a plain CLI binary
     (base path = the executable's directory). */
  char path[1024];
  char *base = SDL_GetBasePath();
  if (base) {
    snprintf(path, sizeof(path), "%s%s", base, filename);
    SDL_free(base);
  } else {
    snprintf(path, sizeof(path), "%s", filename);
  }

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

/* Rasterize codepoint `cp` at fd->scale into the atlas at the running pen,
   filling *gi. Shared by the initial ASCII build and on-demand caching. If the
   atlas is full, the glyph draws nothing but keeps a sane advance for spacing. */
static void pack_glyph(FontData *fd, int cp, GlyphInfo *gi) {
  int w, h, xoff, yoff;
  unsigned char *bmp = stbtt_GetCodepointBitmap(&fd->info, 0, fd->scale, cp, &w, &h, &xoff, &yoff);

  if (fd->pen_x + w + 1 >= FONT_ATLAS_W) {
    fd->pen_x = 1;
    fd->pen_y += fd->row_h + 1;
    fd->row_h = 0;
  }

  int advance, lsb;
  stbtt_GetCodepointHMetrics(&fd->info, cp, &advance, &lsb);

  if (fd->pen_y + h + 1 >= FONT_ATLAS_H) {       /* atlas exhausted */
    if (bmp) stbtt_FreeBitmap(bmp, NULL);
    memset(gi, 0, sizeof *gi);
    gi->xadvance = (advance * fd->scale) / dpi_scale;
    return;
  }

  if (bmp) {
    for (int r = 0; r < h; r++)
      memcpy(&fd->atlas[(fd->pen_y + r) * FONT_ATLAS_W + fd->pen_x], &bmp[r * w], w);
    stbtt_FreeBitmap(bmp, NULL);
  }

  gi->x0 = fd->pen_x;        gi->y0 = fd->pen_y;
  gi->x1 = fd->pen_x + w;    gi->y1 = fd->pen_y + h;
  gi->xoff = xoff / dpi_scale; gi->yoff = yoff / dpi_scale;
  gi->bw = w / dpi_scale;      gi->bh = h / dpi_scale;
  gi->xadvance = (advance * fd->scale) / dpi_scale;

  fd->pen_x += w + 1;
  if (h > fd->row_h) fd->row_h = h;
}

static void rebuild_font_atlas_for(FontData *fd) {
  memset(fd->atlas, 0, sizeof(fd->atlas));
  memset(fd->dyn_cp, 0, sizeof(fd->dyn_cp));   /* drop on-demand glyphs; re-raster at the new size */

  float render_size = font_size * dpi_scale;
  fd->scale = stbtt_ScaleForPixelHeight(&fd->info, render_size);

  int asc, desc, lg;
  stbtt_GetFontVMetrics(&fd->info, &asc, &desc, &lg);
  fd->baseline = (asc * fd->scale) / dpi_scale;

  fd->pen_x = 1; fd->pen_y = 1; fd->row_h = 0;
  for (int c = 32; c < 128; c++) pack_glyph(fd, c, &fd->glyphs[c]);

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
                          Color color) {
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
  SDL_GL_SetSwapInterval(1);  /* vsync: cap frame rate to the display refresh */

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

  /* 1x1 white texture: solid rects sample it and multiply by their color,
     so r_draw_rect reuses the same textured-quad path as glyphs */
  static const unsigned char white_px = 255;
  glGenTextures(1, &white_tex);
  glBindTexture(GL_TEXTURE_2D, white_tex);
  glTexImage2D(GL_TEXTURE_2D, 0, GL_ALPHA, 1, 1, 0,
    GL_ALPHA, GL_UNSIGNED_BYTE, &white_px);
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


void r_draw_rect(Rect rect, Color color) {
  switch_texture(white_tex);
  push_quad_uv(rect.x, rect.y, rect.w, rect.h, 0, 0, 1, 1, color);
}


/* Glyph for codepoint `cp`: ASCII from the prebuilt table; anything else from
   the on-demand cache (rasterized + uploaded the first time it's seen). */
static GlyphInfo *glyph_for(FontData *fd, int cp) {
  if (cp < 32) cp = '?';                 /* control chars render as '?', as before */
  if (cp < 128) return &fd->glyphs[cp];

  unsigned hash = (unsigned)cp & (DYN_CAP - 1);
  for (int probe = 0; probe < DYN_CAP; probe++) {
    int idx = (hash + probe) & (DYN_CAP - 1);
    if (fd->dyn_cp[idx] == cp) return &fd->dyn_glyph[idx];
    if (fd->dyn_cp[idx] == 0) {            /* miss → rasterize into this slot */
      fd->dyn_cp[idx] = cp;
      flush();                             /* finish pending quads before touching the texture */
      pack_glyph(fd, cp, &fd->dyn_glyph[idx]);
      GlyphInfo *gi = &fd->dyn_glyph[idx];
      int gw = gi->x1 - gi->x0, gh = gi->y1 - gi->y0;
      if (gw > 0 && gh > 0) {
        glBindTexture(GL_TEXTURE_2D, fd->tex);
        glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
        glPixelStorei(GL_UNPACK_ROW_LENGTH, FONT_ATLAS_W);
        glTexSubImage2D(GL_TEXTURE_2D, 0, gi->x0, gi->y0, gw, gh,
                        GL_ALPHA, GL_UNSIGNED_BYTE,
                        &fd->atlas[gi->y0 * FONT_ATLAS_W + gi->x0]);
        glPixelStorei(GL_UNPACK_ROW_LENGTH, 0);
        glBindTexture(GL_TEXTURE_2D, current_tex);   /* keep GL binding == current_tex */
      }
      return gi;
    }
  }
  return &fd->glyphs['?'];                  /* cache full (unreachable in practice) */
}

void r_draw_text(const char *text, Vec2 pos, Color color) {
  FontData *fd = &fonts[current_font];
  switch_texture(fd->tex);
  float x = pos.x;
  float y = pos.y;
  for (const char *p = text; *p; ) {
    int cp;
    p += utf8_decode(p, 4, &cp);
    GlyphInfo *g = glyph_for(fd, cp);

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




int r_get_text_width(const char *text, int len) {
  FontData *fd = &fonts[current_font];
  float w = 0;
  int i = 0;
  while (i < len && text[i]) {
    int cp;
    i += utf8_decode(text + i, len - i, &cp);
    w += glyph_for(fd, cp)->xadvance;
  }
  return (int)(w + 0.5f);
}


int r_get_text_height(void) {
  return (int)(font_size + 0.5f);
}

int r_has_glyph(const char *utf8, int byte_len) {
  int cp = 0;
  utf8_decode(utf8, byte_len, &cp);
  if (cp <= 0) return 0;
  /* gate on the body font (where substituted glyphs render in prose); a font
     missing a codepoint returns glyph index 0 (.notdef / the tofu box) */
  FontData *fd = &fonts[FONT_REGULAR];
  if (!fd->loaded) return 1;          /* font not ready: don't suppress */
  return stbtt_FindGlyphIndex(&fd->info, cp) != 0;
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


void r_set_clip_rect(Rect rect) {
  flush();
  glScissor(rect.x * dpi_scale, draw_height - (rect.y + rect.h) * dpi_scale,
            rect.w * dpi_scale, rect.h * dpi_scale);
}


void r_clear(Color clr) {
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
