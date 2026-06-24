/* gfx.h — minimal geometry/color types + mouse-button flags.
 *
 * The editor only ever used microui for these few value types and their
 * constructors (it draws immediately via renderer.h, not microui widgets), so
 * this ~25-line header replaces the whole vendored library. Names keep the
 * mu_/MU_ prefix for now to avoid a churny rename; that's a cosmetic follow-up.
 * Layout matches the old microui structs exactly (renderer.c reads the fields). */
#ifndef GFX_H
#define GFX_H

typedef struct { int x, y; } mu_Vec2;
typedef struct { int x, y, w, h; } mu_Rect;
typedef struct { unsigned char r, g, b, a; } mu_Color;

static inline mu_Vec2 mu_vec2(int x, int y) {
  mu_Vec2 v = { x, y };
  return v;
}
static inline mu_Rect mu_rect(int x, int y, int w, int h) {
  mu_Rect r = { x, y, w, h };
  return r;
}
static inline mu_Color mu_color(int r, int g, int b, int a) {
  mu_Color c = { (unsigned char)r, (unsigned char)g, (unsigned char)b, (unsigned char)a };
  return c;
}

/* mouse-button bit flags (kept identical to microui's values) */
enum { MU_MOUSE_LEFT = 1, MU_MOUSE_RIGHT = 2, MU_MOUSE_MIDDLE = 4 };

#endif /* GFX_H */
