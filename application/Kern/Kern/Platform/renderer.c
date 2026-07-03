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
static float font_scale = 1.0f;   /* quad-level draw/measure scale; see renderer.h */

/* Load a TTF at an exact path into `style`. On failure: exit if `required`,
   else leave the slot unloaded and return 0 (so an optional font degrades). */
static int load_ttf_path(int style, const char *path, int required) {
  FontData *fd = &fonts[style];
  FILE *f = fopen(path, "rb");
  if (!f) { if (required) { fprintf(stderr, "cannot open font: %s\n", path); exit(1); } return 0; }
  fseek(f, 0, SEEK_END);
  long sz = ftell(f);
  fseek(f, 0, SEEK_SET);
  fd->file_buf = malloc(sz);
  if (!fd->file_buf || fread(fd->file_buf, 1, sz, f) != (size_t)sz) {
    fclose(f); free(fd->file_buf); fd->file_buf = NULL;
    if (required) exit(1); return 0;
  }
  fclose(f);
  if (!stbtt_InitFont(&fd->info, fd->file_buf, 0)) {
    if (required) { fprintf(stderr, "stbtt_InitFont failed for %s\n", path); exit(1); }
    free(fd->file_buf); fd->file_buf = NULL; return 0;
  }
  fd->loaded = 1;
  return 1;
}

static void load_ttf(int style, const char *filename) {
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
  load_ttf_path(style, path, 1);
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
  SDL_GL_SetAttribute(SDL_GL_STENCIL_SIZE, 8);   /* for rounded-shape clip masks */
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
  /* Native system font (San Francisco) for chrome like the publish overlay.
     Optional: on older macOS / non-mac it just falls back to FONT_REGULAR. */
  load_ttf_path(FONT_UI, "/System/Library/Fonts/SFNS.ttf", 0);
  rebuild_all_font_atlases();

  current_font = FONT_REGULAR;
  current_tex = fonts[FONT_REGULAR].tex;
  assert(glGetError() == 0);
}


void r_draw_rect(Rect rect, Color color) {
  switch_texture(white_tex);
  push_quad_uv(rect.x, rect.y, rect.w, rect.h, 0, 0, 1, 1, color);
}

/* Filled rounded rectangle with anti-aliased edges. Builds a coverage mask from
   the rounded-rect signed-distance field into a GL_ALPHA texture, then draws it
   modulated by `color` (same path as glyphs: alpha = coverage × color.a). Small
   overlay buttons only, so regenerating the mask per call is cheap. */
static GLuint shape_tex;
static unsigned char *shape_buf;
static int shape_bufcap;
void r_draw_round_rect(Rect dst, int radius, Color color) {
  if (dst.w <= 0 || dst.h <= 0) return;
  if (radius < 0) radius = 0;
  int w = dst.w, h = dst.h;
  int maxr = (w < h ? w : h) / 2;
  if (radius > maxr) radius = maxr;

  int n = w * h;
  if (n > shape_bufcap) { free(shape_buf); shape_buf = malloc(n); shape_bufcap = n; }
  if (!shape_buf) { r_draw_rect(dst, color); return; }

  double hw = w / 2.0, hh = h / 2.0, r = radius;
  for (int y = 0; y < h; y++) {
    for (int x = 0; x < w; x++) {
      double px = (x + 0.5) - hw, py = (y + 0.5) - hh;
      double ax = fabs(px) - (hw - r), ay = fabs(py) - (hh - r);
      double axc = ax > 0 ? ax : 0, ayc = ay > 0 ? ay : 0;
      double outside = sqrt(axc * axc + ayc * ayc);
      double inside = fmin(fmax(ax, ay), 0.0);
      double sd = outside + inside - r;      /* <0 inside the shape */
      double cov = 0.5 - sd;                  /* ~1px anti-aliased edge */
      if (cov < 0) cov = 0; else if (cov > 1) cov = 1;
      shape_buf[y * w + x] = (unsigned char)(cov * 255.0 + 0.5);
    }
  }

  flush();
  if (!shape_tex) glGenTextures(1, &shape_tex);
  glBindTexture(GL_TEXTURE_2D, shape_tex);
  glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
  glTexImage2D(GL_TEXTURE_2D, 0, GL_ALPHA, w, h, 0, GL_ALPHA, GL_UNSIGNED_BYTE, shape_buf);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
  current_tex = shape_tex;
  push_quad_uv(dst.x, dst.y, w, h, 0, 0, 1, 1, color);
  flush();
  switch_texture(white_tex);
}

/* Gaussian-blur whatever is already drawn under `rect`, in place and clipped to
   the rect. Copies the framebuffer region into a texture, then convolves it with
   a (2k+1)² gaussian kernel: each tap redraws the captured region offset by the
   kernel sample, modulated by its normalized weight, accumulated additively so
   the result is a true weighted average (not stacked ghosts). A scissor keeps the
   smear inside the rect so it can't bleed past the panel. `radius` = blur extent
   in logical px. No shaders needed. */
static GLuint blur_tex;
void r_blur_rect(Rect rect, int radius) {
  if (rect.w <= 0 || rect.h <= 0 || radius < 1) return;
  flush();                                   /* make sure the text underneath is in the buffer */

  /* framebuffer region in pixels, bottom-left origin (mirrors r_set_clip_rect) */
  int px = (int)(rect.x * dpi_scale);
  int pw = (int)(rect.w * dpi_scale);
  int ph = (int)(rect.h * dpi_scale);
  int py = draw_height - (int)((rect.y + rect.h) * dpi_scale);
  if (px < 0) { pw += px; px = 0; }
  if (py < 0) { ph += py; py = 0; }
  if (px + pw > draw_width)  pw = draw_width  - px;
  if (py + ph > draw_height) ph = draw_height - py;
  if (pw <= 0 || ph <= 0) return;

  if (!blur_tex) glGenTextures(1, &blur_tex);
  glBindTexture(GL_TEXTURE_2D, blur_tex);
  glCopyTexImage2D(GL_TEXTURE_2D, 0, GL_RGB, px, py, pw, ph, 0);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);

  /* normalized 1D gaussian; the 2D weight of a tap is g[ix]·g[iy] (separable) */
  const int k = 3;
  float sigma = radius / 2.0f; if (sigma < 0.5f) sigma = 0.5f;
  float stepw = (float)radius / k;
  float g[2 * k + 1]; float gsum = 0;
  for (int i = -k; i <= k; i++) { float d = i * stepw; g[i + k] = expf(-(d * d) / (2 * sigma * sigma)); gsum += g[i + k]; }
  for (int i = 0; i < 2 * k + 1; i++) g[i] /= gsum;

  GLint sbox[4]; glGetIntegerv(GL_SCISSOR_BOX, sbox);
  glScissor(px, py, pw, ph);                 /* contain the blur to the panel */
  current_tex = blur_tex;                    /* captured region drawn back, v-flipped */

  /* center tap replaces the region; the rest accumulate */
  glBlendFunc(GL_ONE, GL_ZERO);
  { GLubyte cw = (GLubyte)(g[k] * g[k] * 255.0f + 0.5f); Color c = { cw, cw, cw, 255 };
    push_quad_uv(rect.x, rect.y, rect.w, rect.h, 0, 1, 1, 0, c); }
  flush();
  glBlendFunc(GL_ONE, GL_ONE);
  for (int iy = -k; iy <= k; iy++)
    for (int ix = -k; ix <= k; ix++) {
      if (ix == 0 && iy == 0) continue;
      GLubyte cw = (GLubyte)(g[ix + k] * g[iy + k] * 255.0f + 0.5f);
      if (cw == 0) continue;
      Color c = { cw, cw, cw, 255 };
      push_quad_uv(rect.x + ix * stepw, rect.y + iy * stepw, rect.w, rect.h, 0, 1, 1, 0, c);
    }
  flush();

  glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);   /* restore normal blending */
  glScissor(sbox[0], sbox[1], sbox[2], sbox[3]);       /* restore clip */
  switch_texture(white_tex);                 /* leave the batcher on the solid texel */
}

/* Arbitrary-shape clip via the stencil buffer. Bracket drawing like:
     r_clip_mask_begin();   // start defining the mask
     ...draw the mask shape (any r_draw_rect calls)...   // color is not written
     r_clip_mask_use();     // subsequent draws are clipped to that shape
     ...draw the clipped content...
     r_clip_mask_end();     // back to normal
   Used to round off the typewriter guards (blur + tint) to their corner. */
void r_clip_mask_begin(void) {
  flush();
  glClearStencil(0);
  glClear(GL_STENCIL_BUFFER_BIT);            /* within the active scissor */
  glEnable(GL_STENCIL_TEST);
  glStencilMask(0xFF);
  glStencilFunc(GL_ALWAYS, 1, 0xFF);
  glStencilOp(GL_KEEP, GL_KEEP, GL_REPLACE); /* shape pixels → stencil 1 */
  glColorMask(GL_FALSE, GL_FALSE, GL_FALSE, GL_FALSE);
}
void r_clip_mask_use(void) {
  flush();                                   /* commit the mask geometry */
  glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
  glStencilFunc(GL_EQUAL, 1, 0xFF);          /* draw only where the mask is set */
  glStencilOp(GL_KEEP, GL_KEEP, GL_KEEP);
}
void r_clip_mask_end(void) {
  flush();
  glDisable(GL_STENCIL_TEST);
}

/* Draw an RGBA image scaled into `dst` as an alpha-blended quad. The caller
   bakes a smooth circular alpha into the pixels (see KernApp.decodeRGBA), so
   the round avatar comes out anti-aliased without a 1-bit stencil clip. The
   texture is (re)uploaded only when the pixel pointer changes, so redrawing the
   same avatar every frame is cheap. */
static GLuint image_tex;
static const unsigned char *image_tex_src;
void r_draw_image_circle(Rect dst, const unsigned char *rgba, int iw, int ih) {
  if (!rgba || iw <= 0 || ih <= 0 || dst.w <= 0 || dst.h <= 0) return;
  flush();
  if (!image_tex) glGenTextures(1, &image_tex);
  if (image_tex_src != rgba) {
    glBindTexture(GL_TEXTURE_2D, image_tex);
    glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, iw, ih, 0, GL_RGBA, GL_UNSIGNED_BYTE, rgba);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    image_tex_src = rgba;
  }

  switch_texture(image_tex);
  Color white = { 255, 255, 255, 255 };   /* modulate: show the image's own colors */
  push_quad_uv(dst.x, dst.y, dst.w, dst.h, 0, 0, 1, 1, white);
  flush();
  switch_texture(white_tex);
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

      float dx = x + g->xoff * font_scale;
      float dy = y + (fd->baseline + g->yoff) * font_scale;

      push_quad_uv(dx, dy, g->bw * font_scale, g->bh * font_scale, u0, v0, u1, v1, color);
    }

    x += g->xadvance * font_scale;
  }
}




int r_get_text_width(const char *text, int len) {
  FontData *fd = &fonts[current_font];
  float w = 0;
  int i = 0;
  while (i < len && text[i]) {
    int cp;
    i += utf8_decode(text + i, len - i, &cp);
    w += glyph_for(fd, cp)->xadvance * font_scale;
  }
  return (int)(w + 0.5f);
}


int r_get_text_height(void) {
  return (int)(font_size * font_scale + 0.5f);
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

void r_set_font_scale(float scale) {
  font_scale = scale > 0.0f ? scale : 1.0f;
}

void r_set_font_style(int style) {
  if (style >= 0 && style < FONT_COUNT && fonts[style].loaded) {
    current_font = style;
  }
}

int r_get_font_style(void) {
  return current_font;
}

int r_ui_font_style(void) {
  return fonts[FONT_UI].loaded ? FONT_UI : FONT_REGULAR;
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
