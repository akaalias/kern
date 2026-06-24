/* stub_renderer.h — headless capture implementation of renderer.h.
 *
 * Deterministic text metrics (every glyph is cell_w wide, cell_h tall) and a
 * record of every draw call, so navigation/markdown layout can be unit-tested
 * with no GL/window. Defaults: cell 10x20, window 800x600. */
#ifndef KERN_STUB_RENDERER_H
#define KERN_STUB_RENDERER_H

#include "renderer.h"   /* mu_Rect, mu_Color, mu_Vec2, FONT_* */

typedef struct { char ch[8]; int x, y, style; mu_Color color; } StubText;
typedef struct { mu_Rect rect; mu_Color color; } StubRect;

#define STUB_MAX 8192
extern StubText stub_texts[STUB_MAX];
extern int      stub_text_count;
extern StubRect stub_rects[STUB_MAX];
extern int      stub_rect_count;

void stub_reset(void);                                  /* clear captured ops + style */
void stub_set_metrics(int cell_w, int cell_h, int win_w, int win_h);

#endif /* KERN_STUB_RENDERER_H */
