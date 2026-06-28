/* navigation.h — Window metrics, word wrapping, cursor navigation, search */
#ifndef NAVIGATION_H
#define NAVIGATION_H

#include "editor_types.h"

/* window / page metrics */
int nav_win_w(void);
int nav_win_h(void);
int nav_page_w(void);
int nav_page_margin(void);
int nav_line_height(void);

/* Normal-mode top page margin in pixels: virtual whitespace above line 0 so the
   first line rests below the window top (negate for the min scroll_y). */
int nav_top_margin(const ViewState *vs);

/* word wrapping */
int nav_count_wraps(Line *l);
int nav_get_wrap_breaks(Line *l, int *starts, int max_starts);
int nav_total_visual_lines(EditorState *ed);

/* Reflow (invalidate all cached wraps) only if the page width changed since the
   last reflow, recording the new width in vs->wrap_page_w. Returns 1 if it
   reflowed, 0 if the width was unchanged (a no-op). Use on resize, where the
   font is constant so wraps depend solely on the page width. */
int nav_maybe_reflow(EditorState *ed, ViewState *vs);

/* coordinate mapping */
int nav_visual_to_logical(EditorState *ed, int visual_line, int *visual_offset);
int nav_logical_to_visual(EditorState *ed, int logical_line);
int nav_cursor_to_visual(EditorState *ed, int cline, int ccol);

/* cursor */
void nav_cursor_clamp(EditorState *ed);
void nav_ensure_cursor_visible(EditorState *ed, ViewState *vs);

/* whether a heading's "### " markers fit in the left margin (so they hang there
   rather than rendering inline). Mirrors do_render's layout decision. */
int nav_heading_markers_hang(Line *l);

/* absolute screen x of the caret at (line, col), matching the render pass
   (list hanging indent + per-span font metrics + hung heading markers). */
int nav_cursor_x(EditorState *ed, int line, int col);

/* Publish the symbol-substitution reveal range for logical line `ln` (the caret
   point unioned with the active selection's extent on that line). Call before any
   md_draw_text / md_col_x / md_x_to_col for `ln` so they collapse/reveal the same
   tokens the render pass does. */
void nav_sub_reveal_for_line(EditorState *ed, int ln);

/* Move the caret one *visual* row (dir = +1 down, -1 up), keeping a pixel goal
   column. Wrapped paragraphs move a screen row at a time, not a whole logical
   line. Backs C-n/C-p and the down/up arrows. */
void nav_visual_move(EditorState *ed, ViewState *vs, int dir);

/* The scroll_y that puts the cursor's visual row `fraction` of the way down the
   page (0 = top, 0.5 = center, 0.382 = golden), floored at 0. */
float nav_pin_target(EditorState *ed, ViewState *vs, float fraction);

/* Jump scroll_y (and the ease target) to nav_pin_target — instant, no glide.
   Used by C-l recenter. */
void nav_pin_cursor(EditorState *ed, ViewState *vs, float fraction);

/* click */
void nav_click_to_cursor(EditorState *ed, ViewState *vs, int mx, int my);

/* Typewriter hard right margin: 1 if inserting `add` on the caret's line would
   push its width past the writable page width, so the insert should be blocked
   (Enter still starts a new line). Measured in the body font, like the wrap. */
int nav_at_right_margin(EditorState *ed, const char *add);

/* search */
void nav_search_find_next(EditorState *ed, ViewState *vs, int from_line, int from_col);
void nav_search_find_prev(EditorState *ed, ViewState *vs, int from_line, int from_col);
void nav_search_find_first(EditorState *ed, ViewState *vs);
void nav_search_find_current_dir(EditorState *ed, ViewState *vs);

/* status */
void nav_status_set(ViewState *vs, const char *msg);
const char *nav_status_get(EditorState *ed, ViewState *vs);

#endif
