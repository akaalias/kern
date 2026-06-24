/* gfx.h — minimal geometry/color types + mouse-button flags.
 *
 * The editor draws immediately via renderer.h; it just needs these few value
 * types and their constructors. This ~25-line header is all that remained after
 * the vendored UI library was removed. */
#ifndef GFX_H
#define GFX_H

typedef struct { int x, y; } Vec2;
typedef struct { int x, y, w, h; } Rect;
typedef struct { unsigned char r, g, b, a; } Color;

static inline Vec2 vec2(int x, int y) {
  Vec2 v = { x, y };
  return v;
}
static inline Rect rect(int x, int y, int w, int h) {
  Rect r = { x, y, w, h };
  return r;
}
static inline Color color(int r, int g, int b, int a) {
  Color c = { (unsigned char)r, (unsigned char)g, (unsigned char)b, (unsigned char)a };
  return c;
}

/* mouse-button bit flags */
enum { MOUSE_LEFT = 1, MOUSE_RIGHT = 2, MOUSE_MIDDLE = 4 };

#endif /* GFX_H */
