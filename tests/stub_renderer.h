/* stub_renderer.h — headless capture implementation of renderer.h.
 *
 * Deterministic text metrics (every glyph is cell_w wide, cell_h tall) and a
 * record of every draw call, so navigation/markdown layout can be unit-tested
 * with no GL/window. Defaults: cell 10x20, window 800x600. */
#ifndef KERN_STUB_RENDERER_H
#define KERN_STUB_RENDERER_H

#include "renderer.h"   /* Rect, Color, Vec2, FONT_* */

typedef struct { char ch[8]; int x, y, style; Color color; } StubText;
typedef struct { Rect rect; Color color; } StubRect;

/* a single op stream in draw order (text + rects interleaved) — used by the
 * snapshot harness so goldens capture exact draw ordering. */
typedef enum { STUB_OP_TEXT, STUB_OP_RECT } StubOpKind;
typedef struct {
  StubOpKind kind;
  char    ch[8];   /* TEXT only */
  Rect rect;    /* TEXT: x,y at glyph origin (w,h = 0); RECT: full rect */
  int     style;   /* TEXT only */
  Color color;
} StubOp;

#define STUB_MAX 8192
extern StubText stub_texts[STUB_MAX];
extern int      stub_text_count;
extern StubRect stub_rects[STUB_MAX];
extern int      stub_rect_count;
extern StubOp   stub_ops[STUB_MAX];
extern int      stub_op_count;

void stub_reset(void);                                  /* clear captured ops + style */
void stub_set_metrics(int cell_w, int cell_h, int win_w, int win_h);
void stub_set_style_extra(int style, int extra);       /* per-style extra px/char */

#endif /* KERN_STUB_RENDERER_H */
